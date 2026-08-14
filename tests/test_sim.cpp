#include "game.hpp"
#include "profile.hpp"
#include "daily.hpp"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::string temporaryTestPath(const char* filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

void advance(ta::GameSim& sim, int ticks, bool useUltimate = false) {
    for (int i = 0; i < ticks && !sim.isGameOver() && !sim.isVictory(); ++i) {
        if (sim.upgradePending()) sim.chooseUpgrade(0);
        if (useUltimate && i % 300 == 0) sim.activateUltimate();
        sim.tick();
    }
}
} // namespace

int main() {
    const ta::InputBindings defaultBindings = ta::defaultInputBindings();
    check(defaultBindings.key(ta::InputAction::Confirm) == 13 && defaultBindings.key(ta::InputAction::Ultimate) == 32, "default input action map is not stable");
    check(ta::validInputKey(defaultBindings.key(ta::InputAction::Pause)) && std::string(ta::inputActionName(ta::InputAction::Ultimate)).size() > 0, "input action metadata is incomplete");
    // Same seed and commands must produce exactly the same simulation state.
    ta::GameSim a(12345u), b(12345u);
    a.setWeapon(ta::Weapon::ExplosiveCannon); b.setWeapon(ta::Weapon::ExplosiveCannon);
    a.setSkull(ta::Skull::Haste); b.setSkull(ta::Skull::Haste);
    a.reset(12345u); b.reset(12345u);
    for (int i = 0; i < 1600; ++i) {
        if (a.upgradePending()) { a.chooseUpgrade(0); b.chooseUpgrade(0); }
        a.tick(); b.tick();
        check(a.stateHash() == b.stateHash(), "determinism diverged for identical seed and commands");
        if (a.isGameOver() || a.isVictory()) break;
    }

    ta::GameSim run(99u);
    run.setWeapon(ta::Weapon::RapidFire);
    run.reset(99u);
    advance(run, 5000, true);
    check(run.stats().ticks > 0, "simulation did not advance");
    check(run.stats().kills > 0, "tower never killed an enemy");
    check(run.waveNumber() > 1 || run.isVictory(), "wave progression did not occur");
    check(run.stats().upgrades > 0, "upgrade choices were never presented/selected");
    if (run.isGameOver()) check(!run.failureGuidance().empty(), "terminal run did not expose failure guidance");
    check(run.stats().ultimates > 0, "ultimate was never activated");
    check(run.stats().shotsFired > 0, "tower never produced a firing event");
    check(run.stats().damageDealt > 0, "combat telemetry did not record damage");

    ta::GameSim complete(0xBEEFu);
    int completeTicks = 0;
    bool sawBossPhaseTwo = false;
    bool sawBossTelegraph = false;
    while (!complete.isGameOver() && !complete.isVictory() && completeTicks++ < 100000) {
        if (complete.upgradePending()) complete.chooseUpgrade(0);
        if (completeTicks % 240 == 0) complete.activateUltimate();
        complete.tick();
        for (const ta::Enemy& enemy : complete.enemies()) {
            if (enemy.boss && enemy.phase >= 2) sawBossPhaseTwo = true;
            if (enemy.boss && enemy.telegraphTicks > 0) sawBossTelegraph = true;
        }
    }
    check(complete.isVictory(), "a fully played run did not reach the victory state");
    check(complete.waveNumber() == 10, "victory did not occur on the final wave");
    check(sawBossPhaseTwo, "boss never entered its second phase");
    check(sawBossTelegraph && complete.stats().bossAttacks > 0, "boss never emitted or resolved a telegraphed attack");
    const ta::RunSummary summary = complete.runSummary();
    check(summary.victory && summary.score == complete.stats().score && summary.kills == complete.stats().kills && summary.durationTicks == complete.stats().ticks && summary.scoreMultiplier == complete.skullScoreMultiplier(), "run summary did not mirror terminal statistics");

    ta::GameSim skullNone(777u), skullSwarm(777u);
    skullNone.reset(777u);
    skullSwarm.setSkull(ta::Skull::Swarm); skullSwarm.reset(777u);
    advance(skullNone, 100, false); advance(skullSwarm, 100, false);
    check(skullSwarm.enemiesSpawnedThisWave() >= skullNone.enemiesSpawnedThisWave(), "swarm skull did not increase spawn pressure");
    ta::GameSim skullCombo(778u), skullSingle(778u);
    skullCombo.setSkullMask(static_cast<ta::SkullMask>((1u << static_cast<unsigned int>(ta::Skull::Swarm)) | (1u << static_cast<unsigned int>(ta::Skull::Greed))));
    skullSingle.setSkull(ta::Skull::Swarm);
    skullCombo.reset(778u); skullSingle.reset(778u);
    advance(skullCombo, 500, false); advance(skullSingle, 500, false);
    check(skullCombo.skullMask() != skullSingle.skullMask() && skullCombo.skullScoreMultiplier() > skullSingle.skullScoreMultiplier(), "skull combinations did not compound risk/reward");

    // Swarm must never turn the final boss wave into a mixed multi-spawn wave.
    ta::ContentConfig fastBossContent;
    fastBossContent.waveEnemyBudget.fill(1);
    fastBossContent.waveSpawnInterval.fill(1);
    fastBossContent.enemyHealthScale.fill(0.05f);
    fastBossContent.enemySpeedScale.fill(1.0f);
    for (auto& weights : fastBossContent.waveEnemyTypeWeight) {
        weights.fill(0.0f);
        weights[static_cast<std::size_t>(ta::EnemyType::Grunt)] = 100.0f;
    }
    fastBossContent.weaponDamage[static_cast<std::size_t>(ta::Weapon::RapidFire)] = 1000.0f;
    fastBossContent.weaponCooldown[static_cast<std::size_t>(ta::Weapon::RapidFire)] = 1;
    ta::GameSim swarmBossWave(779u);
    swarmBossWave.setContentConfig(fastBossContent);
    swarmBossWave.setWeapon(ta::Weapon::RapidFire);
    swarmBossWave.setSkull(ta::Skull::Swarm);
    swarmBossWave.reset(779u);
    int bossWaveSafety = 0;
    while (swarmBossWave.waveNumber() < 10 && !swarmBossWave.isGameOver() && !swarmBossWave.isVictory() && bossWaveSafety++ < 10000) {
        if (swarmBossWave.upgradePending()) swarmBossWave.chooseUpgrade(0);
        swarmBossWave.tick();
    }
    if (swarmBossWave.waveNumber() == 10 && swarmBossWave.upgradePending()) swarmBossWave.chooseUpgrade(0);
    if (swarmBossWave.waveNumber() == 10 && !swarmBossWave.isGameOver() && !swarmBossWave.isVictory()) swarmBossWave.tick();
    check(swarmBossWave.waveNumber() == 10 && swarmBossWave.enemiesTargetThisWave() == 1, "swarm skull changed the final boss wave spawn target");

    ta::ContentConfig endlessContent = fastBossContent;
    endlessContent.enemySpeedScale.fill(1.0f);
    endlessContent.enemyHealthScale.fill(0.01f);
    endlessContent.skullLives[0] = 1000;
    endlessContent.bossAttackCooldownTicks = 100000;
    endlessContent.weaponDamage[static_cast<std::size_t>(ta::Weapon::RapidFire)] = 10000.0f;
    ta::GameSim endless(780u);
    endless.setContentConfig(endlessContent);
    endless.setEndless(true);
    endless.setWeapon(ta::Weapon::RapidFire);
    endless.reset(780u);
    int endlessSafety = 0;
    while (endless.waveNumber() <= 11 && !endless.isGameOver() && endlessSafety++ < 20000) {
        if (endless.upgradePending()) endless.chooseUpgrade(0);
        endless.tick();
    }
    check(endless.endless() && endless.waveNumber() > 10 && !endless.isVictory(), "endless mode did not continue beyond the standard final wave");

    ta::GameSim choices(5u);
    choices.reset(5u);
    int safety = 0;
    while (!choices.upgradePending() && safety++ < 5000) choices.tick();
    check(choices.upgradePending(), "first upgrade draft was not reached");
    check(choices.pendingChoices().size() == 3, "upgrade draft does not contain three choices");
    check(choices.rerollUpgradeChoices() && choices.upgradePending() && choices.rerollsRemaining() == 0, "single upgrade reroll was not deterministic or limited");
    check(!choices.rerollUpgradeChoices(), "upgrade reroll could be used more than once");
    choices.chooseUpgrade(1);
    check(choices.stats().upgrades == 1 && !choices.upgradePending(), "upgrade selection did not apply");
    for (int upgrade = 0; upgrade < 15; ++upgrade) check(std::string(ta::upgradeDescription(static_cast<ta::Upgrade>(upgrade))).size() > 0, "upgrade has no player-facing effect description");
    for (int support = 0; support < 5; ++support) check(std::string(ta::supportModuleName(static_cast<ta::SupportModule>(support))).size() > 0 && std::string(ta::supportModuleDescription(static_cast<ta::SupportModule>(support))).size() > 0, "support module has no player-facing description");
    ta::GameSim economyBase(606u), economySupport(606u);
    economySupport.setSupport(ta::SupportModule::CreditRelay);
    economyBase.reset(606u); economySupport.reset(606u);
    advance(economyBase, 700, false); advance(economySupport, 700, false);
    check(economySupport.currencyAmount() > economyBase.currencyAmount(), "credit relay did not increase run currency");

    ta::GameSim ultimate(42u);
    ultimate.activateUltimate();
    check(ultimate.stats().ultimates == 1, "ultimate failed while ready");
    ultimate.activateUltimate();
    check(ultimate.stats().ultimates == 1, "ultimate activated while on cooldown");
    advance(ultimate, 600, false);
    check(ultimate.ultimateRatio() >= 0.0f && ultimate.ultimateRatio() <= 1.0f, "ultimate ratio left valid range");

    // Replay and profile files are versioned, deterministic, and reject malformed input.
    ta::ReplayData replay;
    replay.seed = 8080u;
    replay.weapon = ta::Weapon::SniperRailgun;
    replay.support = ta::SupportModule::CorrosionAmp;
    replay.skull = ta::Skull::Greed;
    replay.skullMask = static_cast<ta::SkullMask>((1u << static_cast<unsigned int>(ta::Skull::Greed)) | (1u << static_cast<unsigned int>(ta::Skull::Swarm)));
    replay.ultimate = ta::Ultimate::GravityShift;
    replay.ultimateModule = ta::UltimateModule::GravityWell;
    replay.autoUltimate = true;
    replay.arena = ta::Arena::NeonRuins;
    replay.endless = true;
    replay.events = {{30, ta::ReplayAction::Ultimate, 0}, {61, ta::ReplayAction::Upgrade, 2}};
    ta::ReplayData decoded;
    std::string error;
    check(ta::ReplayData::deserialize(replay.serialize(), decoded, &error), "replay serialization failed");
    check(decoded.seed == replay.seed && decoded.weapon == replay.weapon && decoded.support == replay.support && decoded.skull == replay.skull && decoded.skullMask == replay.skullMask && decoded.ultimate == replay.ultimate && decoded.ultimateModule == replay.ultimateModule && decoded.autoUltimate == replay.autoUltimate && decoded.arena == replay.arena && decoded.endless && decoded.dailyDateKey == replay.dailyDateKey && decoded.events.size() == 2, "replay round trip changed data");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nweapon invalid\n", decoded, &error), "malformed replay was accepted");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nevent 30 1 0\nevent 20 1 0\n", decoded, &error), "out-of-order replay events were accepted");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nevent 30 1 3\n", decoded, &error), "invalid replay upgrade choice was accepted");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nevent 30 3 1\n", decoded, &error), "invalid replay reroll value was accepted");
    const std::string replayPath = temporaryTestPath("tower_ascend_test.replay");
    check(replay.save(replayPath, &error) && ta::ReplayData::load(replayPath, decoded, &error), "replay file persistence failed");

    ta::ReplayData script;
    script.seed = 456u; script.weapon = ta::Weapon::ArcaneBeam; script.skull = ta::Skull::None; script.arena = ta::Arena::EmberCrater;
    ta::GameSim scripted(456u); scripted.setWeapon(script.weapon); scripted.setSkull(script.skull); scripted.setArena(script.arena); scripted.reset(456u);
    for (std::uint32_t tick = 1; tick <= 900 && !scripted.isGameOver() && !scripted.isVictory(); ++tick) {
        if (scripted.upgradePending()) { scripted.chooseUpgrade(0); script.events.push_back({tick, ta::ReplayAction::Upgrade, 0}); }
        if (tick % 240 == 0) { scripted.activateUltimate(); script.events.push_back({tick, ta::ReplayAction::Ultimate, 0}); }
        scripted.tick();
    }
    const std::uint32_t expectedReplayHash = scripted.stateHash();
    std::uint32_t actualReplayHash = 0;
    check(ta::replayFinalHash(script, 900, actualReplayHash), "replay command stream was not fully consumed");
    check(actualReplayHash == expectedReplayHash, "replay playback hash differs from recorded run");
    ta::GameSim hashA(123u), hashB(123u);
    hashA.tick(); hashB.tick();
    check(hashA.stateHash() == hashB.stateHash(), "complete state hash diverged for equal simulations");
    hashA.activateUltimate();
    check(hashA.stateHash() != hashB.stateHash(), "state hash omitted a meaningful cooldown/state change");

    ta::GameSim autoUltimate(222u);
    autoUltimate.setAutoUltimate(true);
    autoUltimate.reset(222u);
    for (int tick = 0; tick < 1500 && !autoUltimate.isGameOver() && !autoUltimate.isVictory(); ++tick) {
        if (autoUltimate.upgradePending()) autoUltimate.chooseUpgrade(0);
        autoUltimate.tick();
    }
    check(autoUltimate.stats().ultimates == 0, "legacy automatic ultimate flag changed manual-only combat");

    for (int ultimate = 0; ultimate < 5; ++ultimate) {
        ta::GameSim ultimateRun(900u + static_cast<std::uint32_t>(ultimate));
        ultimateRun.setUltimate(static_cast<ta::Ultimate>(ultimate));
        ultimateRun.reset(900u + static_cast<std::uint32_t>(ultimate));
        for (int tick = 0; tick < 240 && !ultimateRun.isGameOver(); ++tick) ultimateRun.tick();
        ultimateRun.activateUltimate();
        check(ultimateRun.stats().ultimates == 1, "selectable ultimate failed to activate");
        check(std::string(ta::ultimateName(static_cast<ta::Ultimate>(ultimate))).size() > 0, "ultimate has no display name");
    }

    ta::ProfileData profile;
    profile.bestScore = 9001; profile.bestWave = 10; profile.runsCompleted = 4; profile.totalKills = 77; profile.reducedFlashes = true; profile.highContrast = true; profile.masterVolume = 60; profile.musicVolume = 70; profile.sfxVolume = 80; profile.uiVolume = 60;
    profile.uiScalePercent = 120; profile.colorBlindPalette = 2; profile.subtitles = false; profile.vibration = false;
    profile.equippedSupportModule = static_cast<std::uint8_t>(ta::SupportModule::RepairDrones);
    profile.equippedWeapon = static_cast<std::uint8_t>(ta::Weapon::FrostBlaster);
    profile.equippedUltimate = static_cast<std::uint8_t>(ta::Ultimate::GravityShift);
    profile.unlockedUltimateModulesMask = 1u;
    profile.equippedUltimateModule = static_cast<std::uint8_t>(ta::UltimateModule::MeteorQuickCharge);
    profile.supportModuleLevels[static_cast<std::size_t>(ta::SupportModule::RepairDrones)] = 2;
    profile.inputBindings.key(ta::InputAction::Ultimate) = 'e';
    const std::string profilePath = temporaryTestPath("tower_ascend_test.profile");
    ta::ProfileData loadedProfile;
    check(ta::saveProfile(profilePath, profile, &error) && ta::loadProfile(profilePath, loadedProfile, &error), "profile persistence failed");
    check(loadedProfile.bestScore == profile.bestScore && loadedProfile.reducedFlashes == profile.reducedFlashes && loadedProfile.highContrast == profile.highContrast && loadedProfile.masterVolume == profile.masterVolume && loadedProfile.musicVolume == profile.musicVolume && loadedProfile.sfxVolume == profile.sfxVolume && loadedProfile.uiVolume == profile.uiVolume && loadedProfile.uiScalePercent == profile.uiScalePercent && loadedProfile.colorBlindPalette == profile.colorBlindPalette && loadedProfile.subtitles == profile.subtitles && loadedProfile.vibration == profile.vibration && loadedProfile.equippedSupportModule == profile.equippedSupportModule && loadedProfile.equippedWeapon == profile.equippedWeapon && loadedProfile.equippedUltimate == profile.equippedUltimate && loadedProfile.unlockedUltimateModulesMask == profile.unlockedUltimateModulesMask && loadedProfile.equippedUltimateModule == profile.equippedUltimateModule && loadedProfile.supportModuleLevels == profile.supportModuleLevels, "profile round trip changed data");
    check(loadedProfile.version == 15 && loadedProfile.inputBindings.key(ta::InputAction::Ultimate) == 'e', "profile input bindings did not round trip");
    profile.bestScore += 1;
    check(ta::saveProfile(profilePath, profile, &error) && !std::filesystem::exists(profilePath + ".tmp"), "profile replacement left a temporary save behind");
    check(ta::isSkinUnlocked(loadedProfile, ta::TowerSkin::Azure), "default Azure skin was not unlocked");
    check(!ta::isSkinUnlocked(loadedProfile, ta::TowerSkin::Nebula), "premium cosmetic was unlocked without shards");
    loadedProfile.cosmeticShards = 100;
    check(ta::unlockSkin(loadedProfile, ta::TowerSkin::Nebula), "cosmetic unlock failed with sufficient shards");
    check(ta::equipSkin(loadedProfile, ta::TowerSkin::Nebula) && loadedProfile.equippedSkin == static_cast<std::uint8_t>(ta::TowerSkin::Nebula), "cosmetic equip failed");
    const auto shardsBeforeAward = loadedProfile.cosmeticShards;
    ta::SimStats rewardStats; rewardStats.wave = 10; rewardStats.kills = 80;
    ta::awardRunCosmetics(loadedProfile, rewardStats, true);
    check(loadedProfile.cosmeticShards > shardsBeforeAward, "completed run did not award cosmetic shards");
    ta::ProfileData customReward;
    ta::awardRunCosmetics(customReward, rewardStats, true, 37);
    check(customReward.cosmeticShards == static_cast<std::uint32_t>(rewardStats.kills / 8 + 40 + 37), "daily reward value was not applied explicitly");
    ta::awardRunProgression(customReward, rewardStats, true, ta::Ultimate::MeteorRain);
    ta::awardRunProgression(customReward, rewardStats, true, ta::Ultimate::MeteorRain);
    ta::awardRunProgression(customReward, rewardStats, true, ta::Ultimate::MeteorRain);
    check(customReward.coreParts > 0, "run progression did not award core parts");
    ta::ProfileData milestoneReward;
    milestoneReward.runsCompleted = 5;
    check(ta::awardRunProgression(milestoneReward, rewardStats, false, ta::Ultimate::BulletStorm) == 1u && milestoneReward.legendCores == 1u, "non-daily milestone did not provide fallback legend core progress");
    check(ta::claimDailyLegendCores(customReward, 20260812u, 2u), "first daily legend reward was not claimable");
    check(customReward.legendCores == 2u && !ta::claimDailyLegendCores(customReward, 20260812u, 2u), "daily legend reward was claimable twice");
    check(customReward.ultimateMasteryRuns[static_cast<std::size_t>(ta::Ultimate::MeteorRain)] == 3, "completed runs did not advance ultimate mastery");
    check(customReward.coreParts >= ta::workshopTowerCost(customReward), "progression reward cannot reach workshop cost fixture");
    const std::uint32_t towerCost = ta::workshopTowerCost(customReward);
    const std::uint32_t partsBeforePurchase = customReward.coreParts;
    check(ta::purchaseTowerCore(customReward) && customReward.towerCoreLevel == 1, "tower workshop purchase failed");
    check(customReward.coreParts == partsBeforePurchase - towerCost, "tower workshop cost accounting failed");
    const std::uint32_t supportCost = ta::workshopSupportCost(customReward, ta::SupportModule::RepairDrones);
    const std::uint32_t supportPartsBefore = customReward.coreParts + supportCost;
    customReward.coreParts = supportPartsBefore;
    check(ta::purchaseSupportModule(customReward, ta::SupportModule::RepairDrones) && customReward.supportModuleLevels[static_cast<std::size_t>(ta::SupportModule::RepairDrones)] == 1 && customReward.coreParts == supportPartsBefore - supportCost, "support workshop purchase failed");
    customReward.legendCores = 5;
    check(ta::unlockUltimateEvolution(customReward, ta::UltimateEvolution::SolarAftermath), "legendary ultimate evolution unlock failed");
    check(ta::equipUltimateEvolution(customReward, ta::UltimateEvolution::SolarAftermath) && customReward.equippedUltimateEvolution == static_cast<std::uint8_t>(ta::UltimateEvolution::SolarAftermath), "legendary ultimate evolution equip failed");
    const std::string legacyProfilePath = temporaryTestPath("tower_ascend_legacy.profile");
    {
        std::ofstream legacy(legacyProfilePath);
        legacy << "TA_PROFILE 1\n" << "best_score 12\n" << "best_wave 2\n" << "runs_completed 1\n" << "total_kills 3\n" << "reduced_flashes 0\n";
    }
    ta::ProfileData migrated;
    const bool legacyLoaded = ta::loadProfile(legacyProfilePath, migrated, &error);
    check(legacyLoaded && migrated.version == 15 && migrated.unlockedSkinsMask == 1u && migrated.coreParts == 0u && migrated.legendCores == 0u && migrated.equippedWeapon == 0 && migrated.equippedUltimate == 0 && migrated.equippedUltimateModule == 255u, "legacy profile migration failed");
    check(!migrated.highContrast && migrated.masterVolume == 100, "legacy profile did not receive safe accessibility defaults");
    const std::string v6ProfilePath = temporaryTestPath("tower_ascend_v6.profile");
    {
        std::ofstream v6(v6ProfilePath);
        v6 << "TA_PROFILE 6\n" << "best_score 80\n" << "best_wave 7\n" << "runs_completed 4\n" << "total_kills 42\n" << "core_parts 135\n" << "legend_cores 2\n" << "daily_claimed 20260812\n" << "tower_core_level 3\n" << "unlocked_skins 1\n";
    }
    ta::ProfileData migratedV6;
    check(ta::loadProfile(v6ProfilePath, migratedV6, &error) && migratedV6.version == 15 && migratedV6.coreParts == 135u && migratedV6.legendCores == 2u && ta::hasClaimedDaily(migratedV6, 20260812u) && migratedV6.ultimateMasteryRuns[0] == 0 && migratedV6.equippedWeapon == 0 && migratedV6.equippedUltimate == 0 && migratedV6.equippedUltimateModule == 255u, "v6 profile did not migrate persistent progression safely");
    const std::string invalidProfilePath = temporaryTestPath("tower_ascend_invalid_binding.profile");
    {
        std::ofstream invalid(invalidProfilePath);
        invalid << "TA_PROFILE 6\nkey_binding_0 0\n";
    }
    check(!ta::loadProfile(invalidProfilePath, migrated, &error), "invalid persisted key binding was accepted");

    ta::GameSim azure(300u), ember(300u);
    azure.setSkin(ta::TowerSkin::Azure); ember.setSkin(ta::TowerSkin::Ember);
    for (int tick = 0; tick < 120; ++tick) { azure.tick(); ember.tick(); }
    check(azure.stateHash() == ember.stateHash(), "cosmetic skin changed deterministic combat state");

    ta::ContentConfig authored;
    check(ta::loadContentConfig(ta::defaultContentDirectory(), authored, &error), "authored content configuration failed to load");
    const std::uint32_t authoredHash = ta::contentFingerprint(authored);
    check(authored.skillCatalogHash != 0 && authored.skillNodes.size() >= 30u, "skill catalog was not loaded with its talent trees");
    check(authored.skillEntityCatalogHash != 0 && authored.maxAlliedUnits == 64u && authored.maxBuildings == 16u && authored.maxSkillZones == 24u, "authored skill entity packs or caps were not loaded");
    for (std::size_t skillIndex = 0; skillIndex < static_cast<std::size_t>(ta::SkillId::Count); ++skillIndex) {
        const ta::SkillId skill = static_cast<ta::SkillId>(skillIndex);
        ta::GameSim skillRun(50000u + static_cast<std::uint32_t>(skillIndex));
        skillRun.setContentConfig(authored);
        ta::SkillLoadout loadout;
        for (std::size_t slot = 0; slot < ta::SkillSlotCount; ++slot) loadout.skills[slot] = static_cast<ta::SkillId>((skillIndex + slot) % static_cast<std::size_t>(ta::SkillId::Count));
        skillRun.setSkillLoadout(loadout);
        skillRun.reset(50000u + static_cast<std::uint32_t>(skillIndex));
        const ta::SkillSnapshot snapshot = skillRun.skillSnapshot(0);
        check(snapshot.skill == skill && !snapshot.iconId.empty() && snapshot.cooldownMaximum > 0 && snapshot.maximumCharges > 0, "skill snapshot omitted authored cooldown/icon data");
        ta::TargetSpec target;
        target.mode = snapshot.targetMode;
        target.world = {700.0f, 360.0f};
        check(skillRun.activateSkill(0, target, &error), "authored skill could not be activated from a deterministic target request");
        check(skillRun.skillSnapshot(0).cooldownRemaining > 0, "skill activation did not start its independent cooldown");
        check(!skillRun.skillVisualEvents().empty() && skillRun.skillVisualEvents().front().skill == skill &&
              skillRun.skillVisualEvents().front().phase == ta::SkillVisualPhase::Cast && skillRun.skillVisualEvents().front().radius > 0.0f,
              "skill cast did not emit a deterministic visual event");
        for (int tick = 0; tick < 45; ++tick) skillRun.tick();
        check(skillRun.stateHash() != 0, "skill state hash was empty after ticking active skill state");
        const bool persistentVisual = skill == ta::SkillId::GravityWell || skill == ta::SkillId::PhaseMine || skill == ta::SkillId::RallyBeacon || skill == ta::SkillId::CryoField || skill == ta::SkillId::ResonancePulse;
        check(persistentVisual ? !skillRun.skillVisualEvents().empty() : skillRun.skillVisualEvents().empty(), "skill visual event lifetime did not match the authored effect");
    }
    ta::SkillLoadout fixtureLoadout;
    fixtureLoadout.skills[0] = ta::SkillId::ResonancePulse;
    fixtureLoadout.skills[1] = ta::SkillId::GravityWell;
    fixtureLoadout.skills[2] = ta::SkillId::PhaseMine;
    fixtureLoadout.skills[3] = ta::SkillId::VanguardDrop;
    fixtureLoadout.skills[4] = ta::SkillId::RuinHex;
    ta::GameSim fixtureRun(50009u);
    fixtureRun.setContentConfig(authored);
    fixtureRun.setSkillLoadout(fixtureLoadout);
    fixtureRun.reset(50009u);
    check(!authored.skillDefinitions[static_cast<std::size_t>(ta::SkillId::ResonancePulse)].operations.empty(), "data-only fixture skill has no authored operations");
    check(fixtureRun.activateSkill(0, ta::TargetSpec{ta::SkillTargetMode::Area, {700.0f, 360.0f}, -1}, &error), "data-only authored skill could not be cast");
    check(fixtureRun.stats().skillTargets[static_cast<std::size_t>(ta::SkillId::ResonancePulse)] > 0 || !fixtureRun.skillZones().empty(), "data-only authored skill did not execute reusable effects");
    check(fixtureRun.activateSkill(1, ta::TargetSpec{ta::SkillTargetMode::Area, {700.0f, 360.0f}, -1}, &error) && !fixtureRun.skillZones().empty() && !fixtureRun.skillZones().back().pullsToEdge,
          "base gravity well did not retain center-pull behavior");
    ta::SkillLoadout edgeGravityLoadout = fixtureLoadout;
    edgeGravityLoadout.nodeBuilds[1] = "gravity_edge_horizon:1";
    fixtureRun.setSkillLoadout(edgeGravityLoadout);
    fixtureRun.reset(50061u);
    check(fixtureRun.activateSkill(1, ta::TargetSpec{ta::SkillTargetMode::Area, {700.0f, 360.0f}, -1}, &error) && !fixtureRun.skillZones().empty() && fixtureRun.skillZones().back().pullsToEdge,
          "edge horizon talent did not switch gravity well to edge-pull behavior");
    ta::GameSim summonRun(50060u);
    summonRun.setContentConfig(authored);
    summonRun.reset(50060u);
    ta::TargetSpec placement{ta::SkillTargetMode::Placement, {700.0f, 360.0f}, -1};
    ta::TargetSpec worldPoint{ta::SkillTargetMode::WorldPoint, {700.0f, 360.0f}, -1};
    check(summonRun.activateSkill(2, worldPoint, &error), "vanguard drop skill failed to create a world-point cast");
    check(!summonRun.alliedUnits().empty(), "vanguard drop did not create allied combat units");
    check(std::any_of(summonRun.skillVisualEvents().begin(), summonRun.skillVisualEvents().end(), [](const ta::SkillVisualEvent& event) { return event.phase == ta::SkillVisualPhase::Spawn && event.skill == ta::SkillId::VanguardDrop; }), "vanguard drop did not emit a spawn visual event");
    check(summonRun.activateSkill(4, placement, &error), "forward barracks skill failed to create a building placement cast");
    check(!summonRun.deployableBuildings().empty(), "forward barracks did not create a deployable building");
    check(std::any_of(summonRun.skillVisualEvents().begin(), summonRun.skillVisualEvents().end(), [](const ta::SkillVisualEvent& event) { return event.phase == ta::SkillVisualPhase::Spawn && event.skill == ta::SkillId::ForwardBarracks; }), "forward barracks did not emit a spawn visual event");
    ta::TargetSpec invalidEnemy{ta::SkillTargetMode::Enemy, {0.0f, 0.0f}, 999999};
    check(!summonRun.activateSkill(3, invalidEnemy, &error) && !error.empty(), "invalid targeted skill cast was accepted");
    ta::ProfileData skillProfile;
    skillProfile.coreParts = 1000;
    check(!ta::isSkillUnlocked(skillProfile, ta::SkillId::DroneSwarm) && ta::unlockSkill(skillProfile, ta::SkillId::DroneSwarm, authored), "skill unlock progression failed");
    const auto firstSkillNode = std::find_if(authored.skillNodes.begin(), authored.skillNodes.end(), [](const ta::SkillNodeDefinition& node) { return node.skillId == "gravity_well" && node.parentId.empty(); });
    check(firstSkillNode != authored.skillNodes.end() && ta::purchaseSkillNode(skillProfile, *firstSkillNode, authored), "generic skill tree node could not be purchased");
    check(ta::equipSkillBuild(skillProfile, 0, firstSkillNode->id + ":1", authored), "purchased skill node could not be equipped into a loadout build");
    const ta::DailyChallenge skillDaily = ta::challengeForDate(20300101u);
    check(!skillDaily.requiredSkills.empty() && !skillDaily.skillSummary.empty(), "daily challenge did not expose a distinct skill requirement");
    ta::ReplayData skillReplay;
    skillReplay.seed = 50070u;
    skillReplay.contentHash = authoredHash;
    ta::ReplayEvent skillEvent;
    skillEvent.tick = 1;
    skillEvent.action = ta::ReplayAction::SkillCast;
    skillEvent.slot = 0;
    skillEvent.skill = ta::SkillId::GravityWell;
    skillEvent.target = {ta::SkillTargetMode::Area, {700.0f, 360.0f}, -1};
    skillEvent.target.direction = {1.0f, 0.25f};
    skillEvent.sequence = 1;
    skillReplay.events.push_back(skillEvent);
    ta::ReplayData decodedSkillReplay;
    check(ta::ReplayData::deserialize(skillReplay.serialize(), decodedSkillReplay) && decodedSkillReplay.events.front().action == ta::ReplayAction::SkillCast && decodedSkillReplay.events.front().sequence == 1, "skill cast replay event did not round-trip");
    check(skillReplay.serialize().find("gravity_well") != std::string::npos, "replay did not persist stable skill IDs");
    std::uint32_t skillReplayHash = 0;
    check(ta::replayFinalHash(skillReplay, 45, skillReplayHash, nullptr, &error), "targeted skill replay did not verify headlessly");
    ta::ReplayData boundReplay = replay;
    boundReplay.contentHash = authoredHash;
    boundReplay.evolution = ta::UltimateEvolution::SolarAftermath;
    ta::ReplayData decodedBound;
    check(ta::ReplayData::deserialize(boundReplay.serialize(), decodedBound, &error) && decodedBound.contentHash == authoredHash && decodedBound.evolution == ta::UltimateEvolution::SolarAftermath, "replay content fingerprint/evolution did not round-trip");
    std::uint32_t boundHash = 0;
    check(ta::replayFinalHash(boundReplay, 120, boundHash, nullptr, &error), "content-bound replay did not verify against authored content");
    ta::ReplayData mismatchedReplay = boundReplay;
    mismatchedReplay.contentHash ^= 0x13579BDFu;
    check(!ta::replayFinalHash(mismatchedReplay, 120, boundHash, nullptr, &error), "replay with a mismatched content fingerprint was accepted");
    ta::ProfileData presetProfile;
    presetProfile.unlockedSkillsMask = (1u << static_cast<unsigned int>(ta::SkillId::Count)) - 1u;
    check(ta::saveSkillPreset(presetProfile, 0) && ta::equipSkillPreset(presetProfile, 0), "skill preset save/equip flow failed");
    ta::GameSim ruleRun(50080u);
    ruleRun.setContentConfig(authored);
    ruleRun.setSkillRules({ta::SkillId::ResonancePulse}, {ta::SkillId::GravityWell}, {"resonance_pulse:amplifier"});
    ta::SkillLoadout validRuleLoadout = fixtureLoadout;
    validRuleLoadout.skills[1] = ta::SkillId::ForwardBarracks;
    ruleRun.setSkillLoadout(validRuleLoadout);
    ruleRun.reset(50080u);
    check(ruleRun.skillLoadoutSatisfiesRules(), "valid daily skill rules were rejected");
    ruleRun.setSkillLoadout(fixtureLoadout);
    check(!ruleRun.skillLoadoutSatisfiesRules() && !ruleRun.activateSkill(1, ta::TargetSpec{ta::SkillTargetMode::Area, {700.0f, 360.0f}, -1}, &error), "forbidden daily skill was cast");
    check(authored.weaponDamage[0] == 18.0f && authored.waveEnemyBudget[9] == 1 && authored.skullScoreMultiplier[4] == 1.50f, "authored content values were not parsed");
    ta::GameSim chassisRun(4444u);
    chassisRun.setChassis(ta::TowerChassis::Bastion);
    chassisRun.reset(4444u);
    check(chassisRun.chassis() == ta::TowerChassis::Bastion && chassisRun.livesRemaining() > 20, "bastion chassis did not apply its durability sidegrade");
    check(std::string(ta::chassisName(ta::TowerChassis::Catalyst)).size() > 0 && std::string(ta::chassisDescription(ta::TowerChassis::Vanguard)).size() > 0, "chassis metadata is incomplete");
    check(authored.skullSpawnScale[1] == 1.50f && authored.skullLives[2] == 10 && authored.skullSpeedScale[3] == 1.25f && authored.skullBossCurrencyBonus[4] == 50, "authored skull gameplay values were not parsed");
    check(authored.ultimateCooldownTicks[0] == 540 && authored.ultimateDamageScale[4] == 1.15f, "authored ultimate tuning was not parsed");
    check(authored.supportModuleCatalogHash != 0 && authored.runExpectedMinutes[0] == 8 && authored.runWaveLimit[1] == 0 && authored.runRewardMultiplier[1] == 0.75f && authored.runWorkshopActive[2] == 0, "authored support/run-type definitions were not parsed");
    check(std::all_of(authored.currencyMetadata.begin(), authored.currencyMetadata.end(), [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.strengths.empty() && !metadata.weaknesses.empty() && !metadata.synergyTags.empty() && !metadata.iconId.empty();
    }), "authored currency descriptions and earning guidance were not loaded");
    check(std::all_of(authored.workshopMetadata.begin(), authored.workshopMetadata.end(), [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.strengths.empty() && !metadata.weaknesses.empty() && !metadata.synergyTags.empty() && !metadata.iconId.empty();
    }), "authored workshop node descriptions and tradeoffs were not loaded");
    check(authored.workshopBaseCost[0] == 80u && authored.workshopCostStep[1] == 35u && authored.workshopMaxLevel[6] == 20u, "authored workshop cost rules were not loaded");
    ta::ProfileData authoredPurchase;
    authoredPurchase.coreParts = ta::workshopTowerCost(authoredPurchase, authored);
    check(ta::purchaseTowerCore(authoredPurchase, authored) && authoredPurchase.towerCoreLevel == 1, "content-driven tower workshop purchase failed");
    check(std::all_of(authored.runTypeMetadata.begin(), authored.runTypeMetadata.end(), [](const ta::RunTypeMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.description.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.rules.empty() && !metadata.iconId.empty();
    }), "authored run-type descriptions and rules were not loaded");
    check(std::all_of(authored.evolutionMetadata.begin(), authored.evolutionMetadata.end(), [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.synergyTags.empty() && !metadata.iconId.empty();
    }), "authored ultimate evolution descriptions and synergy tags were not loaded");
    check(std::all_of(authored.ultimateModuleMetadata.begin(), authored.ultimateModuleMetadata.end(), [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.synergyTags.empty() && !metadata.iconId.empty();
    }) && authored.ultimateModuleCost[0] > 0u && authored.ultimateModuleCooldownScale[0] < 1.0f && authored.ultimateModuleDamageScale[1] > 1.0f, "authored ultimate sidegrades were not loaded");
    ta::ProfileData sidegradeProfile;
    sidegradeProfile.coreParts = authored.ultimateModuleCost[0];
    sidegradeProfile.ultimateMasteryRuns[0] = 3u;
    check(ta::unlockUltimateModule(sidegradeProfile, ta::UltimateModule::MeteorQuickCharge, authored) && ta::equipUltimateModule(sidegradeProfile, ta::UltimateModule::MeteorQuickCharge) && sidegradeProfile.equippedUltimateModule == static_cast<std::uint8_t>(ta::UltimateModule::MeteorQuickCharge), "ultimate sidegrade unlock/equip failed");
    ta::GameSim baseUltimate(811u), quickUltimate(811u);
    baseUltimate.setContentConfig(authored); quickUltimate.setContentConfig(authored);
    baseUltimate.setUltimate(ta::Ultimate::MeteorRain); quickUltimate.setUltimate(ta::Ultimate::MeteorRain); quickUltimate.setUltimateModule(ta::UltimateModule::MeteorQuickCharge);
    baseUltimate.reset(811u); quickUltimate.reset(811u);
    baseUltimate.activateUltimate(); quickUltimate.activateUltimate(); baseUltimate.tick(); quickUltimate.tick();
    check(quickUltimate.ultimateRatio() > baseUltimate.ultimateRatio() && quickUltimate.stateHash() != baseUltimate.stateHash(), "ultimate sidegrade did not alter deterministic cooldown behavior");
    check(std::all_of(authored.ultimateEvolutionCost.begin(), authored.ultimateEvolutionCost.end(), [](std::uint32_t cost) { return cost > 0u; }), "authored ultimate evolution costs were not loaded");
    ta::ContentConfig customEvolutionCost = authored;
    customEvolutionCost.ultimateEvolutionCost[0] = 7u;
    ta::ProfileData customEvolutionPurchase;
    customEvolutionPurchase.legendCores = 7u;
    customEvolutionPurchase.ultimateMasteryRuns[0] = 3u;
    check(ta::unlockUltimateEvolution(customEvolutionPurchase, ta::UltimateEvolution::SolarAftermath, customEvolutionCost) && customEvolutionPurchase.legendCores == 0u, "content-driven ultimate evolution cost was not applied");
    check(std::all_of(authored.synergyMetadata.begin(), authored.synergyMetadata.end(), [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.synergyTags.empty() && !metadata.iconId.empty();
    }), "authored synergy descriptions were not loaded");
    const auto metadataComplete = [](const ta::ContentMetadata& metadata) {
        return !metadata.id.empty() && !metadata.display.empty() && !metadata.shortDescription.empty() && !metadata.longDescription.empty() && !metadata.iconId.empty();
    };
    check(std::all_of(authored.weaponMetadata.begin(), authored.weaponMetadata.end(), metadataComplete) &&
          std::all_of(authored.upgradeMetadata.begin(), authored.upgradeMetadata.end(), metadataComplete) &&
          std::all_of(authored.ultimateMetadata.begin(), authored.ultimateMetadata.end(), metadataComplete) &&
          std::all_of(authored.supportMetadata.begin(), authored.supportMetadata.end(), metadataComplete) &&
          std::all_of(authored.skullMetadata.begin(), authored.skullMetadata.end(), metadataComplete) &&
          std::all_of(authored.arenaMetadata.begin(), authored.arenaMetadata.end(), metadataComplete) &&
          std::all_of(authored.enemyMetadata.begin(), authored.enemyMetadata.end(), metadataComplete), "runtime content metadata was not loaded for every selectable catalog item");
    ta::ContentConfig fastUltimateContent = authored;
    fastUltimateContent.ultimateCooldownTicks[0] = 30;
    ta::GameSim fastUltimate(2468u);
    fastUltimate.setContentConfig(fastUltimateContent);
    fastUltimate.setUltimate(ta::Ultimate::MeteorRain);
    fastUltimate.activateUltimate();
    for (int tick = 0; tick < 30; ++tick) fastUltimate.tick();
    fastUltimate.activateUltimate();
    check(fastUltimate.stats().ultimates == 2, "authored ultimate cooldown was not applied");
    check(authored.waveEnemyTypeWeight[1][0] == 80.0f && authored.waveEnemyTypeWeight[1][1] == 20.0f && authored.waveEnemyTypeWeight[8][5] == 13.0f, "authored wave enemy composition was not parsed");
    check(authored.arenaHealthScale[1] == 1.15f && authored.arenaSpeedScale[2] == 1.12f && authored.arenaPathAmplitude[0] == 120.0f, "authored arena values were not parsed");
    check(authored.enemyHealthScale[1] == 0.60f && authored.enemyHealthScale[6] == 18.0f && authored.enemySpeedScale[4] == 1.35f, "authored enemy health/speed values were not parsed");
    check(authored.enemyDamageResistance[3] == 0.45f && authored.enemyRadius[2] == 21.0f && authored.enemyTeleportCooldown[5] == 2.5f && authored.bossAttackCooldownTicks == 450 && authored.bossTelegraphTicks == 15 && authored.bossAttackLives == 2, "authored enemy behavior values were not parsed");
    ta::ContentConfig tunedEnemies = authored;
    tunedEnemies.enemyHealthScale[0] = 2.0f;
    tunedEnemies.enemySpeedScale[0] = 0.5f;
    tunedEnemies.enemyRadius[0] = 19.0f;
    ta::GameSim baselineEnemy(4321u), tunedEnemy(4321u);
    tunedEnemy.setContentConfig(tunedEnemies);
    baselineEnemy.tick(); tunedEnemy.tick();
    check(!baselineEnemy.enemies().empty() && !tunedEnemy.enemies().empty(), "enemy tuning fixture did not spawn a grunt");
    if (!baselineEnemy.enemies().empty() && !tunedEnemy.enemies().empty()) {
        check(tunedEnemy.enemies().front().maxHp == baselineEnemy.enemies().front().maxHp * 2.0f, "authored enemy health was not applied by the simulation");
        check(tunedEnemy.enemies().front().speed == baselineEnemy.enemies().front().speed * 0.5f && tunedEnemy.enemies().front().radius == 19.0f, "authored enemy speed/radius was not applied by the simulation");
    }
    ta::ContentConfig runnerWave = authored;
    runnerWave.waveEnemyTypeWeight[0].fill(0.0f);
    runnerWave.waveEnemyTypeWeight[0][static_cast<std::size_t>(ta::EnemyType::Runner)] = 100.0f;
    ta::GameSim authoredWave(9876u);
    authoredWave.setContentConfig(runnerWave);
    authoredWave.tick();
    check(!authoredWave.enemies().empty() && authoredWave.enemies().front().type == ta::EnemyType::Runner, "authored wave composition did not control the spawned archetype");
    check(authored.upgradeWeight[9] == 0.75f && authored.upgradeWeight[14] == 0.8f, "authored upgrade weights were not parsed");
        check(authored.upgradeValueA[0] == 2.0f && authored.upgradeValueA[9] == 85.0f && authored.upgradeValueA[14] == 0.06f && authored.upgradeValueB[14] == 0.0f, "authored upgrade magnitudes were not parsed");
    check(authored.upgradeMetadata[1].prerequisites == std::vector<std::string>{"piercing_shots"} &&
          authored.upgradeMetadata[4].prerequisites == std::vector<std::string>{"cluster_bombs"} &&
          authored.upgradeMetadata[12].prerequisites == std::vector<std::string>{"fireball_shells"} &&
          authored.upgradeMetadata[14].prerequisites.empty(), "authored upgrade prerequisites were not parsed");
    check(authored.upgradeMetadata[10].exclusions == std::vector<std::string>{"scavenger"} &&
          authored.upgradeMetadata[11].exclusions == std::vector<std::string>{"emergency_repair"} &&
          authored.upgradeMetadata[0].maxStacks == 1, "authored upgrade exclusions or stack limits were not parsed");
    ta::ContentConfig weightedContent = authored;
    weightedContent.upgradeWeight.fill(0.0001f);
    weightedContent.upgradeWeight[0] = 100.0f;
    ta::GameSim weightedDraft(991u);
    weightedDraft.setContentConfig(weightedContent);
    int weightedSafety = 0;
    while (!weightedDraft.upgradePending() && weightedSafety++ < 5000) weightedDraft.tick();
    check(weightedDraft.upgradePending() && std::find(weightedDraft.pendingChoices().begin(), weightedDraft.pendingChoices().end(), ta::Upgrade::PiercingShots) != weightedDraft.pendingChoices().end(), "upgrade weights did not influence the authored draft");
    ta::ContentConfig gatedContent = authored;
    gatedContent.upgradeWeight.fill(0.0001f);
    gatedContent.upgradeWeight[1] = 100.0f;
    ta::GameSim gatedDraft(992u);
    gatedDraft.setContentConfig(gatedContent);
    int gatedSafety = 0;
    while (!gatedDraft.upgradePending() && gatedSafety++ < 5000) gatedDraft.tick();
    check(gatedDraft.upgradePending() && std::find(gatedDraft.pendingChoices().begin(), gatedDraft.pendingChoices().end(), ta::Upgrade::Ricochet) == gatedDraft.pendingChoices().end(), "upgrade prerequisite was ignored during drafting");
    if (gatedDraft.upgradePending()) {
        const auto piercing = std::find(gatedDraft.pendingChoices().begin(), gatedDraft.pendingChoices().end(), ta::Upgrade::PiercingShots);
        if (piercing != gatedDraft.pendingChoices().end()) gatedDraft.chooseUpgrade(static_cast<int>(piercing - gatedDraft.pendingChoices().begin()));
    }
    ta::GameSim rapidDraft(991u), frostDraft(991u);
    rapidDraft.setWeapon(ta::Weapon::RapidFire); frostDraft.setWeapon(ta::Weapon::FrostBlaster);
    rapidDraft.reset(991u); frostDraft.reset(991u);
    while (!rapidDraft.upgradePending()) rapidDraft.tick();
    while (!frostDraft.upgradePending()) frostDraft.tick();
    check(rapidDraft.pendingChoices() != frostDraft.pendingChoices(), "weapon affinity did not diversify upgrade drafts");

    const ta::DailyChallenge dailyA = ta::currentDailyChallenge();
    const ta::DailyChallenge dailyB = ta::currentDailyChallenge();
    check(dailyA.dateKey != 0 && dailyA.seed != 0 && dailyA.dateKey == dailyB.dateKey && dailyA.seed == dailyB.seed, "daily challenge was not stable for the current UTC date");
    check(static_cast<int>(dailyA.recommendedWeapon) >= 0 && static_cast<int>(dailyA.recommendedWeapon) <= 4 && static_cast<int>(dailyA.skull) >= 1 && static_cast<int>(dailyA.skull) <= 4, "daily challenge selected invalid modifiers");
    check(static_cast<int>(dailyA.arena) >= 0 && static_cast<int>(dailyA.arena) <= 2 && dailyA.arena == dailyB.arena, "daily challenge selected an invalid or unstable arena");
    check(std::string(ta::weaponName(dailyA.recommendedWeapon)).size() > 0 && std::string(ta::skullName(dailyA.skull)).size() > 0 && std::string(ta::arenaName(dailyA.arena)).size() > 0, "daily challenge preview values have no display names");
    check(dailyA.bonusShards > 0 && dailyA.legendCoreReward > 0 && !dailyA.title.empty() && !dailyA.description.empty() && !dailyA.longDescription.empty() && !dailyA.themeTags.empty() && !dailyA.loadoutRule.empty() && !dailyA.skullSummary.empty() && !dailyA.enemySummary.empty() && !dailyA.enemyRoster.empty() && !dailyA.threatSummary.empty() && !dailyA.recommendedUpgradeTags.empty() && !dailyA.modifierSummary.empty() && !dailyA.modifierDescription.empty() && !dailyA.objective.empty() && dailyA.waveBudgetScale > 0.0f && dailyA.enemyHealthScale > 0.0f && dailyA.enemySpeedScale > 0.0f && static_cast<int>(dailyA.requiredUltimate) >= 0, "daily challenge briefing is incomplete");
    bool foundRequiredDailyWeapon = false;
    for (std::uint32_t date = 20300101; date < 20300108; ++date) {
        const ta::DailyChallenge challenge = ta::challengeForDate(date);
        if (challenge.weaponRequired) {
            foundRequiredDailyWeapon = true;
            ta::GameSim dailyLoadout(7000u + date);
            dailyLoadout.setWeapon(static_cast<ta::Weapon>((static_cast<int>(challenge.requiredWeapon) + 1) % 5));
            dailyLoadout.setContentConfig(ta::contentForDailyChallenge(authored, challenge));
            dailyLoadout.setWeapon(challenge.requiredWeapon);
            check(dailyLoadout.weapon() == challenge.requiredWeapon, "daily required weapon could not be applied to the deterministic setup");
        }
    }
    check(foundRequiredDailyWeapon, "daily rotation did not expose any required weapon setup");
    std::array<ta::DailyChallenge, 7> rotation{};
    for (std::uint32_t date = 20300101; date < 20300108; ++date) {
        const ta::DailyChallenge challengeA = ta::challengeForDate(date);
        const ta::DailyChallenge challengeB = ta::challengeForDate(date);
        rotation[static_cast<std::size_t>(date - 20300101u)] = challengeA;
        check(challengeA.seed == challengeB.seed && challengeA.recommendedWeapon == challengeB.recommendedWeapon && challengeA.skull == challengeB.skull && challengeA.arena == challengeB.arena, "daily challenge was not deterministic for an explicit date");
        check(challengeA.dateKey == date && challengeA.bonusShards > 0, "dated daily challenge lost its date or reward");
        check(static_cast<int>(challengeA.requiredUltimate) >= 0 && static_cast<int>(challengeA.requiredUltimate) <= static_cast<int>(ta::Ultimate::EnergySurge), "daily challenge selected an invalid required ultimate");
        check(challengeA.requiredEvolution == ta::UltimateEvolution::None || (static_cast<int>(challengeA.requiredEvolution) - 1) / 3 == static_cast<int>(challengeA.requiredUltimate), "daily evolution does not match required ultimate");
        check(static_cast<int>(challengeA.requiredSupport) >= 0 && static_cast<int>(challengeA.requiredSupport) <= static_cast<int>(ta::SupportModule::CorrosionAmp), "daily challenge selected an invalid required support module");
        check(!challengeA.enemyRoster.empty() && challengeA.enemyRoster.size() == challengeA.enemyPrevalence.size() && !challengeA.threatSummary.empty() && challengeA.waveBudgetScale > 0.0f, "daily challenge has no complete authored threat profile");
    }
    for (std::size_t index = 1; index < rotation.size(); ++index) {
        check(rotation[index].title != rotation[index - 1].title && (rotation[index].recommendedWeapon != rotation[index - 1].recommendedWeapon || rotation[index].skull != rotation[index - 1].skull || rotation[index].arena != rotation[index - 1].arena), "daily rotation repeated without a strategic change");
        check((rotation[index].skullMask & (static_cast<ta::SkullMask>(1u) << static_cast<unsigned int>(rotation[index].skull))) != 0u, "daily recipe skull was not active in its skull mask");
    }
    const ta::DailyChallenge themedA = ta::challengeForDate(20260801u);
    const ta::DailyChallenge themedB = ta::challengeForDate(20260802u);
    check(themedA.title != themedB.title || themedA.description != themedB.description || themedA.objective != themedB.objective, "adjacent daily challenges were not strategically distinct");
    const ta::ContentConfig themedContent = ta::contentForDailyChallenge(authored, themedA);
    check(themedContent.waveEnemyBudget[0] != authored.waveEnemyBudget[0] || themedContent.waveEnemyTypeWeight[0] != authored.waveEnemyTypeWeight[0] || themedContent.enemyHealthScale != authored.enemyHealthScale, "daily threat profile did not alter the simulation content");
    ta::GameSim dailyProfileRun(8181u);
    dailyProfileRun.setContentConfig(themedContent);
    dailyProfileRun.setWeapon(themedA.recommendedWeapon);
    dailyProfileRun.setUltimate(themedA.requiredUltimate);
    dailyProfileRun.setSupport(themedA.requiredSupport);
    dailyProfileRun.setSkullMask(themedA.skullMask);
    dailyProfileRun.setArena(themedA.arena);
    dailyProfileRun.reset(themedA.seed);
    for (int tick = 0; tick < 150; ++tick) dailyProfileRun.tick();
    check(dailyProfileRun.enemiesSpawnedThisWave() > 0, "daily threat profile did not spawn a wave");
    ta::ReplayData dailyReplay;
    dailyReplay.seed = themedA.seed;
    dailyReplay.weapon = themedA.recommendedWeapon;
    dailyReplay.support = themedA.requiredSupport;
    dailyReplay.skullMask = themedA.skullMask;
    dailyReplay.ultimate = themedA.requiredUltimate;
    dailyReplay.evolution = themedA.requiredEvolution;
    dailyReplay.arena = themedA.arena;
    dailyReplay.contentHash = ta::contentFingerprint(authored);
    dailyReplay.dailyDateKey = themedA.dateKey;
    ta::ReplayData decodedDaily;
    check(ta::ReplayData::deserialize(dailyReplay.serialize(), decodedDaily, &error) && decodedDaily.dailyDateKey == themedA.dateKey, "daily replay date identity did not round-trip");
    std::uint32_t dailyReplayHash = 0;
    check(ta::replayFinalHash(dailyReplay, 150, dailyReplayHash, nullptr, &error), "daily replay profile did not verify");
    std::set<std::string> dailyTitles;
    int distinctDailyWeapons = 0;
    std::set<int> dailyWeapons;
    for (std::uint32_t date = 20300101u; date <= 20300131u; ++date) {
        const ta::DailyChallenge challenge = ta::challengeForDate(date);
        dailyTitles.insert(challenge.title);
        dailyWeapons.insert(static_cast<int>(challenge.weaponRequired ? challenge.requiredWeapon : challenge.recommendedWeapon));
        check(!challenge.enemyRoster.empty() && !challenge.objective.empty() && challenge.waveBudgetScale > 0.0f && challenge.enemyHealthScale > 0.0f && challenge.enemySpeedScale > 0.0f, "daily rotation produced an incomplete recipe");
        check(challenge.chassisRequired, "daily rotation did not require an authored tower chassis");
        if (challenge.weaponRequired) check(challenge.recommendedWeapon == challenge.requiredWeapon, "daily required weapon was not reflected in the recommended setup");
        if (challenge.requiredEvolution != ta::UltimateEvolution::None) check((static_cast<int>(challenge.requiredEvolution) - 1) / 3 == static_cast<int>(challenge.requiredUltimate), "daily required evolution does not belong to the required ultimate");
    }
    distinctDailyWeapons = static_cast<int>(dailyWeapons.size());
    check(dailyTitles.size() >= 6 && distinctDailyWeapons >= 4, "long daily rotation did not provide meaningful setup variation");
    ta::DailyChallenge previousDaily;
    bool hasPreviousDaily = false;
    for (std::uint32_t date = 20300301u; date <= 20300331u; ++date) {
        const ta::DailyChallenge challenge = ta::challengeForDate(date);
        if (hasPreviousDaily) check(challenge.title != previousDaily.title && (challenge.recommendedWeapon != previousDaily.recommendedWeapon || challenge.skull != previousDaily.skull || challenge.arena != previousDaily.arena), "extended daily rotation repeated its strategic setup");
        previousDaily = challenge;
        hasPreviousDaily = true;
    }
    std::set<int> dailyChassis;
    for (std::uint32_t date = 20260801u; date < 20260808u; ++date) dailyChassis.insert(static_cast<int>(ta::challengeForDate(date).requiredChassis));
    check(dailyChassis.size() >= 2, "daily rotation did not vary the required tower chassis");

    for (int weapon = 0; weapon < 5; ++weapon) {
        ta::GameSim weaponRun(1000u + static_cast<std::uint32_t>(weapon));
        weaponRun.setWeapon(static_cast<ta::Weapon>(weapon));
        weaponRun.reset(1000u + static_cast<std::uint32_t>(weapon));
        advance(weaponRun, 1800, true);
        check(weaponRun.stats().kills > 0, "a weapon failed to damage and defeat enemies");
    }

    for (int arena = 0; arena < 3; ++arena) {
        ta::GameSim mapRun(7000u + static_cast<std::uint32_t>(arena));
        mapRun.setArena(static_cast<ta::Arena>(arena));
        mapRun.reset(7000u + static_cast<std::uint32_t>(arena));
        advance(mapRun, 100000, true);
        check(mapRun.stats().kills > 0 && (mapRun.isVictory() || mapRun.isGameOver()), "arena run did not reach a terminal state");
        check(std::string(ta::arenaName(static_cast<ta::Arena>(arena))).size() > 0, "arena has no display name");
    }
    for (int arena = 0; arena < 3; ++arena) for (int weapon = 0; weapon < 5; ++weapon) {
        const std::uint32_t seed = 12000u + static_cast<std::uint32_t>(arena * 10 + weapon);
        ta::GameSim matrixRun(seed);
        matrixRun.setArena(static_cast<ta::Arena>(arena));
        matrixRun.setWeapon(static_cast<ta::Weapon>(weapon));
        matrixRun.reset(seed);
        advance(matrixRun, 100000, true);
        check(matrixRun.isVictory() || matrixRun.isGameOver(), "arena/weapon balance run did not reach a terminal state");
    }

    if (failures != 0) std::cerr << failures << " simulation checks failed\n";
    else std::cout << "Tower Ascend simulation checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
