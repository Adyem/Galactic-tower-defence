#include "profile.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ta {
namespace {

bool parseUnsigned(const std::string& value, std::uint32_t& output) {
    try {
        std::size_t used = 0;
        const unsigned long parsed = std::stoul(value, &used);
        if (used != value.size()) return false;
        output = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) { return false; }
}

bool parseInt(const std::string& value, int& output) {
    try {
        std::size_t used = 0;
        const long parsed = std::stol(value, &used);
        if (used != value.size()) return false;
        output = static_cast<int>(parsed);
        return true;
    } catch (...) { return false; }
}

bool atomicWrite(const std::string& path, const std::string& contents, std::string* error) {
    const std::filesystem::path destination(path);
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) { if (error) *error = "unable to open temporary save"; return false; }
    file << contents;
    file.flush();
    if (!file) { if (error) *error = "unable to write save"; return false; }
    file.close();
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(temporary, destination, ec);
    }
    if (ec) { if (error) *error = "unable to commit save: " + ec.message(); return false; }
    return true;
}

} // namespace

std::string ReplayData::serialize() const {
    std::ostringstream stream;
    stream << "TA_REPLAY 1\n";
    stream << "seed " << seed << "\nweapon " << static_cast<int>(weapon) << "\nskull " << static_cast<int>(skull) << "\nskull_mask " << static_cast<unsigned int>(skullMask) << "\nultimate " << static_cast<int>(ultimate) << "\nauto_ultimate " << (autoUltimate ? 1 : 0) << "\narena " << static_cast<int>(arena) << "\n";
    if (contentHash != 0) stream << "content_hash " << contentHash << "\n";
    for (const ReplayEvent& event : events) stream << "event " << event.tick << ' ' << static_cast<int>(event.action) << ' ' << static_cast<int>(event.value) << "\n";
    return stream.str();
}

bool ReplayData::deserialize(const std::string& text, ReplayData& output, std::string* error) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line) || line != "TA_REPLAY 1") { if (error) *error = "invalid replay header"; return false; }
    ReplayData parsed;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag.empty()) continue;
        if (tag == "seed") {
            std::string value; fields >> value;
            if (!parseUnsigned(value, parsed.seed)) { if (error) *error = "invalid replay seed"; return false; }
        } else if (tag == "weapon") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Weapon::SniperRailgun)) { if (error) *error = "invalid replay weapon"; return false; }
            parsed.weapon = static_cast<Weapon>(value);
        } else if (tag == "skull") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Skull::Greed)) { if (error) *error = "invalid replay skull"; return false; }
            parsed.skull = static_cast<Skull>(value);
            parsed.skullMask = value == 0 ? 0 : static_cast<SkullMask>(1u << value);
        } else if (tag == "skull_mask") {
            unsigned int value = 0; fields >> value;
            const unsigned int valid = (1u << (static_cast<unsigned int>(Skull::Greed) + 1u)) - 2u;
            if (fields.fail() || value > valid) { if (error) *error = "invalid replay skull mask"; return false; }
            parsed.skullMask = static_cast<SkullMask>(value);
        } else if (tag == "ultimate") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Ultimate::EnergySurge)) { if (error) *error = "invalid replay ultimate"; return false; }
            parsed.ultimate = static_cast<Ultimate>(value);
        } else if (tag == "auto_ultimate") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > 1) { if (error) *error = "invalid replay auto ultimate flag"; return false; }
            parsed.autoUltimate = value != 0;
        } else if (tag == "arena") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Arena::NeonRuins)) { if (error) *error = "invalid replay arena"; return false; }
            parsed.arena = static_cast<Arena>(value);
        } else if (tag == "content_hash") {
            std::string value;
            fields >> value;
            if (!parseUnsigned(value, parsed.contentHash) || parsed.contentHash == 0) { if (error) *error = "invalid replay content hash"; return false; }
        } else if (tag == "event") {
            std::uint32_t tick = 0; int action = 0; int value = 0;
            if (!(fields >> tick >> action >> value) || tick == 0 || action < 1 || action > 2 || value < 0 || value > 255) { if (error) *error = "invalid replay event"; return false; }
            parsed.events.push_back({tick, static_cast<ReplayAction>(action), static_cast<std::uint8_t>(value)});
        } else {
            if (error) *error = "unknown replay record: " + tag;
            return false;
        }
    }
    output = parsed;
    return true;
}

bool ReplayData::save(const std::string& path, std::string* error) const { return atomicWrite(path, serialize(), error); }

bool ReplayData::load(const std::string& path, ReplayData& output, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { if (error) *error = "unable to open replay"; return false; }
    std::ostringstream contents; contents << file.rdbuf();
    return deserialize(contents.str(), output, error);
}

bool replayFinalHash(const ReplayData& replay, std::uint32_t ticks, std::uint32_t& hash, SimStats* finalStats, std::string* error) {
    GameSim simulation(replay.seed);
    if (replay.contentHash != 0) {
        ContentConfig authored;
        std::string contentError;
        const std::string contentDirectory = defaultContentDirectory();
        if (!loadContentConfig(contentDirectory, authored, &contentError)) {
            if (error) *error = "unable to load replay content: " + contentError;
            return false;
        }
        if (contentFingerprint(authored) != replay.contentHash) {
            if (error) *error = "replay content hash does not match installed authored content";
            return false;
        }
        simulation.setContentConfig(authored);
    }
    simulation.setWeapon(replay.weapon);
    if (replay.skullMask != 0) simulation.setSkullMask(replay.skullMask);
    else simulation.setSkull(replay.skull);
    simulation.setUltimate(replay.ultimate);
    simulation.setAutoUltimate(replay.autoUltimate);
    simulation.setArena(replay.arena);
    simulation.reset(replay.seed);
    std::size_t eventIndex = 0;
    for (std::uint32_t tick = 1; tick <= ticks && !simulation.isGameOver() && !simulation.isVictory(); ++tick) {
        while (eventIndex < replay.events.size() && replay.events[eventIndex].tick == tick) {
            const ReplayEvent& event = replay.events[eventIndex++];
            if (event.action == ReplayAction::Upgrade) simulation.chooseUpgrade(event.value);
            else if (event.action == ReplayAction::Ultimate) simulation.activateUltimate();
        }
        simulation.tick();
    }
    hash = simulation.stateHash();
    if (finalStats) *finalStats = simulation.stats();
    return eventIndex == replay.events.size();
}

bool saveProfile(const std::string& path, const ProfileData& profile, std::string* error) {
    std::ostringstream stream;
    stream << "TA_PROFILE 3\n"
           << "best_score " << profile.bestScore << "\n"
           << "best_wave " << profile.bestWave << "\n"
           << "runs_completed " << profile.runsCompleted << "\n"
           << "total_kills " << profile.totalKills << "\n"
           << "reduced_flashes " << (profile.reducedFlashes ? 1 : 0) << "\n";
    stream << "cosmetic_shards " << profile.cosmeticShards << "\n"
           << "unlocked_skins " << profile.unlockedSkinsMask << "\n"
           << "equipped_skin " << static_cast<unsigned int>(profile.equippedSkin) << "\n"
           << "high_contrast " << (profile.highContrast ? 1 : 0) << "\n"
           << "master_volume " << static_cast<unsigned int>(profile.masterVolume) << "\n";
    return atomicWrite(path, stream.str(), error);
}

bool loadProfile(const std::string& path, ProfileData& profile, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { if (error) *error = "profile does not exist"; return false; }
    std::string line;
    if (!std::getline(file, line) || (line != "TA_PROFILE 1" && line != "TA_PROFILE 2" && line != "TA_PROFILE 3")) { if (error) *error = "invalid profile header"; return false; }
    ProfileData parsed;
    parsed.version = line == "TA_PROFILE 3" ? 3u : (line == "TA_PROFILE 2" ? 2u : 1u);
    while (std::getline(file, line)) {
        std::istringstream fields(line); std::string tag, value; fields >> tag >> value;
        if (tag.empty()) continue;
        if (tag == "best_score") { if (!parseInt(value, parsed.bestScore)) return false; }
        else if (tag == "best_wave") { if (!parseInt(value, parsed.bestWave)) return false; }
        else if (tag == "runs_completed") { if (!parseUnsigned(value, parsed.runsCompleted)) return false; }
        else if (tag == "total_kills") { if (!parseUnsigned(value, parsed.totalKills)) return false; }
        else if (tag == "reduced_flashes") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.reducedFlashes = flag != 0; }
        else if (tag == "cosmetic_shards") { if (!parseUnsigned(value, parsed.cosmeticShards)) return false; }
        else if (tag == "unlocked_skins") { if (!parseUnsigned(value, parsed.unlockedSkinsMask) || (parsed.unlockedSkinsMask & 1u) == 0) return false; }
        else if (tag == "equipped_skin") { std::uint32_t skin = 0; if (!parseUnsigned(value, skin) || skin > static_cast<std::uint32_t>(TowerSkin::Gold) || (parsed.unlockedSkinsMask & (1u << skin)) == 0) return false; parsed.equippedSkin = static_cast<std::uint8_t>(skin); }
        else if (tag == "high_contrast") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.highContrast = flag != 0; }
        else if (tag == "master_volume") { std::uint32_t volume = 0; if (!parseUnsigned(value, volume) || volume > 100) return false; parsed.masterVolume = static_cast<std::uint8_t>(volume); }
        else { if (error) *error = "unknown profile record: " + tag; return false; }
    }
    profile = parsed;
    return true;
}

bool isSkinUnlocked(const ProfileData& profile, TowerSkin skin) {
    const auto bit = 1u << static_cast<unsigned int>(skin);
    return (profile.unlockedSkinsMask & bit) != 0;
}

bool unlockSkin(ProfileData& profile, TowerSkin skin) {
    const auto bit = 1u << static_cast<unsigned int>(skin);
    if ((profile.unlockedSkinsMask & bit) != 0) return true;
    constexpr std::uint32_t cost = 100;
    if (profile.cosmeticShards < cost) return false;
    profile.cosmeticShards -= cost;
    profile.unlockedSkinsMask |= bit;
    return true;
}

bool equipSkin(ProfileData& profile, TowerSkin skin) {
    if (!isSkinUnlocked(profile, skin)) return false;
    profile.equippedSkin = static_cast<std::uint8_t>(skin);
    return true;
}

void awardRunCosmetics(ProfileData& profile, const SimStats& stats, bool dailyRun, std::uint32_t dailyBonusShards) {
    profile.cosmeticShards += static_cast<std::uint32_t>(stats.kills / 8 + (stats.wave >= 10 ? 40 : stats.wave * 2) + (dailyRun ? dailyBonusShards : 0));
}

} // namespace ta
