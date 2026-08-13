#include "daily.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>

namespace ta {
namespace {

struct DailyRecipeText {
    int themeIndex = -1;
    std::string title;
    std::string description;
    std::string longDescription;
    std::vector<std::string> themeTags;
    std::string loadoutRule;
    std::string enemySummary;
    std::vector<std::string> enemyRoster;
    std::vector<std::string> enemyPrevalence;
    std::vector<std::string> requiredSkills;
    std::vector<std::string> forbiddenSkills;
    std::vector<std::string> allowedSkillBranches;
    std::string threatSummary;
    std::string recommendedUpgradeTags;
    std::string modifierSummary;
    std::string modifierDescription;
    std::string objective;
    std::string requiredUltimate;
    std::string requiredWeapon;
    std::string requiredChassis;
    std::string requiredEvolution;
    std::string requiredSupport;
    std::string requiredSkull;
    std::string requiredArena;
    std::uint32_t extraSkullMask = 0;
    std::uint32_t bonusShards = 0;
    std::uint32_t legendCoreReward = 0;
    float waveBudgetScale = 1.0f;
    float enemyHealthScale = 1.0f;
    float enemySpeedScale = 1.0f;
    bool workshopNormalized = true;
};

std::string textField(const std::string& record, const char* key) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    return std::regex_search(record, match, pattern) ? match[1].str() : std::string();
}

std::uint32_t numberField(const std::string& record, const char* key, std::uint32_t fallback) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*([0-9]+)");
    if (!std::regex_search(record, match, pattern)) return fallback;
    try { return static_cast<std::uint32_t>(std::stoul(match[1].str())); } catch (...) { return fallback; }
}

bool booleanField(const std::string& record, const char* key, bool fallback) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(true|false)");
    if (!std::regex_search(record, match, pattern)) return fallback;
    return match[1].str() == "true";
}

float floatField(const std::string& record, const char* key, float fallback) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (!std::regex_search(record, match, pattern)) return fallback;
    try { return std::stof(match[1].str()); } catch (...) { return fallback; }
}

std::vector<std::string> stringArrayField(const std::string& record, const char* key) {
    std::smatch match;
    const std::regex arrayPattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    if (!std::regex_search(record, match, arrayPattern)) return {};
    std::vector<std::string> result;
    const std::regex valuePattern("\\\"([^\\\"]*)\\\"");
    const std::string values = match[1].str();
    for (std::sregex_iterator it(values.begin(), values.end(), valuePattern), end; it != end; ++it) result.push_back((*it)[1].str());
    return result;
}

std::vector<DailyRecipeText> loadDailyRecipes() {
    std::ifstream file(std::filesystem::path(defaultContentDirectory()) / "daily_challenges.json");
    if (!file) return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();
    std::vector<DailyRecipeText> recipes;
    const std::regex objectPattern("\\{[^{}]*\\}");
    for (std::sregex_iterator it(text.begin(), text.end(), objectPattern), end; it != end; ++it) {
        const std::string record = (*it)[0].str();
        if (record.find("\"theme_index\"") == std::string::npos) continue;
        DailyRecipeText recipe;
        recipe.themeIndex = static_cast<int>(numberField(record, "theme_index", 99));
        recipe.title = textField(record, "title");
        recipe.description = textField(record, "description");
        recipe.longDescription = textField(record, "long_description");
        recipe.themeTags = stringArrayField(record, "theme_tags");
        recipe.loadoutRule = textField(record, "loadout_rule");
        recipe.enemySummary = textField(record, "enemy_summary");
        recipe.enemyRoster = stringArrayField(record, "enemy_roster");
        recipe.enemyPrevalence = stringArrayField(record, "enemy_prevalence");
        recipe.requiredSkills = stringArrayField(record, "required_skills");
        recipe.forbiddenSkills = stringArrayField(record, "forbidden_skills");
        recipe.allowedSkillBranches = stringArrayField(record, "allowed_skill_branches");
        recipe.threatSummary = textField(record, "threat_summary");
        recipe.recommendedUpgradeTags = textField(record, "recommended_upgrade_tags");
        recipe.modifierSummary = textField(record, "modifier_summary");
        recipe.modifierDescription = textField(record, "modifier_description");
        recipe.objective = textField(record, "objective");
        recipe.requiredUltimate = textField(record, "required_ultimate");
        recipe.requiredWeapon = textField(record, "required_weapon");
        recipe.requiredChassis = textField(record, "required_chassis");
        recipe.requiredEvolution = textField(record, "required_evolution");
        recipe.requiredSupport = textField(record, "required_support");
        recipe.requiredSkull = textField(record, "required_skull");
        recipe.requiredArena = textField(record, "required_arena");
        recipe.extraSkullMask = numberField(record, "extra_skull_mask", 0);
        recipe.bonusShards = numberField(record, "bonus_shards", 0);
        recipe.legendCoreReward = numberField(record, "legend_core_reward", 0);
        recipe.waveBudgetScale = floatField(record, "wave_budget_scale", 1.0f);
        recipe.enemyHealthScale = floatField(record, "enemy_health_scale", 1.0f);
        recipe.enemySpeedScale = floatField(record, "enemy_speed_scale", 1.0f);
        recipe.workshopNormalized = booleanField(record, "workshop_normalized", true);
        if (recipe.themeIndex >= 0 && recipe.themeIndex < 7 && !recipe.title.empty() && !recipe.description.empty()) recipes.push_back(recipe);
    }
    return recipes;
}

const DailyRecipeText* recipeForTheme(int theme) {
    static const std::vector<DailyRecipeText> recipes = loadDailyRecipes();
    for (const DailyRecipeText& recipe : recipes) if (recipe.themeIndex == theme) return &recipe;
    return nullptr;
}

Ultimate ultimateFromId(const std::string& id) {
    if (id == "bullet_storm") return Ultimate::BulletStorm;
    if (id == "absolute_zero") return Ultimate::AbsoluteZero;
    if (id == "gravity_shift") return Ultimate::GravityShift;
    if (id == "energy_surge") return Ultimate::EnergySurge;
    return Ultimate::MeteorRain;
}

UltimateEvolution evolutionFromId(const std::string& id) {
    const std::array<std::string, 16> ids{{"none", "solar_aftermath", "extinction_spear", "shattered_sky", "resonant_arsenal", "suppressive_grid", "execution_protocol", "brittle_singularity", "permafrost_engine", "cold_conductor", "event_horizon", "chrono_reversal", "mass_driver", "overdrive_link", "chain_reactor", "terminal_discharge"}};
    for (std::size_t index = 0; index < ids.size(); ++index) if (ids[index] == id) return static_cast<UltimateEvolution>(index);
    return UltimateEvolution::None;
}

SupportModule supportFromId(const std::string& id) {
    if (id == "credit_relay") return SupportModule::CreditRelay;
    if (id == "stasis_field") return SupportModule::StasisField;
    if (id == "repair_drones") return SupportModule::RepairDrones;
    if (id == "corrosion_amp") return SupportModule::CorrosionAmp;
    return SupportModule::None;
}

Skull skullFromId(const std::string& id) {
    if (id == "glass_cannon") return Skull::GlassCannon;
    if (id == "haste") return Skull::Haste;
    if (id == "greed") return Skull::Greed;
    return Skull::Swarm;
}

Arena arenaFromId(const std::string& id) {
    if (id == "ember_crater") return Arena::EmberCrater;
    if (id == "neon_ruins") return Arena::NeonRuins;
    return Arena::Moonbase;
}

Weapon weaponFromId(const std::string& id) {
    if (id == "explosive_cannon") return Weapon::ExplosiveCannon;
    if (id == "arcane_beam") return Weapon::ArcaneBeam;
    if (id == "frost_blaster") return Weapon::FrostBlaster;
    if (id == "sniper_railgun") return Weapon::SniperRailgun;
    return Weapon::RapidFire;
}

TowerChassis chassisFromId(const std::string& id) {
    if (id == "bastion") return TowerChassis::Bastion;
    if (id == "catalyst") return TowerChassis::Catalyst;
    return TowerChassis::Vanguard;
}

EnemyType enemyFromId(const std::string& id) {
    if (id == "runner") return EnemyType::Runner;
    if (id == "tank") return EnemyType::Tank;
    if (id == "shielded") return EnemyType::Shielded;
    if (id == "swarmling") return EnemyType::Swarmling;
    if (id == "teleporter") return EnemyType::Teleporter;
    if (id == "boss") return EnemyType::Boss;
    return EnemyType::Grunt;
}

SkillId skillFromId(const std::string& id) {
    const std::array<std::pair<const char*, SkillId>, 10> skills{{
        {"gravity_well", SkillId::GravityWell}, {"phase_mine", SkillId::PhaseMine}, {"vanguard_drop", SkillId::VanguardDrop},
        {"forward_barracks", SkillId::ForwardBarracks}, {"ruin_hex", SkillId::RuinHex}, {"rally_beacon", SkillId::RallyBeacon},
        {"sentry_fabricator", SkillId::SentryFabricator}, {"cryo_field", SkillId::CryoField}, {"drone_swarm", SkillId::DroneSwarm}, {"resonance_pulse", SkillId::ResonancePulse}
    }};
    for (const auto& entry : skills) if (id == entry.first) return entry.second;
    return SkillId::Count;
}

std::uint32_t previousDateKey(std::uint32_t dateKey) {
    const unsigned int year = dateKey / 10000u;
    const unsigned int month = (dateKey / 100u) % 100u;
    const unsigned int day = dateKey % 100u;
    if (day > 1u) return dateKey - 1u;
    const unsigned int previousMonth = month == 1u ? 12u : month - 1u;
    const unsigned int previousYear = month == 1u ? year - 1u : year;
    const unsigned int monthLengths[] = {0u,31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u};
    unsigned int previousDay = monthLengths[previousMonth];
    if (previousMonth == 2u && (previousYear % 4u == 0u && (previousYear % 100u != 0u || previousYear % 400u == 0u))) previousDay = 29u;
    return previousYear * 10000u + previousMonth * 100u + previousDay;
}

std::uint32_t hashForDate(std::uint32_t dateKey) {
    std::uint32_t hash = dateKey ^ 0xA53C9E17u;
    hash ^= hash >> 16; hash *= 2246822519u; hash ^= hash >> 13; hash *= 3266489917u; hash ^= hash >> 16;
    return hash == 0u ? 1u : hash;
}

int themeForDate(std::uint32_t dateKey) {
    int theme = static_cast<int>(hashForDate(dateKey) % 7u);
    if (dateKey == 0u) return theme;
    const std::uint32_t previousDate = previousDateKey(dateKey);
    const std::uint32_t previousPreviousDate = previousDateKey(previousDate);
    int previousTheme = static_cast<int>(hashForDate(previousDate) % 7u);
    if (previousTheme == static_cast<int>(hashForDate(previousPreviousDate) % 7u)) previousTheme = (previousTheme + 1) % 7;
    if (theme == previousTheme) theme = (theme + 1) % 7;
    return theme;
}

} // namespace

DailyChallenge challengeForDate(std::uint32_t dateKey) {
    const std::uint32_t hash = hashForDate(dateKey);
    DailyChallenge result;
    result.dateKey = dateKey;
    result.seed = hash == 0 ? 1u : hash;
    const int theme = themeForDate(dateKey);
    result.recommendedWeapon = static_cast<Weapon>((hash / 7u) % 5u);
    result.skull = static_cast<Skull>(1 + (hash / 5u) % 4u);
    result.skullMask = static_cast<SkullMask>(1u << static_cast<unsigned int>(result.skull));
    result.arena = static_cast<Arena>((hash / 19u) % 3u);
    if (dateKey != 0u) {
        const std::uint32_t previousHash = hashForDate(previousDateKey(dateKey));
        if (result.recommendedWeapon == static_cast<Weapon>((previousHash / 7u) % 5u)) result.recommendedWeapon = static_cast<Weapon>((static_cast<unsigned int>(result.recommendedWeapon) + 1u) % 5u);
        if (result.skull == static_cast<Skull>(1 + (previousHash / 5u) % 4u)) result.skull = static_cast<Skull>(1 + (static_cast<unsigned int>(result.skull) % 4u));
        if (result.arena == static_cast<Arena>((previousHash / 19u) % 3u)) result.arena = static_cast<Arena>((static_cast<unsigned int>(result.arena) + 1u) % 3u);
        result.skullMask = static_cast<SkullMask>(1u << static_cast<unsigned int>(result.skull));
    }
    result.bonusShards = 20u + theme * 5u;
    result.legendCoreReward = 1u + (theme == 2u || theme == 5u ? 1u : 0u);
    if (const DailyRecipeText* recipe = recipeForTheme(static_cast<int>(theme)); recipe != nullptr) {
        result.title = recipe->title;
        result.description = recipe->description;
        result.longDescription = recipe->longDescription.empty() ? recipe->description : recipe->longDescription;
        result.themeTags = recipe->themeTags;
        result.loadoutRule = recipe->loadoutRule;
        result.enemySummary = recipe->enemySummary;
        for (const std::string& id : recipe->enemyRoster) result.enemyRoster.push_back(enemyFromId(id));
        result.enemyPrevalence = recipe->enemyPrevalence;
        for (const std::string& id : recipe->requiredSkills) { const SkillId skill = skillFromId(id); if (skill != SkillId::Count) result.requiredSkills.push_back(skill); }
        for (const std::string& id : recipe->forbiddenSkills) { const SkillId skill = skillFromId(id); if (skill != SkillId::Count) result.forbiddenSkills.push_back(skill); }
        result.allowedSkillBranches = recipe->allowedSkillBranches;
        result.threatSummary = recipe->threatSummary;
        result.recommendedUpgradeTags = recipe->recommendedUpgradeTags;
        result.modifierSummary = recipe->modifierSummary;
        result.modifierDescription = recipe->modifierDescription.empty() ? recipe->modifierSummary : recipe->modifierDescription;
        result.objective = recipe->objective;
        result.requiredUltimate = ultimateFromId(recipe->requiredUltimate);
        if (!recipe->requiredChassis.empty() && recipe->requiredChassis != "none") {
            result.requiredChassis = chassisFromId(recipe->requiredChassis);
            result.chassisRequired = true;
        }
        if (!recipe->requiredWeapon.empty() && recipe->requiredWeapon != "none") {
            result.requiredWeapon = weaponFromId(recipe->requiredWeapon);
            result.recommendedWeapon = result.requiredWeapon;
            result.weaponRequired = true;
        }
        result.requiredEvolution = evolutionFromId(recipe->requiredEvolution);
        result.requiredSupport = supportFromId(recipe->requiredSupport);
        result.skull = skullFromId(recipe->requiredSkull);
        result.skullMask = static_cast<SkullMask>(1u << static_cast<unsigned int>(result.skull));
        result.arena = arenaFromId(recipe->requiredArena);
        result.skullMask = static_cast<SkullMask>(result.skullMask | recipe->extraSkullMask);
        result.bonusShards = recipe->bonusShards;
        result.legendCoreReward = recipe->legendCoreReward;
        result.waveBudgetScale = recipe->waveBudgetScale;
        result.enemyHealthScale = recipe->enemyHealthScale;
        result.enemySpeedScale = recipe->enemySpeedScale;
        result.workshopNormalized = recipe->workshopNormalized;
    }
    if (result.requiredSkills.empty()) {
        // Every daily has a distinct tactical loan. The player can still use
        // four personal skills around it, so this is a constraint, not a
        // forced full build.
        static constexpr std::array<SkillId, 7> dailySkills{{SkillId::CryoField, SkillId::RuinHex, SkillId::DroneSwarm, SkillId::PhaseMine, SkillId::GravityWell, SkillId::VanguardDrop, SkillId::SentryFabricator}};
        result.requiredSkills.push_back(dailySkills[static_cast<std::size_t>(theme)]);
    }
    if (result.allowedSkillBranches.empty()) {
        static constexpr std::array<const char*, 7> branches{{"cryo_field:permafrost", "ruin_hex:withering", "drone_swarm:disruptor", "phase_mine:phase_snare", "gravity_well:event_horizon", "vanguard_drop:bulwark", "sentry_fabricator:mortar"}};
        result.allowedSkillBranches.push_back(branches[static_cast<std::size_t>(theme)]);
    }
    result.skillSummary = "REQUIRED SKILL // ";
    for (std::size_t index = 0; index < result.requiredSkills.size(); ++index) {
        if (index > 0) result.skillSummary += " + ";
        result.skillSummary += skillName(result.requiredSkills[index]);
    }
    result.skullSummary = "SKULLS: ";
    result.skullSummary += skullName(result.skull);
    if ((result.skullMask & (1u << static_cast<unsigned int>(Skull::GlassCannon))) != 0u && result.skull != Skull::GlassCannon) {
        result.skullSummary += " + ";
        result.skullSummary += skullName(Skull::GlassCannon);
    }
    return result;
}

ContentConfig contentForDailyChallenge(const ContentConfig& authored, const DailyChallenge& challenge) {
    ContentConfig daily = authored;
    const float budgetScale = std::max(0.25f, challenge.waveBudgetScale);
    for (std::size_t wave = 0; wave < 9; ++wave) {
        daily.waveEnemyBudget[wave] = std::max(1, static_cast<int>(std::ceil(static_cast<float>(authored.waveEnemyBudget[wave]) * budgetScale)));
        if (challenge.enemyRoster.empty()) continue;
        for (float& weight : daily.waveEnemyTypeWeight[wave]) weight = 0.0f;
        const float weight = 100.0f / static_cast<float>(challenge.enemyRoster.size());
        for (const EnemyType enemy : challenge.enemyRoster) {
            if (enemy != EnemyType::Boss) daily.waveEnemyTypeWeight[wave][static_cast<std::size_t>(enemy)] = weight;
        }
    }
    const float healthScale = std::max(0.25f, challenge.enemyHealthScale);
    const float speedScale = std::max(0.25f, challenge.enemySpeedScale);
    for (const EnemyType enemy : challenge.enemyRoster) {
        const std::size_t index = static_cast<std::size_t>(enemy);
        if (enemy != EnemyType::Boss) {
            daily.enemyHealthScale[index] *= healthScale;
            daily.enemySpeedScale[index] *= speedScale;
        }
    }
    return daily;
}

DailyChallenge currentDailyChallenge() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    const std::uint32_t dateKey = static_cast<std::uint32_t>((utc.tm_year + 1900) * 10000 + (utc.tm_mon + 1) * 100 + utc.tm_mday);
    return challengeForDate(dateKey);
}

} // namespace ta
