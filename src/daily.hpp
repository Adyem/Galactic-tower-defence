#pragma once

#include "game.hpp"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace ta {

struct DailyChallenge {
    std::uint32_t dateKey = 0;
    std::uint32_t seed = 1;
    Weapon recommendedWeapon = Weapon::RapidFire;
    Weapon requiredWeapon = Weapon::RapidFire;
    bool weaponRequired = false;
    TowerChassis requiredChassis = TowerChassis::Vanguard;
    bool chassisRequired = false;
    Ultimate requiredUltimate = Ultimate::MeteorRain;
    UltimateEvolution requiredEvolution = UltimateEvolution::None;
    SupportModule requiredSupport = SupportModule::None;
    Skull skull = Skull::Swarm;
    SkullMask skullMask = 1u << static_cast<unsigned int>(Skull::Swarm);
    Arena arena = Arena::Moonbase;
    std::uint32_t bonusShards = 20;
    std::uint32_t legendCoreReward = 1;
    std::string title = "DAILY PROTOCOL";
    std::string description = "A deterministic tactical challenge.";
    std::string longDescription = "A deterministic tactical challenge with a hand-authored threat profile.";
    std::vector<std::string> themeTags;
    std::string loadoutRule = "RECOMMENDED LOADOUT";
    std::string skullSummary = "SWARM";
    std::string enemySummary = "STANDARD ENEMY MIX";
    std::vector<EnemyType> enemyRoster;
    std::vector<std::string> enemyPrevalence;
    std::vector<SkillId> requiredSkills;
    std::vector<SkillId> forbiddenSkills;
    std::vector<std::string> allowedSkillBranches;
    std::string skillSummary = "OPEN SKILL LOADOUT";
    std::string threatSummary = "STANDARD THREAT PROFILE";
    std::string recommendedUpgradeTags = "ADAPT TO THE THREAT";
    std::string modifierSummary = "ONE SKULL ACTIVE";
    std::string modifierDescription = "The daily modifier changes the normal threat profile.";
    std::string objective = "CLEAR ALL WAVES";
    float waveBudgetScale = 1.0f;
    float enemyHealthScale = 1.0f;
    float enemySpeedScale = 1.0f;
    bool workshopNormalized = true;
};

DailyChallenge challengeForDate(std::uint32_t dateKey);
DailyChallenge currentDailyChallenge();
ContentConfig contentForDailyChallenge(const ContentConfig& authored, const DailyChallenge& challenge);

} // namespace ta
