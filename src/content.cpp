#include "game.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <type_traits>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <set>

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

std::string stringForKey(const std::string& record, const char* key) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    return std::regex_search(record, match, pattern) ? match[1].str() : std::string();
}

int integerForKey(const std::string& record, const char* key, int fallback) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*([0-9]+)");
    if (!std::regex_search(record, match, pattern)) return fallback;
    try { return std::stoi(match[1].str()); } catch (...) { return fallback; }
}

float floatForKey(const std::string& record, const char* key, float fallback) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (!std::regex_search(record, match, pattern)) return fallback;
    try { return std::stof(match[1].str()); } catch (...) { return fallback; }
}

std::vector<std::string> stringsForKey(const std::string& record, const char* key) {
    std::smatch match;
    const std::regex arrayPattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    if (!std::regex_search(record, match, arrayPattern)) return {};
    std::vector<std::string> result;
    const std::regex valuePattern("\\\"([^\\\"]*)\\\"");
    const std::string values = match[1].str();
    for (std::sregex_iterator it(values.begin(), values.end(), valuePattern), end; it != end; ++it) result.push_back((*it)[1].str());
    return result;
}

ContentMetadata metadataForId(const std::string& text, const char* id) {
    const std::regex objectPattern("\\{[^{}]*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), objectPattern), end; it != end; ++it) {
        const std::string record = (*it)[0].str();
        if (stringForKey(record, "id") != id) continue;
        ContentMetadata metadata;
        metadata.id = id;
        metadata.display = stringForKey(record, "display");
        metadata.shortDescription = stringForKey(record, "short_description");
        metadata.longDescription = stringForKey(record, "long_description");
        metadata.strengths = stringsForKey(record, "strengths");
        metadata.weaknesses = stringsForKey(record, "weaknesses");
        metadata.synergyTags = stringsForKey(record, "synergy_tags");
        metadata.prerequisites = stringsForKey(record, "prerequisites");
        metadata.exclusions = stringsForKey(record, "exclusions");
        metadata.maxStacks = std::max(1, integerForKey(record, "max_stacks", 1));
        metadata.iconId = stringForKey(record, "icon_id");
        metadata.effect = stringForKey(record, "effect");
        return metadata;
    }
    return {};
}

RunTypeMetadata runTypeMetadataForId(const std::string& text, const char* id) {
    const std::regex objectPattern("\\{[^{}]*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), objectPattern), end; it != end; ++it) {
        const std::string record = (*it)[0].str();
        if (stringForKey(record, "id") != id) continue;
        return {id, stringForKey(record, "display"), stringForKey(record, "description"), stringForKey(record, "short_description"), stringForKey(record, "long_description"), stringForKey(record, "rules"), stringForKey(record, "icon_id")};
    }
    return {};
}

template <std::size_t N>
bool loadMetadata(const std::string& text, const std::array<const char*, N>& ids, std::array<ContentMetadata, N>& output) {
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = metadataForId(text, ids[index]);
        if (output[index].id.empty() || output[index].display.empty() || output[index].shortDescription.empty() || output[index].longDescription.empty() || output[index].iconId.empty() || output[index].strengths.empty() || output[index].weaknesses.empty() || output[index].synergyTags.empty()) return false;
    }
    return true;
}

template <typename Container>
void addMetadataToHash(const Container& metadata, const std::function<void(const std::string&)>& addString) {
    for (const ContentMetadata& entry : metadata) {
        addString(entry.id); addString(entry.display); addString(entry.shortDescription); addString(entry.longDescription); addString(entry.iconId);
        for (const std::string& value : entry.strengths) addString(value);
        for (const std::string& value : entry.weaknesses) addString(value);
        for (const std::string& value : entry.synergyTags) addString(value);
        for (const std::string& value : entry.prerequisites) addString(value);
        for (const std::string& value : entry.exclusions) addString(value);
        addString(std::to_string(entry.maxStacks));
        addString(entry.effect);
    }
}

} // namespace

std::uint32_t contentFingerprint(const ContentConfig& content) {
    std::uint32_t hash = 2166136261u;
    auto add = [&hash](std::uint32_t value) { hash ^= value; hash *= 16777619u; };
    auto addString = [&add](const std::string& value) { for (const unsigned char character : value) add(character); add(0u); };
    auto addFloat = [&add](float value) { std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); add(bits); };
    for (float value : content.weaponDamage) addFloat(value);
    for (int value : content.weaponCooldown) add(static_cast<std::uint32_t>(value));
    for (float value : content.projectileSpeed) addFloat(value);
    for (int value : content.ultimateCooldownTicks) add(static_cast<std::uint32_t>(value));
    for (float value : content.ultimateDamageScale) addFloat(value);
    for (float value : content.chassisWeaponDamageScale) addFloat(value);
    for (float value : content.chassisWeaponCooldownScale) addFloat(value);
    for (float value : content.chassisUltimateCooldownScale) addFloat(value);
    for (int value : content.chassisLivesBonus) add(static_cast<std::uint32_t>(value));
    for (int value : content.waveEnemyBudget) add(static_cast<std::uint32_t>(value));
    for (int value : content.waveSpawnInterval) add(static_cast<std::uint32_t>(value));
    for (const auto& row : content.waveEnemyTypeWeight) for (float value : row) addFloat(value);
    for (float value : content.skullScoreMultiplier) addFloat(value);
    for (float value : content.skullSpawnScale) addFloat(value);
    for (int value : content.skullLives) add(static_cast<std::uint32_t>(value));
    for (float value : content.skullSpeedScale) addFloat(value);
    for (int value : content.skullCurrencyBonus) add(static_cast<std::uint32_t>(value));
    for (int value : content.skullBossCurrencyBonus) add(static_cast<std::uint32_t>(value));
    add(content.ultimateEvolutionCatalogHash);
    add(content.dailyChallengeCatalogHash);
    add(content.supportModuleCatalogHash);
    add(content.skillEntityCatalogHash);
    add(static_cast<std::uint32_t>(content.maxAlliedUnits));
    add(static_cast<std::uint32_t>(content.maxBuildings));
    add(static_cast<std::uint32_t>(content.maxSkillZones));
    for (int value : content.runExpectedMinutes) add(static_cast<std::uint32_t>(value));
    for (int value : content.runWaveLimit) add(static_cast<std::uint32_t>(value));
    for (float value : content.runRewardMultiplier) addFloat(value);
    for (int value : content.runWorkshopActive) add(static_cast<std::uint32_t>(value));
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
    addMetadataToHash(content.weaponMetadata, addString);
    addMetadataToHash(content.chassisMetadata, addString);
    addMetadataToHash(content.upgradeMetadata, addString);
    addMetadataToHash(content.ultimateMetadata, addString);
    addMetadataToHash(content.supportMetadata, addString);
    addMetadataToHash(content.currencyMetadata, addString);
    addMetadataToHash(content.workshopMetadata, addString);
    for (std::uint32_t value : content.workshopBaseCost) add(value);
    for (std::uint32_t value : content.workshopCostStep) add(value);
    for (std::uint32_t value : content.workshopMaxLevel) add(value);
    addMetadataToHash(content.skullMetadata, addString);
    addMetadataToHash(content.arenaMetadata, addString);
    addMetadataToHash(content.enemyMetadata, addString);
    addMetadataToHash(content.evolutionMetadata, addString);
    for (std::uint32_t value : content.ultimateEvolutionCost) add(value);
    addMetadataToHash(content.ultimateModuleMetadata, addString);
    for (std::uint32_t value : content.ultimateModuleCost) add(value);
    for (float value : content.ultimateModuleCooldownScale) addFloat(value);
    for (float value : content.ultimateModuleDamageScale) addFloat(value);
    addMetadataToHash(content.synergyMetadata, addString);
    addMetadataToHash(content.statusMetadata, addString);
    addMetadataToHash(content.allyMetadata, addString);
    addMetadataToHash(content.buildingMetadata, addString);
    add(content.skillCatalogHash);
    for (const SkillDefinition& definition : content.skillDefinitions) {
        addString(definition.id); addString(definition.display); addString(definition.shortDescription); addString(definition.longDescription);
        addString(definition.iconId); addString(definition.effect); addString(definition.targetMode);
        add(static_cast<std::uint32_t>(definition.cooldownTicks)); add(static_cast<std::uint32_t>(definition.charges)); add(static_cast<std::uint32_t>(definition.durationTicks));
        addFloat(definition.range); addFloat(definition.radius); addFloat(definition.valueA); addFloat(definition.valueB);
        for (const std::string& tag : definition.tags) addString(tag);
        for (const std::string& operation : definition.operations) addString(operation);
    }
    for (const SkillNodeDefinition& node : content.skillNodes) {
        addString(node.id); addString(node.skillId); addString(node.parentId); addString(node.branchId); addString(node.display); addString(node.description); addString(node.iconLayer);
        add(static_cast<std::uint32_t>(node.tier)); add(static_cast<std::uint32_t>(node.maxRank)); add(node.cost); addFloat(node.cooldownScale); addFloat(node.radiusScale); addFloat(node.valueScale); add(static_cast<std::uint32_t>(node.chargesDelta));
    }
    for (const RunTypeMetadata& entry : content.runTypeMetadata) {
        addString(entry.id); addString(entry.display); addString(entry.description); addString(entry.shortDescription); addString(entry.longDescription); addString(entry.rules); addString(entry.iconId);
    }
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

    std::string chassis;
    if (!readFile(std::filesystem::path(directory) / "tower_chassis.json", chassis, error)) return false;
    const std::array<const char*, 3> chassisIds{{"vanguard", "bastion", "catalyst"}};
    if (!loadMetadata(chassis, chassisIds, parsed.chassisMetadata)) { if (error) *error = "tower chassis metadata is incomplete"; return false; }
    std::vector<float> chassisDamage, chassisCooldown, chassisUltimate;
    std::vector<int> chassisLives;
    if (!numbersForKey(chassis, "weapon_damage_scale", chassisDamage) || !numbersForKey(chassis, "weapon_cooldown_scale", chassisCooldown) || !numbersForKey(chassis, "ultimate_cooldown_scale", chassisUltimate) || !numbersForKey(chassis, "lives_bonus", chassisLives) || chassisDamage.size() != 3 || chassisCooldown.size() != 3 || chassisUltimate.size() != 3 || chassisLives.size() != 3) { if (error) *error = "tower chassis content must define three complete tuning records"; return false; }
    for (std::size_t i = 0; i < 3; ++i) {
        if (chassisDamage[i] <= 0.0f || chassisCooldown[i] <= 0.0f || chassisUltimate[i] <= 0.0f || chassisLives[i] < 0) { if (error) *error = "tower chassis tuning values are outside safe ranges"; return false; }
        parsed.chassisWeaponDamageScale[i] = chassisDamage[i]; parsed.chassisWeaponCooldownScale[i] = chassisCooldown[i]; parsed.chassisUltimateCooldownScale[i] = chassisUltimate[i]; parsed.chassisLivesBonus[i] = chassisLives[i];
    }

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
    std::string ultimateEvolutions;
    if (!readFile(std::filesystem::path(directory) / "ultimate_evolutions.json", ultimateEvolutions, error)) return false;
    const char* evolutionIds[] = {"solar_aftermath", "extinction_spear", "shattered_sky", "resonant_arsenal", "suppressive_grid", "execution_protocol", "brittle_singularity", "permafrost_engine", "cold_conductor", "event_horizon", "chrono_reversal", "mass_driver", "overdrive_link", "chain_reactor", "terminal_discharge"};
    for (const char* id : evolutionIds) if (ultimateEvolutions.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing ultimate evolution: ") + id; return false; }
    const std::array<const char*, 15> evolutionMetadataIds{{"solar_aftermath", "extinction_spear", "shattered_sky", "resonant_arsenal", "suppressive_grid", "execution_protocol", "brittle_singularity", "permafrost_engine", "cold_conductor", "event_horizon", "chrono_reversal", "mass_driver", "overdrive_link", "chain_reactor", "terminal_discharge"}};
    if (!loadMetadata(ultimateEvolutions, evolutionMetadataIds, parsed.evolutionMetadata)) {
        if (error) *error = "ultimate evolution metadata is incomplete";
        return false;
    }
    std::vector<int> evolutionCosts;
    if (!numbersForKey(ultimateEvolutions, "cost_legend_cores", evolutionCosts) || evolutionCosts.size() != 15) {
        if (error) *error = "ultimate evolution costs must define fifteen entries";
        return false;
    }
    for (std::size_t index = 0; index < evolutionCosts.size(); ++index) {
        if (evolutionCosts[index] <= 0) { if (error) *error = "ultimate evolution cost must be positive"; return false; }
        parsed.ultimateEvolutionCost[index] = static_cast<std::uint32_t>(evolutionCosts[index]);
    }
    std::uint32_t evolutionHash = 2166136261u;
    for (const unsigned char character : ultimateEvolutions) { evolutionHash ^= character; evolutionHash *= 16777619u; }
    parsed.ultimateEvolutionCatalogHash = evolutionHash == 0 ? 1u : evolutionHash;
    std::string ultimateModules;
    if (!readFile(std::filesystem::path(directory) / "ultimate_modules.json", ultimateModules, error)) return false;
    const std::array<const char*, 10> ultimateModuleIds{{"meteor_quick_charge", "meteor_overload", "bullet_suppressor", "bullet_focus", "zero_field", "zero_shatter", "gravity_well", "gravity_reversal", "surge_overdrive", "surge_discharge"}};
    if (!loadMetadata(ultimateModules, ultimateModuleIds, parsed.ultimateModuleMetadata)) {
        if (error) *error = "ultimate module metadata is incomplete";
        return false;
    }
    std::vector<int> moduleCosts;
    std::vector<float> moduleCooldownScales;
    std::vector<float> moduleDamageScales;
    if (!numbersForKey(ultimateModules, "cost_core_parts", moduleCosts) || !numbersForKey(ultimateModules, "cooldown_scale", moduleCooldownScales) || !numbersForKey(ultimateModules, "damage_scale", moduleDamageScales) || moduleCosts.size() != 10 || moduleCooldownScales.size() != 10 || moduleDamageScales.size() != 10) {
        if (error) *error = "ultimate modules must define ten complete numeric choices";
        return false;
    }
    for (std::size_t index = 0; index < 10; ++index) {
        if (moduleCosts[index] <= 0 || moduleCooldownScales[index] <= 0.0f || moduleDamageScales[index] <= 0.0f) { if (error) *error = "ultimate module tuning is outside safe ranges"; return false; }
        parsed.ultimateModuleCost[index] = static_cast<std::uint32_t>(moduleCosts[index]);
        parsed.ultimateModuleCooldownScale[index] = moduleCooldownScales[index];
        parsed.ultimateModuleDamageScale[index] = moduleDamageScales[index];
    }
    std::string synergies;
    if (!readFile(std::filesystem::path(directory) / "synergies.json", synergies, error)) return false;
    const std::array<const char*, 5> synergyIds{{"fire_wind", "ice_electric", "poison_teleport", "pierce_ricochet", "ultimate_evolutions"}};
    if (!loadMetadata(synergies, synergyIds, parsed.synergyMetadata)) {
        if (error) *error = "synergy metadata is incomplete";
        return false;
    }
    std::string dailyChallenges;
    if (!readFile(std::filesystem::path(directory) / "daily_challenges.json", dailyChallenges, error)) return false;
    const char* dailyIds[] = {"frozen_circuit", "last_shell", "swarm_protocol", "toxic_transit", "blackout", "no_safe_distance", "burning_economy"};
    for (const char* id : dailyIds) if (dailyChallenges.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing daily challenge: ") + id; return false; }
    std::uint32_t dailyHash = 2166136261u;
    for (const unsigned char character : dailyChallenges) { dailyHash ^= character; dailyHash *= 16777619u; }
    parsed.dailyChallengeCatalogHash = dailyHash == 0 ? 1u : dailyHash;
    std::string supportModules;
    if (!readFile(std::filesystem::path(directory) / "support_modules.json", supportModules, error)) return false;
    const char* supportIds[] = {"none", "credit_relay", "stasis_field", "repair_drones", "corrosion_amp"};
    for (const char* id : supportIds) if (supportModules.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing support module: ") + id; return false; }
    std::uint32_t supportHash = 2166136261u;
    for (const unsigned char character : supportModules) { supportHash ^= character; supportHash *= 16777619u; }
    parsed.supportModuleCatalogHash = supportHash == 0 ? 1u : supportHash;
    std::string statuses;
    std::string allies;
    std::string buildings;
    if (!readFile(std::filesystem::path(directory) / "statuses.json", statuses, error) ||
        !readFile(std::filesystem::path(directory) / "allies.json", allies, error) ||
        !readFile(std::filesystem::path(directory) / "buildings.json", buildings, error)) return false;
    const std::array<const char*, 5> statusIds{{"slow", "weakness", "stun", "burn", "shield"}};
    const std::array<const char*, 5> allyIds{{"soldier", "striker", "bulwark", "drone", "disruptor"}};
    const std::array<const char*, 4> buildingIds{{"barracks", "armory", "sentry", "mortar"}};
    for (const char* id : statusIds) if (statuses.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing status: ") + id; return false; }
    for (const char* id : allyIds) if (allies.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing ally: ") + id; return false; }
    for (const char* id : buildingIds) if (buildings.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing building: ") + id; return false; }
    std::array<ContentMetadata, 5> statusMetadata;
    std::array<ContentMetadata, 5> allyMetadata;
    std::array<ContentMetadata, 4> buildingMetadata;
    if (!loadMetadata(statuses, statusIds, statusMetadata) || !loadMetadata(allies, allyIds, allyMetadata) || !loadMetadata(buildings, buildingIds, buildingMetadata)) {
        if (error) *error = "skill entity metadata is incomplete";
        return false;
    }
    parsed.statusMetadata.assign(statusMetadata.begin(), statusMetadata.end());
    parsed.allyMetadata.assign(allyMetadata.begin(), allyMetadata.end());
    parsed.buildingMetadata.assign(buildingMetadata.begin(), buildingMetadata.end());
    const int authoredAlliedCap = integerForKey(allies, "max_allied_units", 0);
    const int authoredBuildingCap = integerForKey(buildings, "max_buildings", 0);
    const int authoredZoneCap = integerForKey(buildings, "max_skill_zones", 0);
    if (authoredAlliedCap <= 0 || authoredBuildingCap <= 0 || authoredZoneCap <= 0) { if (error) *error = "skill entity caps must be positive"; return false; }
    parsed.maxAlliedUnits = static_cast<std::size_t>(authoredAlliedCap);
    parsed.maxBuildings = static_cast<std::size_t>(authoredBuildingCap);
    parsed.maxSkillZones = static_cast<std::size_t>(authoredZoneCap);
    std::uint32_t entityHash = 2166136261u;
    for (const std::string& pack : {statuses, allies, buildings}) for (const unsigned char character : pack) { entityHash ^= character; entityHash *= 16777619u; }
    parsed.skillEntityCatalogHash = entityHash == 0 ? 1u : entityHash;
    std::string currencies;
    if (!readFile(std::filesystem::path(directory) / "currencies.json", currencies, error)) return false;
    const std::array<const char*, 4> currencyIds{{"credits", "core_parts", "shards", "legend_cores"}};
    if (!loadMetadata(currencies, currencyIds, parsed.currencyMetadata)) {
        if (error) *error = "currency metadata is incomplete";
        return false;
    }
    std::string workshop;
    if (!readFile(std::filesystem::path(directory) / "workshop.json", workshop, error)) return false;
    const std::array<const char*, 10> workshopIds{{"tower_core", "module_rapid_fire", "module_explosive_cannon", "module_arcane_beam", "module_frost_blaster", "module_sniper_railgun", "support_credit_relay", "support_stasis_field", "support_repair_drones", "support_corrosion_amp"}};
    if (!loadMetadata(workshop, workshopIds, parsed.workshopMetadata)) {
        if (error) *error = "workshop metadata is incomplete";
        return false;
    }
    std::vector<int> workshopBaseCost, workshopCostStep, workshopMaxLevel;
    if (!numbersForKey(workshop, "base_cost", workshopBaseCost) || !numbersForKey(workshop, "cost_step", workshopCostStep) || !numbersForKey(workshop, "max_level", workshopMaxLevel) || workshopBaseCost.size() != 10 || workshopCostStep.size() != 10 || workshopMaxLevel.size() != 10) {
        if (error) *error = "workshop content must define ten complete cost records";
        return false;
    }
    for (std::size_t index = 0; index < 10; ++index) {
        if (workshopBaseCost[index] <= 0 || workshopCostStep[index] <= 0 || workshopMaxLevel[index] <= 0) {
            if (error) *error = "workshop cost values are outside safe ranges";
            return false;
        }
        parsed.workshopBaseCost[index] = static_cast<std::uint32_t>(workshopBaseCost[index]);
        parsed.workshopCostStep[index] = static_cast<std::uint32_t>(workshopCostStep[index]);
        parsed.workshopMaxLevel[index] = static_cast<std::uint32_t>(workshopMaxLevel[index]);
    }
    std::string runTypes;
    if (!readFile(std::filesystem::path(directory) / "run_types.json", runTypes, error)) return false;
    const char* runIds[] = {"standard", "endless", "daily"};
    for (const char* id : runIds) if (runTypes.find(std::string("\"id\":\"") + id + "\"") == std::string::npos) { if (error) *error = std::string("missing run type: ") + id; return false; }
    std::vector<int> runMinutes, runLimits, runWorkshop;
    std::vector<float> runMultipliers;
    if (!numbersForKey(runTypes, "expected_minutes", runMinutes) || !numbersForKey(runTypes, "wave_limit", runLimits) || !numbersForKey(runTypes, "reward_multiplier", runMultipliers) || !numbersForKey(runTypes, "workshop_active", runWorkshop) || runMinutes.size() != 3 || runLimits.size() != 3 || runMultipliers.size() != 3 || runWorkshop.size() != 3) {
        if (error) *error = "run type content must define three complete records";
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        if (runMinutes[index] <= 0 || runLimits[index] < 0 || runMultipliers[index] <= 0.0f || (runWorkshop[index] != 0 && runWorkshop[index] != 1)) { if (error) *error = "run type values are outside safe ranges"; return false; }
        parsed.runExpectedMinutes[index] = runMinutes[index];
        parsed.runWaveLimit[index] = runLimits[index];
        parsed.runRewardMultiplier[index] = runMultipliers[index];
        parsed.runWorkshopActive[index] = runWorkshop[index];
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
    const std::array<const char*, 5> weaponIds{{"rapid_fire", "explosive_cannon", "arcane_beam", "frost_blaster", "sniper_railgun"}};
    const std::array<const char*, 15> upgradeIds{{"piercing_shots", "ricochet", "overclock", "cluster_bombs", "shockwave", "fireball_shells", "chain_lightning", "freezing_blast", "burning_shot", "black_hole", "emergency_repair", "scavenger", "wind_shear", "poison_coil", "steady_aim"}};
    const std::array<const char*, 5> ultimateIds{{"meteor_rain", "bullet_storm", "absolute_zero", "gravity_shift", "energy_surge"}};
    const std::array<const char*, 5> supportIdsForMetadata{{"none", "credit_relay", "stasis_field", "repair_drones", "corrosion_amp"}};
    const std::array<const char*, 4> authoredSkullIds{{"swarm", "glass_cannon", "haste", "greed"}};
    const std::array<const char*, 3> arenaIds{{"moonbase", "ember_crater", "neon_ruins"}};
    const std::array<const char*, 7> enemyIds{{"grunt", "runner", "tank", "shielded", "swarmling", "teleporter", "boss"}};
    if (!loadMetadata(weapons, weaponIds, parsed.weaponMetadata) || !loadMetadata(upgrades, upgradeIds, parsed.upgradeMetadata) ||
        !loadMetadata(ultimates, ultimateIds, parsed.ultimateMetadata) || !loadMetadata(supportModules, supportIdsForMetadata, parsed.supportMetadata) ||
        !loadMetadata(arenas, arenaIds, parsed.arenaMetadata) || !loadMetadata(enemies, enemyIds, parsed.enemyMetadata)) {
        if (error) *error = "content metadata is incomplete or missing required fields";
        return false;
    }
    std::array<ContentMetadata, 4> authoredSkullMetadata{};
    if (!loadMetadata(skulls, authoredSkullIds, authoredSkullMetadata)) {
        if (error) *error = "content metadata is incomplete or missing required skull fields";
        return false;
    }
    parsed.skullMetadata[0].id = "none";
    parsed.skullMetadata[0].display = "No Skull";
    parsed.skullMetadata[0].shortDescription = "No skull modifier.";
    parsed.skullMetadata[0].longDescription = "Play without an additional skull risk modifier.";
    parsed.skullMetadata[0].iconId = "skull.none";
    for (std::size_t index = 0; index < authoredSkullMetadata.size(); ++index) parsed.skullMetadata[index + 1] = authoredSkullMetadata[index];
    const std::array<const char*, 3> runTypeIds{{"standard", "endless", "daily"}};
    for (std::size_t index = 0; index < runTypeIds.size(); ++index) {
        parsed.runTypeMetadata[index] = runTypeMetadataForId(runTypes, runTypeIds[index]);
        const RunTypeMetadata& metadata = parsed.runTypeMetadata[index];
        if (metadata.id.empty() || metadata.display.empty() || metadata.description.empty() || metadata.rules.empty()) {
            if (error) *error = "run type metadata is incomplete";
            return false;
        }
    }
    std::string skills;
    if (!readFile(std::filesystem::path(directory) / "skills.json", skills, error)) return false;
    const std::array<const char*, static_cast<std::size_t>(SkillId::Count)> skillIds{{
        "gravity_well", "phase_mine", "vanguard_drop", "forward_barracks", "ruin_hex",
        "rally_beacon", "sentry_fabricator", "cryo_field", "drone_swarm", "resonance_pulse"
    }};
    const std::regex skillRecordPattern("\\{[^{}]*\\}");
    for (std::size_t index = 0; index < skillIds.size(); ++index) {
        bool found = false;
        for (std::sregex_iterator it(skills.begin(), skills.end(), skillRecordPattern), end; it != end; ++it) {
            const std::string record = (*it)[0].str();
            if (stringForKey(record, "id") != skillIds[index]) continue;
            SkillDefinition definition;
            definition.id = skillIds[index];
            definition.display = stringForKey(record, "display");
            definition.shortDescription = stringForKey(record, "short_description");
            definition.longDescription = stringForKey(record, "long_description");
            definition.iconId = stringForKey(record, "icon_id");
            definition.effect = stringForKey(record, "effect");
            definition.targetMode = stringForKey(record, "target_mode");
            definition.cooldownTicks = integerForKey(record, "cooldown_ticks", 90);
            definition.charges = integerForKey(record, "charges", 1);
            definition.durationTicks = integerForKey(record, "duration_ticks", 90);
            definition.range = floatForKey(record, "range", 700.0f);
            definition.radius = floatForKey(record, "radius", 100.0f);
            definition.valueA = floatForKey(record, "value_a", 1.0f);
            definition.valueB = floatForKey(record, "value_b", 1.0f);
            definition.tags = stringsForKey(record, "tags");
            definition.operations = stringsForKey(record, "operations");
            if (definition.display.empty() || definition.shortDescription.empty() || definition.longDescription.empty() || definition.iconId.empty() ||
                definition.effect.empty() || definition.targetMode.empty() || definition.cooldownTicks <= 0 || definition.charges <= 0 || definition.durationTicks <= 0 ||
                definition.range <= 0.0f || definition.radius <= 0.0f || definition.tags.empty()) {
                if (error) *error = std::string("invalid skill definition: ") + skillIds[index];
                return false;
            }
            parsed.skillDefinitions[index] = definition;
            found = true;
            break;
        }
        if (!found) { if (error) *error = std::string("missing skill: ") + skillIds[index]; return false; }
    }
    std::string skillTrees;
    if (!readFile(std::filesystem::path(directory) / "skill_trees.json", skillTrees, error)) return false;
    for (std::sregex_iterator it(skillTrees.begin(), skillTrees.end(), skillRecordPattern), end; it != end; ++it) {
        const std::string record = (*it)[0].str();
        SkillNodeDefinition node;
        node.id = stringForKey(record, "id");
        node.skillId = stringForKey(record, "skill_id");
        node.parentId = stringForKey(record, "parent_id");
        node.branchId = stringForKey(record, "branch_id");
        node.display = stringForKey(record, "display");
        node.description = stringForKey(record, "description");
        node.iconLayer = stringForKey(record, "icon_layer");
        node.tier = integerForKey(record, "tier", 1);
        node.maxRank = integerForKey(record, "max_rank", 1);
        node.cost = static_cast<std::uint32_t>(std::max(1, integerForKey(record, "cost", 25)));
        node.cooldownScale = floatForKey(record, "cooldown_scale", 1.0f);
        node.radiusScale = floatForKey(record, "radius_scale", 1.0f);
        node.valueScale = floatForKey(record, "value_scale", 1.0f);
        node.chargesDelta = integerForKey(record, "charges_delta", 0);
        if (node.id.empty() || node.skillId.empty() || node.branchId.empty() || node.display.empty() || node.description.empty() || node.iconLayer.empty() ||
            node.tier < 1 || node.maxRank < 1 || node.cooldownScale <= 0.0f || node.radiusScale <= 0.0f || node.valueScale <= 0.0f) {
            if (error) *error = "invalid skill tree node";
            return false;
        }
        bool knownSkill = false;
        for (const char* skillId : skillIds) if (node.skillId == skillId) knownSkill = true;
        if (!knownSkill) { if (error) *error = "skill tree node references unknown skill: " + node.skillId; return false; }
        parsed.skillNodes.push_back(node);
    }
    if (parsed.skillNodes.empty()) { if (error) *error = "skill tree content is empty"; return false; }
    std::set<std::string> nodeIds;
    for (const SkillNodeDefinition& node : parsed.skillNodes) {
        if (!nodeIds.insert(node.id).second) { if (error) *error = "duplicate skill tree node: " + node.id; return false; }
        if (!node.parentId.empty()) {
            const auto parent = std::find_if(parsed.skillNodes.begin(), parsed.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == node.parentId; });
            if (parent == parsed.skillNodes.end() || parent->skillId != node.skillId || parent->tier >= node.tier) { if (error) *error = "invalid skill tree parent for: " + node.id; return false; }
        }
    }
    for (const char* skillId : skillIds) {
        int genericNodes = 0;
        std::set<std::string> branches;
        int highestTier = 0;
        for (const SkillNodeDefinition& node : parsed.skillNodes) if (node.skillId == skillId) {
            if (node.parentId.empty() && node.tier == 1) ++genericNodes;
            if (node.tier >= 2) branches.insert(node.branchId);
            highestTier = std::max(highestTier, node.tier);
        }
        if (genericNodes == 0 || branches.size() < 2u || highestTier < 4) { if (error) *error = std::string("skill tree needs generic, two branches, and capstones: ") + skillId; return false; }
    }
    std::uint32_t skillHash = 2166136261u;
    const auto addSkillString = [&skillHash](const std::string& value) { for (const unsigned char character : value) { skillHash ^= character; skillHash *= 16777619u; } skillHash ^= 0u; skillHash *= 16777619u; };
    const auto addSkillInt = [&skillHash](int value) { skillHash ^= static_cast<std::uint32_t>(value); skillHash *= 16777619u; };
    for (const SkillDefinition& definition : parsed.skillDefinitions) {
        addSkillString(definition.id); addSkillString(definition.display); addSkillString(definition.shortDescription); addSkillString(definition.longDescription);
        addSkillString(definition.iconId); addSkillString(definition.effect); addSkillString(definition.targetMode);
        addSkillInt(definition.cooldownTicks); addSkillInt(definition.charges); addSkillInt(definition.durationTicks);
        addSkillString(std::to_string(definition.range)); addSkillString(std::to_string(definition.radius)); addSkillString(std::to_string(definition.valueA)); addSkillString(std::to_string(definition.valueB));
        for (const std::string& tag : definition.tags) addSkillString(tag);
        for (const std::string& operation : definition.operations) addSkillString(operation);
    }
    for (const SkillNodeDefinition& node : parsed.skillNodes) {
        addSkillString(node.id); addSkillString(node.skillId); addSkillString(node.parentId); addSkillString(node.branchId); addSkillString(node.display); addSkillString(node.description); addSkillString(node.iconLayer);
        addSkillInt(node.tier); addSkillInt(node.maxRank); addSkillInt(static_cast<int>(node.cost)); addSkillString(std::to_string(node.cooldownScale)); addSkillString(std::to_string(node.radiusScale)); addSkillString(std::to_string(node.valueScale)); addSkillInt(node.chargesDelta);
    }
    parsed.skillCatalogHash = skillHash == 0 ? 1u : skillHash;
    output = parsed;
    return true;
}

void GameSim::setContentConfig(const ContentConfig& config) {
    if (enemyList.empty() && (tickCount == 0 || gameOver || victory)) {
        content = config;
        const unsigned int moduleIndex = static_cast<unsigned int>(selectedUltimateModule);
        const float moduleScale = moduleIndex < 10u && moduleIndex / 2u == static_cast<unsigned int>(selectedUltimate) ? content.ultimateModuleCooldownScale[moduleIndex] : 1.0f;
        ultimateMaxCooldown = std::max(1, static_cast<int>(static_cast<float>(content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)]) * content.chassisUltimateCooldownScale[static_cast<std::size_t>(selectedChassis)] * moduleScale));
    }
}

} // namespace ta
