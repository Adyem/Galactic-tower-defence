#include "game.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <type_traits>
#include <cstring>
#include <cstdlib>

namespace ta {
namespace {

bool readFile(const std::filesystem::path& path, std::string& text, std::string* error) {
    std::ifstream file(path);
    if (!file) { if (error) *error = "unable to open content file: " + path.string(); return false; }
    std::ostringstream stream; stream << file.rdbuf(); text = stream.str();
    int braces = 0;
    int brackets = 0;
    bool quoted = false;
    bool escaped = false;
    for (char c : text) {
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && quoted) { escaped = true; continue; }
        if (c == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == '{') ++braces;
        if (c == '}') --braces;
        if (c == '[') ++brackets;
        if (c == ']') --brackets;
        if (braces < 0 || brackets < 0) break;
    }
    if (quoted || braces != 0 || brackets != 0) {
        if (error) *error = "invalid JSON structure: " + path.string();
        return false;
    }
    return true;
}

template <typename T>
bool numbersForKey(const std::string& text, const char* key, std::vector<T>& values) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        try {
            if constexpr (std::is_same_v<T, int>) values.push_back(std::stoi((*it)[1].str()));
            else values.push_back(static_cast<T>(std::stof((*it)[1].str())));
        } catch (...) { return false; }
    }
    return !values.empty();
}

} // namespace

std::uint32_t contentFingerprint(const ContentConfig& content) {
    std::uint32_t hash = 2166136261u;
    auto add = [&hash](std::uint32_t value) { hash ^= value; hash *= 16777619u; };
    auto addFloat = [&add](float value) { std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); add(bits); };
    for (float value : content.weaponDamage) addFloat(value);
    for (int value : content.weaponCooldown) add(static_cast<std::uint32_t>(value));
    for (float value : content.projectileSpeed) addFloat(value);
    for (int value : content.ultimateCooldownTicks) add(static_cast<std::uint32_t>(value));
    for (float value : content.ultimateDamageScale) addFloat(value);
    for (int value : content.waveEnemyBudget) add(static_cast<std::uint32_t>(value));
    for (int value : content.waveSpawnInterval) add(static_cast<std::uint32_t>(value));
    for (const auto& row : content.waveEnemyTypeWeight) for (float value : row) addFloat(value);
    for (float value : content.skullScoreMultiplier) addFloat(value);
    for (float value : content.skullSpawnScale) addFloat(value);
    for (int value : content.skullLives) add(static_cast<std::uint32_t>(value));
    for (float value : content.skullSpeedScale) addFloat(value);
    for (int value : content.skullCurrencyBonus) add(static_cast<std::uint32_t>(value));
    for (int value : content.skullBossCurrencyBonus) add(static_cast<std::uint32_t>(value));
    for (float value : content.upgradeWeight) addFloat(value);
    for (float value : content.upgradeValueA) addFloat(value);
    for (float value : content.upgradeValueB) addFloat(value);
    for (float value : content.arenaHealthScale) addFloat(value);
    for (float value : content.arenaSpeedScale) addFloat(value);
    for (float value : content.arenaPathAmplitude) addFloat(value);
    for (float value : content.arenaPathFrequency) addFloat(value);
    for (float value : content.enemyHealthScale) addFloat(value);
    for (float value : content.enemySpeedScale) addFloat(value);
    for (float value : content.enemyDamageResistance) addFloat(value);
    for (float value : content.enemyRadius) addFloat(value);
    for (float value : content.enemyTeleportCooldown) addFloat(value);
    add(static_cast<std::uint32_t>(content.bossAttackCooldownTicks));
    add(static_cast<std::uint32_t>(content.bossTelegraphTicks));
    add(static_cast<std::uint32_t>(content.bossAttackLives));
    return hash == 0 ? 1u : hash;
}

std::string defaultContentDirectory() {
    if (const char* environment = std::getenv("TA_CONTENT_DIR"); environment != nullptr && *environment != '\0') return environment;
#ifdef TA_CONTENT_DIR
    const std::filesystem::path compiled = TA_CONTENT_DIR;
    if (std::filesystem::exists(compiled / "weapons.json")) return compiled.string();
#endif
    const std::filesystem::path relativeShare = std::filesystem::path("share") / "tower_ascend" / "content";
    if (std::filesystem::exists(relativeShare / "weapons.json")) return relativeShare.string();
    const std::filesystem::path siblingShare = std::filesystem::path("..") / "share" / "tower_ascend" / "content";
    if (std::filesystem::exists(siblingShare / "weapons.json")) return siblingShare.string();
    return "assets/content";
}

bool loadContentConfig(const std::string& directory, ContentConfig& output, std::string* error) {
    ContentConfig parsed;
    std::string weapons;
    if (!readFile(std::filesystem::path(directory) / "weapons.json", weapons, error)) return false;
    std::vector<float> damage, speed;
    std::vector<int> cooldown;
    if (!numbersForKey(weapons, "damage", damage) || !numbersForKey(weapons, "cooldown_ticks", cooldown) || !numbersForKey(weapons, "projectile_speed", speed) || damage.size() != 5 || cooldown.size() != 5 || speed.size() != 5) {
        if (error) *error = "weapons content must define five numeric weapon records";
        return false;
    }
    for (std::size_t i = 0; i < 5; ++i) { parsed.weaponDamage[i] = damage[i]; parsed.weaponCooldown[i] = cooldown[i]; parsed.projectileSpeed[i] = speed[i]; }

    std::string ultimates;
    if (!readFile(std::filesystem::path(directory) / "ultimates.json", ultimates, error)) return false;
    std::vector<int> ultimateCooldownSeconds;
    std::vector<float> ultimateDamage;
    if (!numbersForKey(ultimates, "cooldown_seconds", ultimateCooldownSeconds) || !numbersForKey(ultimates, "damage_scale", ultimateDamage) || ultimateCooldownSeconds.size() != 5 || ultimateDamage.size() != 5) {
        if (error) *error = "ultimates content must define five cooldown and damage records";
        return false;
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (ultimateCooldownSeconds[i] <= 0 || ultimateDamage[i] <= 0.0f) { if (error) *error = "ultimate tuning values must be positive"; return false; }
        parsed.ultimateCooldownTicks[i] = ultimateCooldownSeconds[i] * 30;
        parsed.ultimateDamageScale[i] = ultimateDamage[i];
    }

    std::string waves;
    if (!readFile(std::filesystem::path(directory) / "waves.json", waves, error)) return false;
    std::vector<int> budgets, intervals;
    if (!numbersForKey(waves, "enemy_budget", budgets) || !numbersForKey(waves, "spawn_interval_ticks", intervals) || budgets.size() != 10 || intervals.size() != 10) {
        if (error) *error = "waves content must define ten numeric wave records";
        return false;
    }
    for (std::size_t i = 0; i < 10; ++i) { if (budgets[i] <= 0 || intervals[i] <= 0) { if (error) *error = "wave values must be positive"; return false; } parsed.waveEnemyBudget[i] = budgets[i]; parsed.waveSpawnInterval[i] = intervals[i]; }
    const char* weightKeys[] = {"grunt_weight", "runner_weight", "tank_weight", "shielded_weight", "swarmling_weight", "teleporter_weight", "boss_weight"};
    for (std::size_t type = 0; type < 7; ++type) {
        std::vector<float> weights;
        if (!numbersForKey(waves, weightKeys[type], weights) || weights.size() != 10) {
            if (error) *error = "waves content must define ten weights for every enemy archetype";
            return false;
        }
        for (std::size_t waveIndex = 0; waveIndex < 10; ++waveIndex) {
            if (weights[waveIndex] < 0.0f) { if (error) *error = "wave enemy weights cannot be negative"; return false; }
            parsed.waveEnemyTypeWeight[waveIndex][type] = weights[waveIndex];
        }
    }
    for (std::size_t waveIndex = 0; waveIndex < 10; ++waveIndex) {
        float total = 0.0f;
        for (float weight : parsed.waveEnemyTypeWeight[waveIndex]) total += weight;
        if (total <= 0.0f) { if (error) *error = "each wave must have a positive enemy mix"; return false; }
    }

    std::string upgrades;
    if (!readFile(std::filesystem::path(directory) / "upgrades.json", upgrades, error)) return false;
    std::vector<float> upgradeWeights, upgradeValuesA, upgradeValuesB;
    if (!numbersForKey(upgrades, "weight", upgradeWeights) || !numbersForKey(upgrades, "value_a", upgradeValuesA) || !numbersForKey(upgrades, "value_b", upgradeValuesB) || upgradeWeights.size() != 15 || upgradeValuesA.size() != 15 || upgradeValuesB.size() != 15) {
        if (error) *error = "upgrades content must define fifteen weights and value pairs";
        return false;
    }
    for (std::size_t i = 0; i < 15; ++i) {
        if (upgradeWeights[i] <= 0.0f || upgradeValuesA[i] <= 0.0f || upgradeValuesB[i] < 0.0f) { if (error) *error = "upgrade weights and values must be positive where required"; return false; }
        parsed.upgradeWeight[i] = upgradeWeights[i];
        parsed.upgradeValueA[i] = upgradeValuesA[i];
        parsed.upgradeValueB[i] = upgradeValuesB[i];
    }

    std::string skulls;
    if (!readFile(std::filesystem::path(directory) / "skulls.json", skulls, error)) return false;
    std::vector<float> multipliers, spawnScales, speedScales;
    std::vector<int> livesValues, currencyBonuses, bossCurrencyBonuses;
    if (!numbersForKey(skulls, "score_multiplier", multipliers) || !numbersForKey(skulls, "spawn_scale", spawnScales) || !numbersForKey(skulls, "speed_scale", speedScales) ||
        !numbersForKey(skulls, "lives", livesValues) || !numbersForKey(skulls, "currency_bonus", currencyBonuses) || !numbersForKey(skulls, "boss_currency_bonus", bossCurrencyBonuses) ||
        multipliers.size() != 4 || spawnScales.size() != 4 || speedScales.size() != 4 || livesValues.size() != 4 || currencyBonuses.size() != 4 || bossCurrencyBonuses.size() != 4) {
        if (error) *error = "skulls content must define four complete modifier records";
        return false;
    }
    for (std::size_t i = 0; i < multipliers.size(); ++i) {
        if (multipliers[i] <= 0.0f || spawnScales[i] <= 0.0f || speedScales[i] <= 0.0f || livesValues[i] <= 0 || currencyBonuses[i] < 0 || bossCurrencyBonuses[i] < 0) { if (error) *error = "skull modifier values are outside safe ranges"; return false; }
        parsed.skullScoreMultiplier[i + 1] = multipliers[i];
        parsed.skullSpawnScale[i + 1] = spawnScales[i];
        parsed.skullLives[i + 1] = livesValues[i];
        parsed.skullSpeedScale[i + 1] = speedScales[i];
        parsed.skullCurrencyBonus[i + 1] = currencyBonuses[i];
        parsed.skullBossCurrencyBonus[i + 1] = bossCurrencyBonuses[i];
    }
    std::string arenas;
    if (!readFile(std::filesystem::path(directory) / "arenas.json", arenas, error)) return false;
    std::vector<float> healthScale, speedScale, amplitude, frequency;
    if (!numbersForKey(arenas, "health_scale", healthScale) || !numbersForKey(arenas, "speed_scale", speedScale) || !numbersForKey(arenas, "path_amplitude", amplitude) || !numbersForKey(arenas, "path_frequency", frequency) || healthScale.size() != 3 || speedScale.size() != 3 || amplitude.size() != 3 || frequency.size() != 3) {
        if (error) *error = "arenas content must define three balance and path profiles";
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) { parsed.arenaHealthScale[i] = healthScale[i]; parsed.arenaSpeedScale[i] = speedScale[i]; parsed.arenaPathAmplitude[i] = amplitude[i]; parsed.arenaPathFrequency[i] = frequency[i]; }

    std::string enemies;
    if (!readFile(std::filesystem::path(directory) / "enemies.json", enemies, error)) return false;
    std::vector<float> enemyHealth, enemySpeed, enemyResistance, enemyRadius, enemyTeleport;
    if (!numbersForKey(enemies, "health_scale", enemyHealth) || !numbersForKey(enemies, "speed_scale", enemySpeed) ||
        !numbersForKey(enemies, "damage_resistance", enemyResistance) || !numbersForKey(enemies, "radius", enemyRadius) ||
        !numbersForKey(enemies, "teleport_cooldown", enemyTeleport) || enemyHealth.size() != 7 || enemySpeed.size() != 7 ||
        enemyResistance.size() != 7 || enemyRadius.size() != 7 || enemyTeleport.size() != 7) {
        if (error) *error = "enemies content must define seven complete numeric archetypes";
        return false;
    }
    for (std::size_t i = 0; i < 7; ++i) {
        if (enemyHealth[i] <= 0.0f || enemySpeed[i] <= 0.0f || enemyRadius[i] <= 0.0f || enemyResistance[i] < 0.0f || enemyResistance[i] > 1.0f || enemyTeleport[i] < 0.0f) {
            if (error) *error = "enemy tuning values are outside safe ranges";
            return false;
        }
        parsed.enemyHealthScale[i] = enemyHealth[i];
        parsed.enemySpeedScale[i] = enemySpeed[i];
        parsed.enemyDamageResistance[i] = enemyResistance[i];
        parsed.enemyRadius[i] = enemyRadius[i];
        parsed.enemyTeleportCooldown[i] = enemyTeleport[i];
    }
    std::vector<int> bossCooldownSeconds, bossTelegraphMilliseconds, bossAttackLives;
    if (!numbersForKey(enemies, "attack_cooldown_seconds", bossCooldownSeconds) || !numbersForKey(enemies, "telegraph_ms", bossTelegraphMilliseconds) ||
        !numbersForKey(enemies, "attack_lives", bossAttackLives) || bossCooldownSeconds.size() != 1 || bossTelegraphMilliseconds.size() != 1 || bossAttackLives.size() != 1 ||
        bossCooldownSeconds[0] <= 0 || bossTelegraphMilliseconds[0] <= 0 || bossAttackLives[0] <= 0) {
        if (error) *error = "boss content must define a positive cooldown, telegraph, and attack damage";
        return false;
    }
    parsed.bossAttackCooldownTicks = bossCooldownSeconds[0] * 30;
    parsed.bossTelegraphTicks = std::max(1, (bossTelegraphMilliseconds[0] * 30 + 999) / 1000);
    parsed.bossAttackLives = bossAttackLives[0];
    output = parsed;
    return true;
}

void GameSim::setContentConfig(const ContentConfig& config) {
    if (tickCount == 0 && enemyList.empty()) {
        content = config;
        ultimateMaxCooldown = content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)];
    }
}

} // namespace ta
