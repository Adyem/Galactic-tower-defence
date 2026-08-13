#pragma once

#include "game.hpp"
#include "input.hpp"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace ta {

enum class ReplayAction : std::uint8_t { Upgrade = 1, Ultimate = 2, Reroll = 3, SkillCast = 4 };

struct ReplayEvent {
    std::uint32_t tick = 0;
    ReplayAction action = ReplayAction::Upgrade;
    std::uint8_t value = 0;
    std::uint8_t slot = 0;
    SkillId skill = SkillId::GravityWell;
    TargetSpec target{};
    std::uint32_t sequence = 0;
};

struct ReplayData {
    std::uint32_t seed = 1;
    Weapon weapon = Weapon::RapidFire;
    TowerChassis chassis = TowerChassis::Vanguard;
    SupportModule support = SupportModule::None;
    Skull skull = Skull::None;
    SkullMask skullMask = 0;
    Ultimate ultimate = Ultimate::MeteorRain;
    UltimateEvolution evolution = UltimateEvolution::None;
    UltimateModule ultimateModule = static_cast<UltimateModule>(255);
    bool autoUltimate = false;
    Arena arena = Arena::Moonbase;
    std::vector<ReplayEvent> events;
    // Zero means a legacy/default-content replay. Non-zero binds playback to
    // the authored content fingerprint used by the client.
    std::uint32_t contentHash = 0;
    bool endless = false;
    // Non-zero binds the replay to the deterministic Daily recipe for that UTC date.
    std::uint32_t dailyDateKey = 0;
    SkillLoadout skillLoadout{};

    std::string serialize() const;
    static bool deserialize(const std::string& text, ReplayData& output, std::string* error = nullptr);
    bool save(const std::string& path, std::string* error = nullptr) const;
    static bool load(const std::string& path, ReplayData& output, std::string* error = nullptr);
};

// Replays a bounded command stream through the deterministic simulation and
// returns the final state hash. This is used by tests and can later power a
// replay viewer without exposing networking concerns to the simulation.
bool replayFinalHash(const ReplayData& replay, std::uint32_t ticks, std::uint32_t& hash, SimStats* finalStats = nullptr, std::string* error = nullptr);

struct ProfileData {
    std::uint32_t version = 15;
    int bestScore = 0;
    int bestWave = 0;
    std::uint32_t runsCompleted = 0;
    std::uint32_t totalKills = 0;
    bool reducedFlashes = false;
    bool highContrast = false;
    std::uint8_t masterVolume = 100;
    std::uint8_t musicVolume = 100;
    std::uint8_t sfxVolume = 100;
    std::uint8_t uiVolume = 100;
    std::uint8_t uiScalePercent = 100;
    std::uint8_t colorBlindPalette = 0;
    bool subtitles = true;
    bool vibration = true;
    InputBindings inputBindings = defaultInputBindings();
    std::uint32_t cosmeticShards = 0;
    std::uint32_t coreParts = 0;
    std::uint32_t legendCores = 0;
    std::vector<std::uint32_t> claimedDailyChallenges;
    std::array<std::uint8_t, 5> ultimateMasteryRuns{{0, 0, 0, 0, 0}};
    std::uint8_t towerCoreLevel = 0;
    std::array<std::uint8_t, 5> weaponModuleLevels{{0, 0, 0, 0, 0}};
    std::array<std::uint8_t, 5> supportModuleLevels{{0, 0, 0, 0, 0}};
    std::uint32_t unlockedUltimateEvolutionsMask = 0;
    std::uint8_t equippedUltimateEvolution = 0;
    std::uint16_t unlockedUltimateModulesMask = 0;
    std::uint8_t equippedUltimateModule = 255;
    std::uint8_t equippedSupportModule = 0;
    std::uint8_t equippedChassis = 0;
    std::uint8_t equippedWeapon = 0;
    std::uint8_t equippedUltimate = 0;
    std::uint32_t unlockedSkillsMask = 0x1Fu;
    std::vector<std::string> unlockedSkillNodes;
    SkillLoadout skillLoadout{};
    std::array<SkillLoadout, 3> skillPresets{};
    std::array<std::string, 3> skillPresetNames{{"BALANCED", "CONTROL", "SUMMONER"}};
    std::uint32_t unlockedSkinsMask = 1u;
    std::uint8_t equippedSkin = 0;
};

bool isSkinUnlocked(const ProfileData& profile, TowerSkin skin);
bool unlockSkin(ProfileData& profile, TowerSkin skin);
bool equipSkin(ProfileData& profile, TowerSkin skin);
void awardRunCosmetics(ProfileData& profile, const SimStats& stats, bool dailyRun, std::uint32_t dailyBonusShards = 20);
std::uint32_t awardRunProgression(ProfileData& profile, const SimStats& stats, bool dailyRun, Ultimate ultimate = Ultimate::MeteorRain);
bool hasClaimedDaily(const ProfileData& profile, std::uint32_t dateKey);
bool claimDailyLegendCores(ProfileData& profile, std::uint32_t dateKey, std::uint32_t reward);
std::uint32_t workshopTowerCost(const ProfileData& profile);
std::uint32_t workshopModuleCost(const ProfileData& profile, Weapon weapon);
std::uint32_t workshopSupportCost(const ProfileData& profile, SupportModule support);
std::uint32_t workshopTowerCost(const ProfileData& profile, const ContentConfig& content);
std::uint32_t workshopModuleCost(const ProfileData& profile, Weapon weapon, const ContentConfig& content);
std::uint32_t workshopSupportCost(const ProfileData& profile, SupportModule support, const ContentConfig& content);
bool purchaseTowerCore(ProfileData& profile);
bool purchaseWeaponModule(ProfileData& profile, Weapon weapon);
bool purchaseSupportModule(ProfileData& profile, SupportModule support);
bool purchaseTowerCore(ProfileData& profile, const ContentConfig& content);
bool purchaseWeaponModule(ProfileData& profile, Weapon weapon, const ContentConfig& content);
bool purchaseSupportModule(ProfileData& profile, SupportModule support, const ContentConfig& content);
bool isUltimateEvolutionUnlocked(const ProfileData& profile, UltimateEvolution evolution);
bool unlockUltimateEvolution(ProfileData& profile, UltimateEvolution evolution);
bool unlockUltimateEvolution(ProfileData& profile, UltimateEvolution evolution, const ContentConfig& content);
bool equipUltimateEvolution(ProfileData& profile, UltimateEvolution evolution);
bool isUltimateModuleUnlocked(const ProfileData& profile, UltimateModule module);
bool unlockUltimateModule(ProfileData& profile, UltimateModule module, const ContentConfig& content);
bool equipUltimateModule(ProfileData& profile, UltimateModule module);
bool isSkillUnlocked(const ProfileData& profile, SkillId skill);
bool unlockSkill(ProfileData& profile, SkillId skill, const ContentConfig& content);
std::uint32_t skillNodeCost(const ProfileData& profile, const SkillNodeDefinition& node);
bool purchaseSkillNode(ProfileData& profile, const SkillNodeDefinition& node, const ContentConfig& content);
int purchasedSkillNodeRank(const ProfileData& profile, const std::string& nodeId);
bool equipSkillBuild(ProfileData& profile, std::size_t slot, const std::string& build, const ContentConfig& content);
bool equipSkillNode(ProfileData& profile, std::size_t slot, const std::string& nodeId, const ContentConfig& content);
bool equipSkill(ProfileData& profile, std::size_t slot, SkillId skill);
bool saveSkillPreset(ProfileData& profile, std::size_t preset);
bool equipSkillPreset(ProfileData& profile, std::size_t preset);

bool saveProfile(const std::string& path, const ProfileData& profile, std::string* error = nullptr);
bool loadProfile(const std::string& path, ProfileData& profile, std::string* error = nullptr);

} // namespace ta
