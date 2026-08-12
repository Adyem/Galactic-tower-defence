#include "game.hpp"
#include "profile.hpp"
#include "daily.hpp"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    check(run.stats().ultimates > 0, "ultimate was never activated");
    check(run.stats().shotsFired > 0, "tower never produced a firing event");

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

    ta::GameSim choices(5u);
    choices.reset(5u);
    int safety = 0;
    while (!choices.upgradePending() && safety++ < 5000) choices.tick();
    check(choices.upgradePending(), "first upgrade draft was not reached");
    check(choices.pendingChoices().size() == 3, "upgrade draft does not contain three choices");
    choices.chooseUpgrade(1);
    check(choices.stats().upgrades == 1 && !choices.upgradePending(), "upgrade selection did not apply");
    for (int upgrade = 0; upgrade < 15; ++upgrade) check(std::string(ta::upgradeDescription(static_cast<ta::Upgrade>(upgrade))).size() > 0, "upgrade has no player-facing effect description");

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
    replay.skull = ta::Skull::Greed;
    replay.skullMask = static_cast<ta::SkullMask>((1u << static_cast<unsigned int>(ta::Skull::Greed)) | (1u << static_cast<unsigned int>(ta::Skull::Swarm)));
    replay.ultimate = ta::Ultimate::GravityShift;
    replay.autoUltimate = true;
    replay.arena = ta::Arena::NeonRuins;
    replay.events = {{30, ta::ReplayAction::Ultimate, 0}, {61, ta::ReplayAction::Upgrade, 2}};
    ta::ReplayData decoded;
    std::string error;
    check(ta::ReplayData::deserialize(replay.serialize(), decoded, &error), "replay serialization failed");
    check(decoded.seed == replay.seed && decoded.weapon == replay.weapon && decoded.skull == replay.skull && decoded.skullMask == replay.skullMask && decoded.ultimate == replay.ultimate && decoded.autoUltimate == replay.autoUltimate && decoded.arena == replay.arena && decoded.events.size() == 2, "replay round trip changed data");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nweapon invalid\n", decoded, &error), "malformed replay was accepted");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nevent 30 1 0\nevent 20 1 0\n", decoded, &error), "out-of-order replay events were accepted");
    check(!ta::ReplayData::deserialize("TA_REPLAY 1\nevent 30 1 3\n", decoded, &error), "invalid replay upgrade choice was accepted");
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
    check(autoUltimate.stats().ultimates > 0, "automatic ultimate mode never triggered");

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
    profile.inputBindings.key(ta::InputAction::Ultimate) = 'e';
    const std::string profilePath = temporaryTestPath("tower_ascend_test.profile");
    ta::ProfileData loadedProfile;
    check(ta::saveProfile(profilePath, profile, &error) && ta::loadProfile(profilePath, loadedProfile, &error), "profile persistence failed");
    check(loadedProfile.bestScore == profile.bestScore && loadedProfile.reducedFlashes == profile.reducedFlashes && loadedProfile.highContrast == profile.highContrast && loadedProfile.masterVolume == profile.masterVolume && loadedProfile.musicVolume == profile.musicVolume && loadedProfile.sfxVolume == profile.sfxVolume && loadedProfile.uiVolume == profile.uiVolume && loadedProfile.uiScalePercent == profile.uiScalePercent && loadedProfile.colorBlindPalette == profile.colorBlindPalette && loadedProfile.subtitles == profile.subtitles && loadedProfile.vibration == profile.vibration, "profile round trip changed data");
    check(loadedProfile.version == 6 && loadedProfile.inputBindings.key(ta::InputAction::Ultimate) == 'e', "profile input bindings did not round trip");
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
    const std::string legacyProfilePath = temporaryTestPath("tower_ascend_legacy.profile");
    {
        std::ofstream legacy(legacyProfilePath);
        legacy << "TA_PROFILE 1\n" << "best_score 12\n" << "best_wave 2\n" << "runs_completed 1\n" << "total_kills 3\n" << "reduced_flashes 0\n";
    }
    ta::ProfileData migrated;
    const bool legacyLoaded = ta::loadProfile(legacyProfilePath, migrated, &error);
    check(legacyLoaded && migrated.version == 1 && migrated.unlockedSkinsMask == 1u, "legacy profile migration failed");
    check(!migrated.highContrast && migrated.masterVolume == 100, "legacy profile did not receive safe accessibility defaults");
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
    ta::ReplayData boundReplay = replay;
    boundReplay.contentHash = authoredHash;
    ta::ReplayData decodedBound;
    check(ta::ReplayData::deserialize(boundReplay.serialize(), decodedBound, &error) && decodedBound.contentHash == authoredHash, "replay content fingerprint did not round-trip");
    std::uint32_t boundHash = 0;
    check(ta::replayFinalHash(boundReplay, 120, boundHash, nullptr, &error), "content-bound replay did not verify against authored content");
    ta::ReplayData mismatchedReplay = boundReplay;
    mismatchedReplay.contentHash ^= 0x13579BDFu;
    check(!ta::replayFinalHash(mismatchedReplay, 120, boundHash, nullptr, &error), "replay with a mismatched content fingerprint was accepted");
    check(authored.weaponDamage[0] == 18.0f && authored.waveEnemyBudget[9] == 1 && authored.skullScoreMultiplier[4] == 1.50f, "authored content values were not parsed");
    check(authored.skullSpawnScale[1] == 1.50f && authored.skullLives[2] == 10 && authored.skullSpeedScale[3] == 1.25f && authored.skullBossCurrencyBonus[4] == 50, "authored skull gameplay values were not parsed");
    check(authored.ultimateCooldownTicks[0] == 540 && authored.ultimateDamageScale[4] == 1.15f, "authored ultimate tuning was not parsed");
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
    check(authored.upgradeWeight[9] == 0.75f && authored.upgradeWeight[14] == 0.75f, "authored upgrade weights were not parsed");
    check(authored.upgradeValueA[0] == 2.0f && authored.upgradeValueA[9] == 85.0f && authored.upgradeValueB[14] == 18.0f, "authored upgrade magnitudes were not parsed");
    ta::ContentConfig weightedContent = authored;
    weightedContent.upgradeWeight.fill(0.0001f);
    weightedContent.upgradeWeight[0] = 100.0f;
    ta::GameSim weightedDraft(991u);
    weightedDraft.setContentConfig(weightedContent);
    int weightedSafety = 0;
    while (!weightedDraft.upgradePending() && weightedSafety++ < 5000) weightedDraft.tick();
    check(weightedDraft.upgradePending() && std::find(weightedDraft.pendingChoices().begin(), weightedDraft.pendingChoices().end(), ta::Upgrade::PiercingShots) != weightedDraft.pendingChoices().end(), "upgrade weights did not influence the authored draft");
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
    check(dailyA.bonusShards > 0, "daily challenge has no bonus reward");
    for (std::uint32_t date = 20300101; date < 20300108; ++date) {
        const ta::DailyChallenge challengeA = ta::challengeForDate(date);
        const ta::DailyChallenge challengeB = ta::challengeForDate(date);
        check(challengeA.seed == challengeB.seed && challengeA.recommendedWeapon == challengeB.recommendedWeapon && challengeA.skull == challengeB.skull && challengeA.arena == challengeB.arena, "daily challenge was not deterministic for an explicit date");
        check(challengeA.dateKey == date && challengeA.bonusShards > 0, "dated daily challenge lost its date or reward");
    }

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
