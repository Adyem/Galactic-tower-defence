#pragma once

#include "game.hpp"
#include "input.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ta {

enum class ReplayAction : std::uint8_t { Upgrade = 1, Ultimate = 2 };

struct ReplayEvent {
    std::uint32_t tick = 0;
    ReplayAction action = ReplayAction::Upgrade;
    std::uint8_t value = 0;
};

struct ReplayData {
    std::uint32_t seed = 1;
    Weapon weapon = Weapon::RapidFire;
    Skull skull = Skull::None;
    SkullMask skullMask = 0;
    Ultimate ultimate = Ultimate::MeteorRain;
    bool autoUltimate = false;
    Arena arena = Arena::Moonbase;
    std::vector<ReplayEvent> events;
    // Zero means a legacy/default-content replay. Non-zero binds playback to
    // the authored content fingerprint used by the client.
    std::uint32_t contentHash = 0;

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
    std::uint32_t version = 6;
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
    std::uint32_t unlockedSkinsMask = 1u;
    std::uint8_t equippedSkin = 0;
};

bool isSkinUnlocked(const ProfileData& profile, TowerSkin skin);
bool unlockSkin(ProfileData& profile, TowerSkin skin);
bool equipSkin(ProfileData& profile, TowerSkin skin);
void awardRunCosmetics(ProfileData& profile, const SimStats& stats, bool dailyRun, std::uint32_t dailyBonusShards = 20);

bool saveProfile(const std::string& path, const ProfileData& profile, std::string* error = nullptr);
bool loadProfile(const std::string& path, ProfileData& profile, std::string* error = nullptr);

} // namespace ta
