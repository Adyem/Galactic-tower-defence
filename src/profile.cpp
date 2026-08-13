#include "profile.hpp"
#include "daily.hpp"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#endif

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

SkillId parseSkillId(const std::string& value) {
    std::uint32_t numeric = 0;
    if (parseUnsigned(value, numeric) && numeric < static_cast<std::uint32_t>(SkillId::Count)) {
        return static_cast<SkillId>(numeric);
    }
    return skillIdFromString(value);
}

void stripCarriageReturn(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
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
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = "unable to commit save: " + std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec) { if (error) *error = "unable to commit save: " + ec.message(); return false; }
#endif
    return true;
}

} // namespace

std::string ReplayData::serialize() const {
    std::ostringstream stream;
    stream << "TA_REPLAY 3\n";
    stream << "seed " << seed << "\nweapon " << static_cast<int>(weapon) << "\nchassis " << static_cast<int>(chassis) << "\nsupport " << static_cast<int>(support) << "\nskull " << static_cast<int>(skull) << "\nskull_mask " << static_cast<unsigned int>(skullMask) << "\nultimate " << static_cast<int>(ultimate) << "\nevolution " << static_cast<int>(evolution) << "\nultimate_module " << static_cast<unsigned int>(ultimateModule) << "\nauto_ultimate " << (autoUltimate ? 1 : 0) << "\narena " << static_cast<int>(arena) << "\n";
    if (contentHash != 0) stream << "content_hash " << contentHash << "\n";
    if (endless) stream << "endless 1\n";
    if (dailyDateKey != 0) stream << "daily_date " << dailyDateKey << "\n";
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        stream << "skill_slot_" << slot << ' ' << skillIdString(skillLoadout.skills[slot]) << "\n";
        if (!skillLoadout.nodeBuilds[slot].empty()) stream << "skill_nodes_" << slot << ' ' << skillLoadout.nodeBuilds[slot] << "\n";
    }
    for (const ReplayEvent& event : events) {
        if (event.action == ReplayAction::SkillCast) {
            const int x = static_cast<int>(std::lround(event.target.world.x * 10.0f));
            const int y = static_cast<int>(std::lround(event.target.world.y * 10.0f));
            const int dx = static_cast<int>(std::lround(event.target.direction.x * 1000.0f));
            const int dy = static_cast<int>(std::lround(event.target.direction.y * 1000.0f));
            stream << "skill_cast " << event.tick << ' ' << static_cast<int>(event.slot) << ' ' << skillIdString(event.skill) << ' ' << static_cast<int>(event.target.mode) << ' ' << x << ' ' << y << ' ' << event.target.entityId << ' ' << event.sequence << ' ' << dx << ' ' << dy << "\n";
        } else stream << "event " << event.tick << ' ' << static_cast<int>(event.action) << ' ' << static_cast<int>(event.value) << "\n";
    }
    return stream.str();
}

bool ReplayData::deserialize(const std::string& text, ReplayData& output, std::string* error) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line)) { if (error) *error = "invalid replay header"; return false; }
    stripCarriageReturn(line);
    const bool replayV3 = line == "TA_REPLAY 3";
    if (line != "TA_REPLAY 1" && line != "TA_REPLAY 2" && !replayV3) { if (error) *error = "invalid replay header"; return false; }
    ReplayData parsed;
    while (std::getline(stream, line)) {
        stripCarriageReturn(line);
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
        } else if (tag == "chassis") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(TowerChassis::Catalyst)) { if (error) *error = "invalid replay chassis"; return false; }
            parsed.chassis = static_cast<TowerChassis>(value);
        } else if (tag == "skull") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Skull::Greed)) { if (error) *error = "invalid replay skull"; return false; }
            parsed.skull = static_cast<Skull>(value);
            parsed.skullMask = value == 0 ? 0 : static_cast<SkullMask>(1u << value);
        } else if (tag == "support") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(SupportModule::CorrosionAmp)) { if (error) *error = "invalid replay support module"; return false; }
            parsed.support = static_cast<SupportModule>(value);
        } else if (tag == "skull_mask") {
            unsigned int value = 0; fields >> value;
            const unsigned int valid = (1u << (static_cast<unsigned int>(Skull::Greed) + 1u)) - 2u;
            if (fields.fail() || value > valid) { if (error) *error = "invalid replay skull mask"; return false; }
            parsed.skullMask = static_cast<SkullMask>(value);
        } else if (tag == "ultimate") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(Ultimate::EnergySurge)) { if (error) *error = "invalid replay ultimate"; return false; }
            parsed.ultimate = static_cast<Ultimate>(value);
        } else if (tag == "evolution") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > static_cast<int>(UltimateEvolution::TerminalDischarge)) { if (error) *error = "invalid replay evolution"; return false; }
            parsed.evolution = static_cast<UltimateEvolution>(value);
        } else if (tag == "ultimate_module") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > 255 || (value < 255 && value > static_cast<int>(UltimateModule::SurgeDischarge))) { if (error) *error = "invalid replay ultimate module"; return false; }
            parsed.ultimateModule = static_cast<UltimateModule>(value);
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
        } else if (tag == "endless") {
            int value = -1; fields >> value;
            if (fields.fail() || value < 0 || value > 1) { if (error) *error = "invalid replay endless flag"; return false; }
            parsed.endless = value != 0;
        } else if (tag == "daily_date") {
            std::string value;
            fields >> value;
            if (!parseUnsigned(value, parsed.dailyDateKey) || parsed.dailyDateKey < 10000101u) { if (error) *error = "invalid replay daily date"; return false; }
        } else if (tag.rfind("skill_slot_", 0) == 0) {
            const std::string indexText = tag.substr(std::string("skill_slot_").size());
            std::uint32_t index = 0; std::string skillText;
            if (!parseUnsigned(indexText, index) || index >= SkillSlotCount || !(fields >> skillText)) { if (error) *error = "invalid replay skill slot"; return false; }
            const SkillId skill = parseSkillId(skillText);
            if (skill == SkillId::Count) { if (error) *error = "invalid replay skill slot"; return false; }
            parsed.skillLoadout.skills[index] = skill;
        } else if (tag.rfind("skill_nodes_", 0) == 0) {
            const std::string indexText = tag.substr(std::string("skill_nodes_").size());
            std::uint32_t index = 0;
            if (!parseUnsigned(indexText, index) || index >= SkillSlotCount || !(fields >> parsed.skillLoadout.nodeBuilds[index])) { if (error) *error = "invalid replay skill nodes"; return false; }
        } else if (tag == "event") {
            std::uint32_t tick = 0; int action = 0; int value = 0;
            if (!(fields >> tick >> action >> value) || tick == 0 || action < 1 || action > 3 || value < 0 || value > 255) { if (error) *error = "invalid replay event"; return false; }
            if (action == static_cast<int>(ReplayAction::Upgrade) && value > 2) { if (error) *error = "invalid replay upgrade choice"; return false; }
            if (action == static_cast<int>(ReplayAction::Ultimate) && value != 0) { if (error) *error = "invalid replay ultimate value"; return false; }
            if (action == static_cast<int>(ReplayAction::Reroll) && value != 0) { if (error) *error = "invalid replay reroll value"; return false; }
            if (!parsed.events.empty() && tick < parsed.events.back().tick) { if (error) *error = "replay events are not ordered by tick"; return false; }
            parsed.events.push_back({tick, static_cast<ReplayAction>(action), static_cast<std::uint8_t>(value), 0, SkillId::GravityWell, {}});
        } else if (tag == "skill_cast") {
            std::uint32_t tick = 0; int slot = -1; std::string skillText; int mode = -1; int x = 0; int y = 0; int entity = -1;
            if (!(fields >> tick >> slot >> skillText >> mode >> x >> y >> entity) || tick == 0 || slot < 0 || slot >= static_cast<int>(SkillSlotCount) || mode < 0 || mode > static_cast<int>(SkillTargetMode::Direction)) { if (error) *error = "invalid replay skill cast"; return false; }
            const SkillId skill = parseSkillId(skillText);
            if (skill == SkillId::Count) { if (error) *error = "invalid replay skill cast"; return false; }
            if (!parsed.events.empty() && tick < parsed.events.back().tick) { if (error) *error = "replay events are not ordered by tick"; return false; }
            ReplayEvent event;
            event.tick = tick; event.action = ReplayAction::SkillCast; event.slot = static_cast<std::uint8_t>(slot); event.skill = static_cast<SkillId>(skill);
            event.target.mode = static_cast<SkillTargetMode>(mode); event.target.world = {static_cast<float>(x) / 10.0f, static_cast<float>(y) / 10.0f}; event.target.entityId = entity;
            if (replayV3) {
                int dx = 0; int dy = 0;
                if (!(fields >> event.sequence >> dx >> dy) || std::abs(dx) > 1000 || std::abs(dy) > 1000) { if (error) *error = "invalid replay skill cast sequence"; return false; }
                event.target.direction = {static_cast<float>(dx) / 1000.0f, static_cast<float>(dy) / 1000.0f};
            }
            parsed.events.push_back(event);
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
    if (replay.contentHash != 0 || replay.dailyDateKey != 0) {
        ContentConfig authored;
        std::string contentError;
        const std::string contentDirectory = defaultContentDirectory();
        if (!loadContentConfig(contentDirectory, authored, &contentError)) {
            if (error) *error = "unable to load replay content: " + contentError;
            return false;
        }
        if (replay.contentHash != 0 && contentFingerprint(authored) != replay.contentHash) {
            if (error) *error = "replay content hash does not match installed authored content";
            return false;
        }
        simulation.setContentConfig(authored);
        if (replay.dailyDateKey != 0) {
            const DailyChallenge challenge = challengeForDate(replay.dailyDateKey);
            simulation.setContentConfig(contentForDailyChallenge(authored, challenge));
            simulation.setSkillRules(challenge.requiredSkills, challenge.forbiddenSkills, challenge.allowedSkillBranches);
        } else simulation.setSkillRules({}, {});
    }
    simulation.setWeapon(replay.weapon);
    simulation.setChassis(replay.chassis);
    simulation.setSupport(replay.support);
    if (replay.skullMask != 0) simulation.setSkullMask(replay.skullMask);
    else simulation.setSkull(replay.skull);
    simulation.setUltimate(replay.ultimate);
    simulation.setUltimateEvolution(replay.evolution);
    if (static_cast<unsigned int>(replay.ultimateModule) < 10u) simulation.setUltimateModule(replay.ultimateModule);
    simulation.setAutoUltimate(replay.autoUltimate);
    simulation.setArena(replay.arena);
    simulation.setEndless(replay.endless);
    simulation.setSkillLoadout(replay.skillLoadout);
    simulation.reset(replay.seed);
    std::size_t eventIndex = 0;
    for (std::uint32_t tick = 1; tick <= ticks && !simulation.isGameOver() && !simulation.isVictory(); ++tick) {
        while (eventIndex < replay.events.size() && replay.events[eventIndex].tick == tick) {
            const ReplayEvent& event = replay.events[eventIndex++];
            if (event.action == ReplayAction::Upgrade) simulation.chooseUpgrade(event.value);
            else if (event.action == ReplayAction::Ultimate) simulation.activateUltimate();
            else if (event.action == ReplayAction::SkillCast) {
                TargetSpec target = event.target;
                if (!simulation.activateSkill(event.slot, target, error)) return false;
                if (event.sequence != 0 && simulation.lastSkillCastSequence() != event.sequence) { if (error) *error = "replay skill cast sequence diverged"; return false; }
            }
            else if (event.action == ReplayAction::Reroll && !simulation.rerollUpgradeChoices()) { if (error) *error = "replay reroll was not legal"; return false; }
        }
        simulation.tick();
    }
    hash = simulation.stateHash();
    if (finalStats) *finalStats = simulation.stats();
    return eventIndex == replay.events.size();
}

bool saveProfile(const std::string& path, const ProfileData& profile, std::string* error) {
    std::ostringstream stream;
    stream << "TA_PROFILE 15\n"
           << "best_score " << profile.bestScore << "\n"
           << "best_wave " << profile.bestWave << "\n"
           << "runs_completed " << profile.runsCompleted << "\n"
           << "total_kills " << profile.totalKills << "\n"
           << "reduced_flashes " << (profile.reducedFlashes ? 1 : 0) << "\n";
    stream << "cosmetic_shards " << profile.cosmeticShards << "\n"
           << "core_parts " << profile.coreParts << "\n"
           << "legend_cores " << profile.legendCores << "\n"
           << "tower_core_level " << static_cast<unsigned int>(profile.towerCoreLevel) << "\n";
    for (std::size_t index = 0; index < profile.weaponModuleLevels.size(); ++index) stream << "module_level_" << index << ' ' << static_cast<unsigned int>(profile.weaponModuleLevels[index]) << "\n";
    for (std::size_t index = 0; index < profile.supportModuleLevels.size(); ++index) stream << "support_level_" << index << ' ' << static_cast<unsigned int>(profile.supportModuleLevels[index]) << "\n";
    stream
           << "unlocked_ultimate_evolutions " << profile.unlockedUltimateEvolutionsMask << "\n"
           << "equipped_ultimate_evolution " << static_cast<unsigned int>(profile.equippedUltimateEvolution) << "\n"
           << "unlocked_ultimate_modules " << profile.unlockedUltimateModulesMask << "\n"
           << "equipped_ultimate_module " << static_cast<unsigned int>(profile.equippedUltimateModule) << "\n"
           << "equipped_support_module " << static_cast<unsigned int>(profile.equippedSupportModule) << "\n"
           << "equipped_chassis " << static_cast<unsigned int>(profile.equippedChassis) << "\n"
           << "equipped_weapon " << static_cast<unsigned int>(profile.equippedWeapon) << "\n"
           << "equipped_ultimate " << static_cast<unsigned int>(profile.equippedUltimate) << "\n"
           << "unlocked_skills " << profile.unlockedSkillsMask << "\n"
           << "unlocked_skins " << profile.unlockedSkinsMask << "\n"
           << "equipped_skin " << static_cast<unsigned int>(profile.equippedSkin) << "\n"
           << "high_contrast " << (profile.highContrast ? 1 : 0) << "\n"
           << "master_volume " << static_cast<unsigned int>(profile.masterVolume) << "\n"
           << "music_volume " << static_cast<unsigned int>(profile.musicVolume) << "\n"
           << "sfx_volume " << static_cast<unsigned int>(profile.sfxVolume) << "\n"
           << "ui_volume " << static_cast<unsigned int>(profile.uiVolume) << "\n"
           << "ui_scale_percent " << static_cast<unsigned int>(profile.uiScalePercent) << "\n"
           << "color_blind_palette " << static_cast<unsigned int>(profile.colorBlindPalette) << "\n"
           << "subtitles " << (profile.subtitles ? 1 : 0) << "\n"
           << "vibration " << (profile.vibration ? 1 : 0) << "\n";
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        stream << "skill_slot_" << slot << ' ' << skillIdString(profile.skillLoadout.skills[slot]) << "\n";
        if (!profile.skillLoadout.nodeBuilds[slot].empty()) stream << "skill_nodes_" << slot << ' ' << profile.skillLoadout.nodeBuilds[slot] << "\n";
    }
    for (std::size_t preset = 0; preset < profile.skillPresets.size(); ++preset) {
        stream << "skill_preset_" << preset << "_name " << profile.skillPresetNames[preset] << "\n";
        for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
            stream << "skill_preset_" << preset << "_slot_" << slot << ' ' << skillIdString(profile.skillPresets[preset].skills[slot]) << "\n";
            if (!profile.skillPresets[preset].nodeBuilds[slot].empty()) stream << "skill_preset_" << preset << "_nodes_" << slot << ' ' << profile.skillPresets[preset].nodeBuilds[slot] << "\n";
        }
    }
    for (const std::string& node : profile.unlockedSkillNodes) if (!node.empty()) stream << "skill_node " << node << "\n";
    for (const std::uint32_t dateKey : profile.claimedDailyChallenges) stream << "daily_claimed " << dateKey << "\n";
    for (std::size_t index = 0; index < profile.ultimateMasteryRuns.size(); ++index) stream << "ultimate_mastery_" << index << ' ' << static_cast<unsigned int>(profile.ultimateMasteryRuns[index]) << "\n";
    for (std::size_t index = 0; index < profile.inputBindings.keyboard.size(); ++index) {
        stream << "key_binding_" << index << ' ' << profile.inputBindings.keyboard[index] << "\n";
    }
    return atomicWrite(path, stream.str(), error);
}

bool loadProfile(const std::string& path, ProfileData& profile, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { if (error) *error = "profile does not exist"; return false; }
    std::string line;
    if (!std::getline(file, line)) { if (error) *error = "invalid profile header"; return false; }
    stripCarriageReturn(line);
    if (line != "TA_PROFILE 1" && line != "TA_PROFILE 2" && line != "TA_PROFILE 3" && line != "TA_PROFILE 4" && line != "TA_PROFILE 5" && line != "TA_PROFILE 6" && line != "TA_PROFILE 7" && line != "TA_PROFILE 8" && line != "TA_PROFILE 9" && line != "TA_PROFILE 10" && line != "TA_PROFILE 11" && line != "TA_PROFILE 12" && line != "TA_PROFILE 13" && line != "TA_PROFILE 14" && line != "TA_PROFILE 15") { if (error) *error = "invalid profile header"; return false; }
    ProfileData parsed;
    parsed.version = line == "TA_PROFILE 15" ? 15u : (line == "TA_PROFILE 14" ? 14u : (line == "TA_PROFILE 13" ? 13u : (line == "TA_PROFILE 12" ? 12u : (line == "TA_PROFILE 11" ? 11u : (line == "TA_PROFILE 10" ? 10u : (line == "TA_PROFILE 9" ? 9u : (line == "TA_PROFILE 8" ? 8u : (line == "TA_PROFILE 7" ? 7u : (line == "TA_PROFILE 6" ? 6u : (line == "TA_PROFILE 5" ? 5u : (line == "TA_PROFILE 4" ? 4u : (line == "TA_PROFILE 3" ? 3u : (line == "TA_PROFILE 2" ? 2u : 1u)))))))))))));
    while (std::getline(file, line)) {
        stripCarriageReturn(line);
        std::istringstream fields(line); std::string tag, value; fields >> tag >> value;
        if (tag.empty()) continue;
        if (tag == "best_score") { if (!parseInt(value, parsed.bestScore)) return false; }
        else if (tag == "best_wave") { if (!parseInt(value, parsed.bestWave)) return false; }
        else if (tag == "runs_completed") { if (!parseUnsigned(value, parsed.runsCompleted)) return false; }
        else if (tag == "total_kills") { if (!parseUnsigned(value, parsed.totalKills)) return false; }
        else if (tag == "reduced_flashes") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.reducedFlashes = flag != 0; }
        else if (tag == "cosmetic_shards") { if (!parseUnsigned(value, parsed.cosmeticShards)) return false; }
        else if (tag == "core_parts") { if (!parseUnsigned(value, parsed.coreParts)) return false; }
        else if (tag == "legend_cores") { if (!parseUnsigned(value, parsed.legendCores)) return false; }
        else if (tag == "tower_core_level") { std::uint32_t level = 0; if (!parseUnsigned(value, level) || level > 20) return false; parsed.towerCoreLevel = static_cast<std::uint8_t>(level); }
        else if (tag.rfind("module_level_", 0) == 0) { const std::string indexText = tag.substr(std::string("module_level_").size()); std::uint32_t index = 0; std::uint32_t level = 0; if (!parseUnsigned(indexText, index) || index >= parsed.weaponModuleLevels.size() || !parseUnsigned(value, level) || level > 20) return false; parsed.weaponModuleLevels[index] = static_cast<std::uint8_t>(level); }
        else if (tag.rfind("support_level_", 0) == 0) { const std::string indexText = tag.substr(std::string("support_level_").size()); std::uint32_t index = 0; std::uint32_t level = 0; if (!parseUnsigned(indexText, index) || index >= parsed.supportModuleLevels.size() || !parseUnsigned(value, level) || level > 20) return false; parsed.supportModuleLevels[index] = static_cast<std::uint8_t>(level); }
        else if (tag == "unlocked_ultimate_evolutions") { if (!parseUnsigned(value, parsed.unlockedUltimateEvolutionsMask) || (parsed.unlockedUltimateEvolutionsMask & ~0xFFFEu) != 0) return false; }
        else if (tag == "equipped_ultimate_evolution") { std::uint32_t evolution = 0; if (!parseUnsigned(value, evolution) || evolution > static_cast<std::uint32_t>(UltimateEvolution::TerminalDischarge) || (evolution != 0 && (parsed.unlockedUltimateEvolutionsMask & (1u << evolution)) == 0)) return false; parsed.equippedUltimateEvolution = static_cast<std::uint8_t>(evolution); }
        else if (tag == "unlocked_ultimate_modules") { std::uint32_t mask = 0; if (!parseUnsigned(value, mask) || (mask & ~0x03FFu) != 0) return false; parsed.unlockedUltimateModulesMask = static_cast<std::uint16_t>(mask); }
        else if (tag == "equipped_ultimate_module") { std::uint32_t module = 0; if (!parseUnsigned(value, module) || module > 255u || (module < 255u && (parsed.unlockedUltimateModulesMask & (1u << module)) == 0)) return false; parsed.equippedUltimateModule = static_cast<std::uint8_t>(module); }
        else if (tag == "equipped_support_module") { std::uint32_t support = 0; if (!parseUnsigned(value, support) || support > static_cast<std::uint32_t>(SupportModule::CorrosionAmp)) return false; parsed.equippedSupportModule = static_cast<std::uint8_t>(support); }
        else if (tag == "equipped_chassis") { std::uint32_t chassis = 0; if (!parseUnsigned(value, chassis) || chassis > static_cast<std::uint32_t>(TowerChassis::Catalyst)) return false; parsed.equippedChassis = static_cast<std::uint8_t>(chassis); }
        else if (tag == "equipped_weapon") { std::uint32_t weapon = 0; if (!parseUnsigned(value, weapon) || weapon > static_cast<std::uint32_t>(Weapon::SniperRailgun)) return false; parsed.equippedWeapon = static_cast<std::uint8_t>(weapon); }
        else if (tag == "equipped_ultimate") { std::uint32_t ultimate = 0; if (!parseUnsigned(value, ultimate) || ultimate > static_cast<std::uint32_t>(Ultimate::EnergySurge)) return false; parsed.equippedUltimate = static_cast<std::uint8_t>(ultimate); }
        else if (tag == "unlocked_skills") { if (!parseUnsigned(value, parsed.unlockedSkillsMask) || (parsed.unlockedSkillsMask & ~((1u << static_cast<unsigned int>(SkillId::Count)) - 1u)) != 0) return false; }
        else if (tag.rfind("skill_slot_", 0) == 0) { const std::string indexText = tag.substr(std::string("skill_slot_").size()); std::uint32_t index = 0; if (!parseUnsigned(indexText, index) || index >= SkillSlotCount) return false; const SkillId skill = parseSkillId(value); if (skill == SkillId::Count) return false; parsed.skillLoadout.skills[index] = skill; }
        else if (tag.rfind("skill_nodes_", 0) == 0) { const std::string indexText = tag.substr(std::string("skill_nodes_").size()); std::uint32_t index = 0; if (!parseUnsigned(indexText, index) || index >= SkillSlotCount || value.empty()) return false; parsed.skillLoadout.nodeBuilds[index] = value; }
        else if (tag.rfind("skill_preset_", 0) == 0) {
            const std::string rest = tag.substr(std::string("skill_preset_").size());
            const std::size_t underscore = rest.find('_');
            std::uint32_t preset = 0;
            if (underscore == std::string::npos || !parseUnsigned(rest.substr(0, underscore), preset) || preset >= parsed.skillPresets.size()) return false;
            const std::string field = rest.substr(underscore + 1);
            if (field == "name") { if (value.empty()) return false; parsed.skillPresetNames[preset] = value; }
            else if (field.rfind("slot_", 0) == 0) { std::uint32_t slot = 0; if (!parseUnsigned(field.substr(5), slot) || slot >= SkillSlotCount) return false; const SkillId skill = parseSkillId(value); if (skill == SkillId::Count) return false; parsed.skillPresets[preset].skills[slot] = skill; }
            else if (field.rfind("nodes_", 0) == 0) { std::uint32_t slot = 0; if (!parseUnsigned(field.substr(6), slot) || slot >= SkillSlotCount || value.empty()) return false; parsed.skillPresets[preset].nodeBuilds[slot] = value; }
            else return false;
        }
        else if (tag == "skill_node") { if (value.empty() || value.find(':') == std::string::npos || std::find(parsed.unlockedSkillNodes.begin(), parsed.unlockedSkillNodes.end(), value) != parsed.unlockedSkillNodes.end()) return false; parsed.unlockedSkillNodes.push_back(value); }
        else if (tag == "daily_claimed") { std::uint32_t dateKey = 0; if (!parseUnsigned(value, dateKey) || dateKey == 0) return false; if (!hasClaimedDaily(parsed, dateKey)) parsed.claimedDailyChallenges.push_back(dateKey); }
        else if (tag.rfind("ultimate_mastery_", 0) == 0) { const std::string indexText = tag.substr(std::string("ultimate_mastery_").size()); std::uint32_t index = 0; std::uint32_t runs = 0; if (!parseUnsigned(indexText, index) || index >= parsed.ultimateMasteryRuns.size() || !parseUnsigned(value, runs) || runs > 255) return false; parsed.ultimateMasteryRuns[index] = static_cast<std::uint8_t>(runs); }
        else if (tag == "unlocked_skins") { if (!parseUnsigned(value, parsed.unlockedSkinsMask) || (parsed.unlockedSkinsMask & 1u) == 0) return false; }
        else if (tag == "equipped_skin") { std::uint32_t skin = 0; if (!parseUnsigned(value, skin) || skin > static_cast<std::uint32_t>(TowerSkin::Gold) || (parsed.unlockedSkinsMask & (1u << skin)) == 0) return false; parsed.equippedSkin = static_cast<std::uint8_t>(skin); }
        else if (tag == "high_contrast") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.highContrast = flag != 0; }
        else if (tag == "master_volume") { std::uint32_t volume = 0; if (!parseUnsigned(value, volume) || volume > 100) return false; parsed.masterVolume = static_cast<std::uint8_t>(volume); }
        else if (tag == "music_volume") { std::uint32_t volume = 0; if (!parseUnsigned(value, volume) || volume > 100) return false; parsed.musicVolume = static_cast<std::uint8_t>(volume); }
        else if (tag == "sfx_volume") { std::uint32_t volume = 0; if (!parseUnsigned(value, volume) || volume > 100) return false; parsed.sfxVolume = static_cast<std::uint8_t>(volume); }
        else if (tag == "ui_volume") { std::uint32_t volume = 0; if (!parseUnsigned(value, volume) || volume > 100) return false; parsed.uiVolume = static_cast<std::uint8_t>(volume); }
        else if (tag == "ui_scale_percent") { std::uint32_t scale = 0; if (!parseUnsigned(value, scale) || scale < 80 || scale > 140) return false; parsed.uiScalePercent = static_cast<std::uint8_t>(scale); }
        else if (tag == "color_blind_palette") { std::uint32_t palette = 0; if (!parseUnsigned(value, palette) || palette > 2) return false; parsed.colorBlindPalette = static_cast<std::uint8_t>(palette); }
        else if (tag == "subtitles") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.subtitles = flag != 0; }
        else if (tag == "vibration") { std::uint32_t flag = 0; if (!parseUnsigned(value, flag) || flag > 1) return false; parsed.vibration = flag != 0; }
        else if (tag.rfind("key_binding_", 0) == 0) {
            const std::string indexText = tag.substr(std::string("key_binding_").size());
            std::uint32_t index = 0;
            int keycode = 0;
            if (!parseUnsigned(indexText, index) || index >= parsed.inputBindings.keyboard.size() || !parseInt(value, keycode) || !validInputKey(keycode)) {
                if (error) *error = "invalid profile key binding";
                return false;
            }
            parsed.inputBindings.keyboard[index] = keycode;
        }
        else { if (error) *error = "unknown profile record: " + tag; return false; }
    }
    // Older profiles are intentionally sparse. Their missing fields retain the
    // safe defaults from ProfileData, then the in-memory record is promoted to
    // the current schema so the next save writes the complete format.
    if (parsed.version < 15u) parsed.version = 15u;
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

std::uint32_t awardRunProgression(ProfileData& profile, const SimStats& stats, bool dailyRun, Ultimate ultimate) {
    profile.coreParts += static_cast<std::uint32_t>(stats.kills / 4 + stats.wave * 3 + (stats.wave >= 10 ? 50 : 0));
    if (dailyRun) profile.coreParts += 20;
    const std::size_t index = static_cast<std::size_t>(ultimate);
    if (index < profile.ultimateMasteryRuns.size() && profile.ultimateMasteryRuns[index] < 255) ++profile.ultimateMasteryRuns[index];
    if (!dailyRun && profile.runsCompleted > 0 && profile.runsCompleted % 5u == 0u) { ++profile.legendCores; return 1u; }
    return 0u;
}

bool hasClaimedDaily(const ProfileData& profile, std::uint32_t dateKey) {
    return std::find(profile.claimedDailyChallenges.begin(), profile.claimedDailyChallenges.end(), dateKey) != profile.claimedDailyChallenges.end();
}

bool claimDailyLegendCores(ProfileData& profile, std::uint32_t dateKey, std::uint32_t reward) {
    if (dateKey == 0 || hasClaimedDaily(profile, dateKey)) return false;
    profile.claimedDailyChallenges.push_back(dateKey);
    profile.legendCores += reward;
    return true;
}

std::uint32_t workshopTowerCost(const ProfileData& profile) {
    return 80u + static_cast<std::uint32_t>(profile.towerCoreLevel) * 45u;
}

std::uint32_t workshopModuleCost(const ProfileData& profile, Weapon weapon) {
    const std::size_t index = static_cast<std::size_t>(weapon);
    return 60u + static_cast<std::uint32_t>(profile.weaponModuleLevels[index]) * 35u;
}

std::uint32_t workshopSupportCost(const ProfileData& profile, SupportModule support) {
    const std::size_t index = static_cast<std::size_t>(support);
    return 55u + static_cast<std::uint32_t>(profile.supportModuleLevels[index]) * 30u;
}

std::uint32_t workshopTowerCost(const ProfileData& profile, const ContentConfig& content) {
    return content.workshopBaseCost[0] + static_cast<std::uint32_t>(profile.towerCoreLevel) * content.workshopCostStep[0];
}

std::uint32_t workshopModuleCost(const ProfileData& profile, Weapon weapon, const ContentConfig& content) {
    const std::size_t weaponIndex = static_cast<std::size_t>(weapon);
    const std::size_t nodeIndex = 1u + weaponIndex;
    return content.workshopBaseCost[nodeIndex] + static_cast<std::uint32_t>(profile.weaponModuleLevels[weaponIndex]) * content.workshopCostStep[nodeIndex];
}

std::uint32_t workshopSupportCost(const ProfileData& profile, SupportModule support, const ContentConfig& content) {
    const std::size_t supportIndex = static_cast<std::size_t>(support);
    if (supportIndex == 0) return 0;
    const std::size_t nodeIndex = 5u + supportIndex;
    return content.workshopBaseCost[nodeIndex] + static_cast<std::uint32_t>(profile.supportModuleLevels[supportIndex]) * content.workshopCostStep[nodeIndex];
}

bool purchaseTowerCore(ProfileData& profile) {
    if (profile.towerCoreLevel >= 20 || profile.coreParts < workshopTowerCost(profile)) return false;
    profile.coreParts -= workshopTowerCost(profile);
    ++profile.towerCoreLevel;
    return true;
}

bool purchaseWeaponModule(ProfileData& profile, Weapon weapon) {
    const std::size_t index = static_cast<std::size_t>(weapon);
    if (profile.weaponModuleLevels[index] >= 20 || profile.coreParts < workshopModuleCost(profile, weapon)) return false;
    profile.coreParts -= workshopModuleCost(profile, weapon);
    ++profile.weaponModuleLevels[index];
    return true;
}

bool purchaseSupportModule(ProfileData& profile, SupportModule support) {
    const std::size_t index = static_cast<std::size_t>(support);
    if (index == 0 || profile.supportModuleLevels[index] >= 20 || profile.coreParts < workshopSupportCost(profile, support)) return false;
    profile.coreParts -= workshopSupportCost(profile, support);
    ++profile.supportModuleLevels[index];
    return true;
}

bool purchaseTowerCore(ProfileData& profile, const ContentConfig& content) {
    if (profile.towerCoreLevel >= content.workshopMaxLevel[0] || profile.coreParts < workshopTowerCost(profile, content)) return false;
    profile.coreParts -= workshopTowerCost(profile, content);
    ++profile.towerCoreLevel;
    return true;
}

bool purchaseWeaponModule(ProfileData& profile, Weapon weapon, const ContentConfig& content) {
    const std::size_t index = static_cast<std::size_t>(weapon);
    const std::size_t nodeIndex = 1u + index;
    if (profile.weaponModuleLevels[index] >= content.workshopMaxLevel[nodeIndex] || profile.coreParts < workshopModuleCost(profile, weapon, content)) return false;
    profile.coreParts -= workshopModuleCost(profile, weapon, content);
    ++profile.weaponModuleLevels[index];
    return true;
}

bool purchaseSupportModule(ProfileData& profile, SupportModule support, const ContentConfig& content) {
    const std::size_t supportIndex = static_cast<std::size_t>(support);
    if (supportIndex == 0) return false;
    const std::size_t nodeIndex = 5u + supportIndex;
    if (profile.supportModuleLevels[supportIndex] >= content.workshopMaxLevel[nodeIndex] || profile.coreParts < workshopSupportCost(profile, support, content)) return false;
    profile.coreParts -= workshopSupportCost(profile, support, content);
    ++profile.supportModuleLevels[supportIndex];
    return true;
}

bool isUltimateEvolutionUnlocked(const ProfileData& profile, UltimateEvolution evolution) {
    if (evolution == UltimateEvolution::None) return true;
    return (profile.unlockedUltimateEvolutionsMask & (1u << static_cast<unsigned int>(evolution))) != 0;
}

bool unlockUltimateEvolution(ProfileData& profile, UltimateEvolution evolution) {
    if (evolution == UltimateEvolution::None || isUltimateEvolutionUnlocked(profile, evolution)) return true;
    constexpr std::uint32_t cost = 5;
    const std::size_t masteryIndex = (static_cast<std::size_t>(evolution) - 1u) / 3u;
    if (masteryIndex >= profile.ultimateMasteryRuns.size() || profile.ultimateMasteryRuns[masteryIndex] < 3 || profile.legendCores < cost) return false;
    profile.legendCores -= cost;
    profile.unlockedUltimateEvolutionsMask |= 1u << static_cast<unsigned int>(evolution);
    return true;
}

bool unlockUltimateEvolution(ProfileData& profile, UltimateEvolution evolution, const ContentConfig& content) {
    if (evolution == UltimateEvolution::None || isUltimateEvolutionUnlocked(profile, evolution)) return true;
    const std::size_t evolutionIndex = static_cast<std::size_t>(evolution) - 1u;
    if (evolutionIndex >= content.ultimateEvolutionCost.size()) return false;
    const std::size_t masteryIndex = evolutionIndex / 3u;
    const std::uint32_t cost = content.ultimateEvolutionCost[evolutionIndex];
    if (masteryIndex >= profile.ultimateMasteryRuns.size() || profile.ultimateMasteryRuns[masteryIndex] < 3 || profile.legendCores < cost) return false;
    profile.legendCores -= cost;
    profile.unlockedUltimateEvolutionsMask |= 1u << static_cast<unsigned int>(evolution);
    return true;
}

bool equipUltimateEvolution(ProfileData& profile, UltimateEvolution evolution) {
    if (!isUltimateEvolutionUnlocked(profile, evolution)) return false;
    profile.equippedUltimateEvolution = static_cast<std::uint8_t>(evolution);
    return true;
}

bool isUltimateModuleUnlocked(const ProfileData& profile, UltimateModule module) {
    const unsigned int index = static_cast<unsigned int>(module);
    return index < 10u && (profile.unlockedUltimateModulesMask & (1u << index)) != 0u;
}

bool unlockUltimateModule(ProfileData& profile, UltimateModule module, const ContentConfig& content) {
    const unsigned int index = static_cast<unsigned int>(module);
    if (index >= 10u || isUltimateModuleUnlocked(profile, module)) return index < 10u;
    const std::size_t ultimateIndex = static_cast<std::size_t>(index / 2u);
    if (profile.ultimateMasteryRuns[ultimateIndex] < 3u || profile.coreParts < content.ultimateModuleCost[index]) return false;
    profile.coreParts -= content.ultimateModuleCost[index];
    profile.unlockedUltimateModulesMask = static_cast<std::uint16_t>(profile.unlockedUltimateModulesMask | (1u << index));
    return true;
}

bool equipUltimateModule(ProfileData& profile, UltimateModule module) {
    if (!isUltimateModuleUnlocked(profile, module)) return false;
    profile.equippedUltimateModule = static_cast<std::uint8_t>(module);
    return true;
}

bool isSkillUnlocked(const ProfileData& profile, SkillId skill) {
    const unsigned int index = static_cast<unsigned int>(skill);
    return index < static_cast<unsigned int>(SkillId::Count) && (profile.unlockedSkillsMask & (1u << index)) != 0;
}

bool unlockSkill(ProfileData& profile, SkillId skill, const ContentConfig& content) {
    const unsigned int index = static_cast<unsigned int>(skill);
    if (index >= static_cast<unsigned int>(SkillId::Count) || isSkillUnlocked(profile, skill)) return true;
    const std::uint32_t cost = 120u + index * 20u;
    if (profile.coreParts < cost) return false;
    profile.coreParts -= cost;
    profile.unlockedSkillsMask |= 1u << index;
    (void)content;
    return true;
}

int purchasedSkillNodeRank(const ProfileData& profile, const std::string& nodeId) {
    const std::string prefix = nodeId + ":";
    for (const std::string& entry : profile.unlockedSkillNodes) if (entry.rfind(prefix, 0) == 0) {
        try { return std::max(0, std::stoi(entry.substr(prefix.size()))); } catch (...) { return 0; }
    }
    return 0;
}

std::uint32_t skillNodeCost(const ProfileData& profile, const SkillNodeDefinition& node) {
    return node.cost + static_cast<std::uint32_t>(purchasedSkillNodeRank(profile, node.id)) * (node.cost / 2u + 1u);
}

bool purchaseSkillNode(ProfileData& profile, const SkillNodeDefinition& node, const ContentConfig& content) {
    SkillId skill = SkillId::GravityWell;
    for (std::size_t index = 0; index < content.skillDefinitions.size(); ++index) if (content.skillDefinitions[index].id == node.skillId) skill = static_cast<SkillId>(index);
    if (!isSkillUnlocked(profile, skill)) return false;
    const int currentRank = purchasedSkillNodeRank(profile, node.id);
    if (currentRank >= node.maxRank) return false;
    if (!node.parentId.empty()) {
        const auto parent = std::find_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == node.parentId; });
        if (parent == content.skillNodes.end() || purchasedSkillNodeRank(profile, parent->id) <= 0) return false;
    }
    if (profile.coreParts < skillNodeCost(profile, node)) return false;
    profile.coreParts -= skillNodeCost(profile, node);
    const std::string prefix = node.id + ":";
    auto existing = std::find_if(profile.unlockedSkillNodes.begin(), profile.unlockedSkillNodes.end(), [&](const std::string& entry) { return entry.rfind(prefix, 0) == 0; });
    const std::string value = prefix + std::to_string(currentRank + 1);
    if (existing == profile.unlockedSkillNodes.end()) profile.unlockedSkillNodes.push_back(value);
    else *existing = value;
    return true;
}

bool equipSkillBuild(ProfileData& profile, std::size_t slot, const std::string& build, const ContentConfig& content) {
    if (slot >= SkillSlotCount) return false;
    const std::size_t equippedSkill = static_cast<std::size_t>(profile.skillLoadout.skills[slot]);
    if (equippedSkill >= content.skillDefinitions.size() || content.skillDefinitions[equippedSkill].id.empty()) return false;
    std::vector<std::string> selectedBranches;
    std::size_t cursor = 0;
    while (cursor < build.size()) {
        const std::size_t comma = build.find(',', cursor);
        const std::string entry = build.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
        const std::size_t colon = entry.find(':');
        if (colon == std::string::npos) return false;
        const std::string nodeId = entry.substr(0, colon);
        int rank = 0;
        try { rank = std::stoi(entry.substr(colon + 1)); } catch (...) { return false; }
        const auto node = std::find_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == nodeId; });
        if (node == content.skillNodes.end() || rank <= 0 || rank > purchasedSkillNodeRank(profile, nodeId)) return false;
        if (node->skillId != content.skillDefinitions[equippedSkill].id) return false;
        if (!node->parentId.empty()) {
            const std::string parentPrefix = node->parentId + ":";
            if (build.find(parentPrefix) == std::string::npos) return false;
        }
        if (node->tier >= 2 && std::find(selectedBranches.begin(), selectedBranches.end(), node->branchId) == selectedBranches.end()) selectedBranches.push_back(node->branchId);
        if (comma == std::string::npos) break;
        cursor = comma + 1;
    }
    if (selectedBranches.size() > 1) return false;
    profile.skillLoadout.nodeBuilds[slot] = build;
    return true;
}

bool equipSkill(ProfileData& profile, std::size_t slot, SkillId skill) {
    if (slot >= SkillSlotCount || !isSkillUnlocked(profile, skill)) return false;
    for (std::size_t index = 0; index < SkillSlotCount; ++index) if (index != slot && profile.skillLoadout.skills[index] == skill) return false;
    profile.skillLoadout.skills[slot] = skill;
    profile.skillLoadout.nodeBuilds[slot].clear();
    return true;
}

bool equipSkillNode(ProfileData& profile, std::size_t slot, const std::string& nodeId, const ContentConfig& content) {
    if (slot >= SkillSlotCount) return false;
    auto node = std::find_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == nodeId; });
    if (node == content.skillNodes.end() || purchasedSkillNodeRank(profile, nodeId) <= 0) return false;
    const std::size_t skillIndex = static_cast<std::size_t>(profile.skillLoadout.skills[slot]);
    if (skillIndex >= content.skillDefinitions.size() || content.skillDefinitions[skillIndex].id != node->skillId) return false;
    std::vector<const SkillNodeDefinition*> path;
    for (const SkillNodeDefinition* current = &*node; current != nullptr;) {
        path.push_back(current);
        if (current->parentId.empty()) break;
        const auto parent = std::find_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == current->parentId; });
        if (parent == content.skillNodes.end()) return false;
        current = &*parent;
    }
    std::reverse(path.begin(), path.end());
    std::string build;
    for (const SkillNodeDefinition* selected : path) {
        const int rank = purchasedSkillNodeRank(profile, selected->id);
        if (rank <= 0) return false;
        if (!build.empty()) build += ',';
        build += selected->id + ":" + std::to_string(rank);
    }
    return equipSkillBuild(profile, slot, build, content);
}

bool saveSkillPreset(ProfileData& profile, std::size_t preset) {
    if (preset >= profile.skillPresets.size()) return false;
    profile.skillPresets[preset] = profile.skillLoadout;
    return true;
}

bool equipSkillPreset(ProfileData& profile, std::size_t preset) {
    if (preset >= profile.skillPresets.size()) return false;
    const SkillLoadout& candidate = profile.skillPresets[preset];
    std::array<bool, static_cast<std::size_t>(SkillId::Count)> seen{};
    for (const SkillId skill : candidate.skills) {
        const std::size_t index = static_cast<std::size_t>(skill);
        if (index >= seen.size() || seen[index]) return false;
        seen[index] = true;
    }
    profile.skillLoadout = candidate;
    return true;
}

} // namespace ta
