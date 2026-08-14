#include "game.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
enum class Policy { First, Random, TagMatch, Synergy, ThreatAware, Oracle };
enum class SkillPolicy { NoSkill, RandomCast, RoleAwareCast, OracleCast };

const char* policyName(Policy policy) {
    switch (policy) {
        case Policy::First: return "first";
        case Policy::Random: return "random";
        case Policy::TagMatch: return "tag_match";
        case Policy::Synergy: return "synergy";
        case Policy::ThreatAware: return "threat_aware";
        case Policy::Oracle: return "oracle";
    }
    return "unknown";
}

bool isCoreFor(ta::Upgrade upgrade, ta::Weapon weapon) {
    switch (weapon) {
        case ta::Weapon::RapidFire:
        case ta::Weapon::SniperRailgun:
            return upgrade == ta::Upgrade::PiercingShots || upgrade == ta::Upgrade::Ricochet || upgrade == ta::Upgrade::Overclock;
        case ta::Weapon::ExplosiveCannon:
            return upgrade == ta::Upgrade::ClusterBombs || upgrade == ta::Upgrade::Shockwave || upgrade == ta::Upgrade::FireballShells || upgrade == ta::Upgrade::WindShear;
        case ta::Weapon::ArcaneBeam:
            return upgrade == ta::Upgrade::ChainLightning || upgrade == ta::Upgrade::BurningShot || upgrade == ta::Upgrade::WindShear || upgrade == ta::Upgrade::PoisonCoil;
        case ta::Weapon::FrostBlaster:
            return upgrade == ta::Upgrade::FreezingBlast || upgrade == ta::Upgrade::ChainLightning || upgrade == ta::Upgrade::Shockwave || upgrade == ta::Upgrade::BlackHole;
    }
    return false;
}

bool owns(const ta::GameSim& sim, ta::Upgrade upgrade) {
    const auto& upgrades = sim.upgrades();
    return std::find(upgrades.begin(), upgrades.end(), upgrade) != upgrades.end();
}

int choiceScore(const ta::GameSim& sim, ta::Upgrade upgrade, Policy policy, int index) {
    if (policy == Policy::First) return 1000 - index;
    if (policy == Policy::Random) return ((sim.stats().ticks / ta::GameSim::TickRate) * 17 + index * 31) % 100;
    int score = isCoreFor(upgrade, sim.weapon()) ? 40 : 0;
    if (policy == Policy::TagMatch) return score * 10 - index;
    if (owns(sim, ta::Upgrade::BurningShot) && upgrade == ta::Upgrade::WindShear) score += 70;
    if (owns(sim, ta::Upgrade::WindShear) && upgrade == ta::Upgrade::FireballShells) score += 70;
    if (owns(sim, ta::Upgrade::FreezingBlast) && upgrade == ta::Upgrade::ChainLightning) score += 75;
    if (owns(sim, ta::Upgrade::PiercingShots) && upgrade == ta::Upgrade::Ricochet) score += 35;
    if (policy == Policy::ThreatAware || policy == Policy::Oracle) {
        const auto& threat = sim.contentConfig().waveEnemyTypeWeight[static_cast<std::size_t>(std::clamp(sim.waveNumber() - 1, 0, 9))];
        const float swarm = threat[static_cast<std::size_t>(ta::EnemyType::Swarmling)];
        const float fast = threat[static_cast<std::size_t>(ta::EnemyType::Runner)] + threat[static_cast<std::size_t>(ta::EnemyType::Teleporter)];
        const float armor = threat[static_cast<std::size_t>(ta::EnemyType::Tank)] + threat[static_cast<std::size_t>(ta::EnemyType::Shielded)];
        if ((upgrade == ta::Upgrade::ClusterBombs || upgrade == ta::Upgrade::ChainLightning || upgrade == ta::Upgrade::BlackHole) && swarm >= 10.0f) score += 65;
        if ((upgrade == ta::Upgrade::FreezingBlast || upgrade == ta::Upgrade::Shockwave) && fast >= 20.0f) score += 65;
        if ((upgrade == ta::Upgrade::PiercingShots || upgrade == ta::Upgrade::BurningShot || upgrade == ta::Upgrade::PoisonCoil) && armor >= 20.0f) score += 65;
    }
    if (policy == Policy::Oracle) {
        score += isCoreFor(upgrade, sim.weapon()) ? 90 : 0;
        score += owns(sim, ta::Upgrade::ClusterBombs) && upgrade == ta::Upgrade::Shockwave ? 90 : 0;
        score += owns(sim, ta::Upgrade::FireballShells) && upgrade == ta::Upgrade::WindShear ? 90 : 0;
        score += owns(sim, ta::Upgrade::FreezingBlast) && upgrade == ta::Upgrade::ChainLightning ? 90 : 0;
    }
    return score * 10 - index;
}

SkillPolicy skillPolicyFor(Policy policy) {
    switch (policy) {
        case Policy::First: return SkillPolicy::NoSkill;
        case Policy::Random: return SkillPolicy::RandomCast;
        case Policy::TagMatch:
        case Policy::Synergy: return SkillPolicy::RoleAwareCast;
        case Policy::ThreatAware:
        case Policy::Oracle: return SkillPolicy::OracleCast;
    }
    return SkillPolicy::NoSkill;
}

bool castReadySkills(ta::GameSim& sim, SkillPolicy policy, int tick) {
    if (policy == SkillPolicy::NoSkill) return false;
    const int decision = tick / 15;
    if (policy == SkillPolicy::RandomCast && decision % 17 != 0) return false;
    bool cast = false;
    const std::size_t preferredSlot = policy == SkillPolicy::RandomCast
        ? static_cast<std::size_t>((decision * 29 + 11) % static_cast<int>(ta::SkillSlotCount))
        : static_cast<std::size_t>(decision % static_cast<int>(ta::SkillSlotCount));
    for (std::size_t offset = 0; offset < ta::SkillSlotCount; ++offset) {
        const std::size_t slot = (preferredSlot + offset) % ta::SkillSlotCount;
        const ta::SkillSnapshot snapshot = sim.skillSnapshot(slot);
        if (snapshot.cooldownRemaining > 0 || snapshot.charges <= 0) continue;
        if (policy == SkillPolicy::RandomCast && ((tick / 15 + static_cast<int>(slot) * 7) % 11) != 0) continue;
        if (policy == SkillPolicy::RoleAwareCast && sim.enemiesRemaining() < (slot % 2 == 0 ? 3 : 1) && sim.alliedUnits().empty()) continue;
        ta::TargetSpec target;
        target.mode = snapshot.targetMode;
        if (snapshot.targetMode == ta::SkillTargetMode::Enemy) {
            for (const ta::Enemy& enemy : sim.enemies()) if (enemy.alive) { target.entityId = enemy.id; break; }
        } else if (snapshot.targetMode == ta::SkillTargetMode::Ally) {
            for (const ta::AlliedUnit& ally : sim.alliedUnits()) if (ally.alive) { target.entityId = ally.id; break; }
        } else if (snapshot.targetMode != ta::SkillTargetMode::None) {
            if (policy == SkillPolicy::RandomCast) {
                target.world = {450.0f + static_cast<float>((decision * 97 + static_cast<int>(slot) * 53) % 680), 80.0f + static_cast<float>((decision * 43 + static_cast<int>(slot) * 31) % 560)};
            } else target.world = {700.0f, 360.0f};
        }
        std::string ignored;
        if (sim.activateSkill(slot, target, &ignored)) { cast = true; break; }
    }
    return cast;
}

void advance(ta::GameSim& sim, int maxTicks, Policy policy) {
    for (int tick = 0; tick < maxTicks && !sim.isGameOver() && !sim.isVictory(); ++tick) {
        if (sim.upgradePending()) {
            int best = 0;
            for (int index = 1; index < static_cast<int>(sim.pendingChoices().size()); ++index) {
                if (choiceScore(sim, sim.pendingChoices()[static_cast<std::size_t>(index)], policy, index) > choiceScore(sim, sim.pendingChoices()[static_cast<std::size_t>(best)], policy, best)) best = index;
            }
            sim.chooseUpgrade(best);
        }
        if (tick % 15 == 0) castReadySkills(sim, skillPolicyFor(policy), tick);
        if (tick % (ta::GameSim::TickRate * 8) == 0) sim.activateUltimate();
        sim.tick();
    }
}

struct Telemetry { int total = 0; int victories = 0; int failures = 0; int nonTerminal = 0; long long score = 0; long long kills = 0; long long leaks = 0; long long damage = 0; long long reactions = 0; long long statuses = 0; long long skillCasts = 0; long long failedSkillCasts = 0; long long skillDamage = 0; long long skillHealing = 0; long long skillTargets = 0; long long skillControlTicks = 0; long long skillSummons = 0; int highestWave = 0; };

void record(Telemetry& result, const ta::GameSim& sim) {
    ++result.total;
    if (sim.isVictory()) ++result.victories;
    else if (sim.isGameOver()) ++result.failures;
    else ++result.nonTerminal;
    result.score += sim.stats().score;
    result.kills += sim.stats().kills;
    result.leaks += sim.stats().leaks;
    result.damage += sim.stats().damageDealt;
    result.reactions += sim.stats().reactionTriggers;
    result.statuses += sim.stats().statusApplications;
    result.skillCasts += sim.stats().skillCasts;
    result.failedSkillCasts += sim.stats().failedSkillCasts;
    for (std::size_t index = 0; index < ta::SkillSlotCount + 4; ++index) {
        if (index >= sim.stats().skillDamage.size()) break;
        result.skillDamage += sim.stats().skillDamage[index];
        result.skillHealing += sim.stats().skillHealing[index];
        result.skillTargets += sim.stats().skillTargets[index];
        result.skillControlTicks += sim.stats().skillControlTicks[index];
        result.skillSummons += sim.stats().skillSummons[index];
    }
    result.highestWave = std::max(result.highestWave, sim.stats().wave);
}

Telemetry combine(const Telemetry& left, const Telemetry& right) {
    Telemetry result = left;
    result.total += right.total; result.victories += right.victories; result.failures += right.failures; result.nonTerminal += right.nonTerminal;
    result.score += right.score; result.kills += right.kills; result.leaks += right.leaks; result.damage += right.damage; result.reactions += right.reactions; result.statuses += right.statuses; result.skillCasts += right.skillCasts; result.failedSkillCasts += right.failedSkillCasts; result.skillDamage += right.skillDamage; result.skillHealing += right.skillHealing; result.skillTargets += right.skillTargets; result.skillControlTicks += right.skillControlTicks; result.skillSummons += right.skillSummons; result.highestWave = std::max(result.highestWave, right.highestWave);
    return result;
}

void printTelemetryCell(const char* label, const Telemetry& result) {
    const double victoryRate = result.total == 0 ? 0.0 : static_cast<double>(result.victories) * 100.0 / static_cast<double>(result.total);
    const double averageWave = result.total == 0 ? 0.0 : static_cast<double>(result.highestWave) / static_cast<double>(result.total);
    const double averageLeaks = result.total == 0 ? 0.0 : static_cast<double>(result.leaks) / static_cast<double>(result.total);
    std::cout << "Tower Ascend balance cell=" << label << " runs=" << result.total << " victory_rate=" << std::fixed << std::setprecision(1) << victoryRate << "% avg_leaks=" << averageLeaks << " max_wave=" << result.highestWave << " victories=" << result.victories << "\n";
    (void)averageWave;
}
}

int main(int argc, char** argv) {
    int runsPerCombination = 4;
    if (argc > 1) {
        try { runsPerCombination = std::stoi(argv[1]); } catch (...) { runsPerCombination = 0; }
    }
    if (runsPerCombination <= 0 || runsPerCombination > 1000) {
        std::cerr << "usage: ta_balance_check [runs-per-weapon-arena, 1..1000]\n";
        return EXIT_FAILURE;
    }

    ta::ContentConfig content;
    std::string error;
    const std::string contentDirectory = ta::defaultContentDirectory();
    if (!ta::loadContentConfig(contentDirectory, content, &error)) {
        std::cerr << "content load failed: " << error << '\n';
        return EXIT_FAILURE;
    }
    const std::array<Policy, 6> policies{{Policy::First, Policy::Random, Policy::TagMatch, Policy::Synergy, Policy::ThreatAware, Policy::Oracle}};
    constexpr std::array<const char*, 3> profileNames{{"fresh", "mid", "max"}};
    constexpr std::array<std::uint8_t, 3> profileLevels{{0, 10, 20}};
    std::array<std::array<Telemetry, 3>, 6> results{};
    bool passed = true;
    for (std::size_t policyIndex = 0; policyIndex < policies.size(); ++policyIndex) {
        const Policy policy = policies[policyIndex];
        for (std::size_t profileIndex = 0; profileIndex < profileLevels.size(); ++profileIndex) {
        Telemetry result;
        for (int combination = 0; combination < 16; ++combination) {
            ta::SkullMask mask = 0;
            for (int skull = 1; skull <= 4; ++skull) if ((combination & (1 << (skull - 1))) != 0) mask = static_cast<ta::SkullMask>(mask | (1u << skull));
            for (int arena = 0; arena < 3; ++arena) for (int weapon = 0; weapon < 5; ++weapon) for (int run = 0; run < runsPerCombination; ++run) {
                const std::uint32_t seed = 0xC7000000u + static_cast<std::uint32_t>(combination * 100000 + arena * 1000 + weapon * 100 + run + 1);
                ta::GameSim sim(seed);
                sim.setContentConfig(content);
                sim.setSkullMask(mask);
                sim.setArena(static_cast<ta::Arena>(arena));
                sim.setWeapon(static_cast<ta::Weapon>(weapon));
                std::array<std::uint8_t, 5> moduleLevels{};
                moduleLevels.fill(profileLevels[profileIndex]);
                sim.setWorkshopProgress(profileLevels[profileIndex], moduleLevels);
                sim.setSupportProgress(moduleLevels);
                sim.reset(seed);
                advance(sim, 100000, policy);
                record(result, sim);
            }
        }
        const double victoryRate = result.total == 0 ? 0.0 : static_cast<double>(result.victories) * 100.0 / static_cast<double>(result.total);
        const double terminalRate = result.total == 0 ? 0.0 : static_cast<double>(result.total - result.nonTerminal) * 100.0 / static_cast<double>(result.total);
        const double averageScore = result.total == 0 ? 0.0 : static_cast<double>(result.score) / static_cast<double>(result.total);
        const double averageKills = result.total == 0 ? 0.0 : static_cast<double>(result.kills) / static_cast<double>(result.total);
        const double averageLeaks = result.total == 0 ? 0.0 : static_cast<double>(result.leaks) / static_cast<double>(result.total);
        const double averageDamage = result.total == 0 ? 0.0 : static_cast<double>(result.damage) / static_cast<double>(result.total);
        const double averageReactions = result.total == 0 ? 0.0 : static_cast<double>(result.reactions) / static_cast<double>(result.total);
        const double averageStatuses = result.total == 0 ? 0.0 : static_cast<double>(result.statuses) / static_cast<double>(result.total);
        std::cout << "Tower Ascend balance policy=" << policyName(policy) << " profile=" << profileNames[profileIndex] << " runs=" << result.total
                  << " victories=" << result.victories << " victory_rate=" << std::fixed << std::setprecision(1) << victoryRate << "%"
                  << " failures=" << result.failures << " non_terminal=" << result.nonTerminal
                  << " terminal_rate=" << terminalRate << "% avg_score=" << averageScore << " avg_kills=" << averageKills << " avg_leaks=" << averageLeaks << " avg_damage=" << averageDamage << " avg_reactions=" << averageReactions << " avg_statuses=" << averageStatuses << " skill_casts=" << (result.total == 0 ? 0.0 : static_cast<double>(result.skillCasts) / result.total) << " skill_damage=" << (result.total == 0 ? 0.0 : static_cast<double>(result.skillDamage) / result.total) << " skill_healing=" << (result.total == 0 ? 0.0 : static_cast<double>(result.skillHealing) / result.total) << " skill_targets=" << (result.total == 0 ? 0.0 : static_cast<double>(result.skillTargets) / result.total) << " highest_wave=" << result.highestWave << '\n';
        results[policyIndex][profileIndex] = result;
        passed = passed && result.nonTerminal == 0;
        }
    }
    Telemetry randomResult, synergyResult;
    for (std::size_t profileIndex = 0; profileIndex < profileLevels.size(); ++profileIndex) {
        randomResult = combine(randomResult, results[1][profileIndex]);
        synergyResult = combine(synergyResult, results[3][profileIndex]);
    }
    const double randomVictoryRate = randomResult.total == 0 ? 0.0 : static_cast<double>(randomResult.victories) / static_cast<double>(randomResult.total);
    const double synergyVictoryRate = synergyResult.total == 0 ? 0.0 : static_cast<double>(synergyResult.victories) / static_cast<double>(synergyResult.total);
    if (randomVictoryRate >= 0.80) {
        std::cerr << "balance policy failure: random choices are too close to universal victory\n";
        passed = false;
    }
    if (synergyVictoryRate <= randomVictoryRate) {
        std::cerr << "balance policy failure: synergy policy does not outperform random choices\n";
        passed = false;
    }
    // A per-weapon/per-arena Oracle view prevents the aggregate matrix from
    // hiding a weak weapon behind stronger neighbors. This is also the
    // smallest report that directly corresponds to the authored matrix axes.
    for (int arena = 0; arena < 3; ++arena) {
        for (int weapon = 0; weapon < 5; ++weapon) {
            Telemetry cell;
            for (int run = 0; run < runsPerCombination; ++run) {
                const std::uint32_t seed = 0xD4000000u + static_cast<std::uint32_t>(arena * 1000 + weapon * 100 + run + 1);
                ta::GameSim sim(seed);
                sim.setContentConfig(content);
                sim.setArena(static_cast<ta::Arena>(arena));
                sim.setWeapon(static_cast<ta::Weapon>(weapon));
                sim.reset(seed);
                advance(sim, 100000, Policy::Oracle);
                record(cell, sim);
            }
            const std::string label = std::string("oracle/") + ta::arenaName(static_cast<ta::Arena>(arena)) + "/" + ta::weaponName(static_cast<ta::Weapon>(weapon));
            printTelemetryCell(label.c_str(), cell);
            if (cell.nonTerminal != 0) passed = false;
        }
    }
    // Every authored Legendary Evolution gets its own deterministic regression
    // cell. This does not replace the broad policy matrix; it verifies that
    // each parent-ultimate specialization is legal, replayable, and terminates
    // under the strongest authored progression fixture.
    std::array<std::uint8_t, 5> maxModules{};
    maxModules.fill(20);
    for (int evolutionValue = static_cast<int>(ta::UltimateEvolution::SolarAftermath);
         evolutionValue <= static_cast<int>(ta::UltimateEvolution::TerminalDischarge); ++evolutionValue) {
        const ta::UltimateEvolution evolution = static_cast<ta::UltimateEvolution>(evolutionValue);
        const ta::Ultimate ultimate = static_cast<ta::Ultimate>((evolutionValue - 1) / 3);
        ta::GameSim evolutionRun(0xE7000000u + static_cast<std::uint32_t>(evolutionValue));
        evolutionRun.setContentConfig(content);
        evolutionRun.setUltimate(ultimate);
        evolutionRun.setUltimateEvolution(evolution);
        evolutionRun.setWorkshopProgress(20, maxModules);
        evolutionRun.setSupportProgress(maxModules);
        evolutionRun.reset(0xE7000000u + static_cast<std::uint32_t>(evolutionValue));
        advance(evolutionRun, 100000, Policy::Oracle);
        std::cout << "Tower Ascend balance evolution=" << ta::ultimateEvolutionName(evolution)
                  << " parent=" << ta::ultimateName(ultimate)
                  << " state=" << evolutionRun.statusText()
                  << " wave=" << evolutionRun.waveNumber()
                  << " damage=" << evolutionRun.stats().damageDealt << '\n';
        if (!evolutionRun.isGameOver() && !evolutionRun.isVictory()) {
            std::cerr << "balance evolution failure: non-terminal evolution cell for " << ta::ultimateEvolutionName(evolution) << '\n';
            passed = false;
        }
    }
    for (int moduleValue = 0; moduleValue < 10; ++moduleValue) {
        const ta::Ultimate ultimate = static_cast<ta::Ultimate>(moduleValue / 2);
        const ta::UltimateModule module = static_cast<ta::UltimateModule>(moduleValue);
        ta::GameSim moduleRun(0xF7000000u + static_cast<std::uint32_t>(moduleValue));
        moduleRun.setContentConfig(content);
        moduleRun.setUltimate(ultimate);
        moduleRun.setUltimateModule(module);
        moduleRun.setWorkshopProgress(20, maxModules);
        moduleRun.setSupportProgress(maxModules);
        moduleRun.reset(0xF7000000u + static_cast<std::uint32_t>(moduleValue));
        advance(moduleRun, 100000, Policy::Oracle);
        std::cout << "Tower Ascend balance module=" << content.ultimateModuleMetadata[static_cast<std::size_t>(moduleValue)].display
                  << " parent=" << ta::ultimateName(ultimate) << " state=" << moduleRun.statusText() << " wave=" << moduleRun.waveNumber() << " damage=" << moduleRun.stats().damageDealt << '\n';
        if (!moduleRun.isGameOver() && !moduleRun.isVictory()) passed = false;
    }
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
