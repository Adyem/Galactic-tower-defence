#include "profile.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int runReplay(const ta::ReplayData& replay, std::uint32_t ticks) {
    std::uint32_t hash = 0;
    ta::SimStats stats;
    std::string error;
    if (!ta::replayFinalHash(replay, ticks, hash, &stats, &error)) {
        std::cerr << "Replay rejected: " << (error.empty() ? "commands beyond simulated range" : error) << '\n';
        return 2;
    }
    std::cout << "Replay verified: seed=" << replay.seed
              << " arena=" << ta::arenaName(replay.arena)
              << " score=" << stats.score
              << " wave=" << stats.wave
              << " kills=" << stats.kills
              << " hash=" << hash << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        ta::ReplayData replay;
        replay.seed = 424242u;
        replay.weapon = ta::Weapon::RapidFire;
        replay.skull = ta::Skull::None;
        replay.skullMask = 0;
        replay.ultimate = ta::Ultimate::MeteorRain;
        replay.arena = ta::Arena::Moonbase;
        ta::ContentConfig authored;
        std::string error;
        const std::string contentDirectory = ta::defaultContentDirectory();
        if (!ta::loadContentConfig(contentDirectory, authored, &error)) { std::cerr << error << '\n'; return 1; }
        replay.contentHash = ta::contentFingerprint(authored);
        return runReplay(replay, 900);
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: ta_replay_check <replay-file> [ticks]\n"
                     "       ta_replay_check --self-test\n";
        return 64;
    }
    ta::ReplayData replay;
    std::string error;
    if (!ta::ReplayData::load(argv[1], replay, &error)) {
        std::cerr << "Replay rejected: " << error << '\n';
        return 1;
    }
    std::uint32_t ticks = 100000;
    if (argc == 3) {
        try {
            const unsigned long parsed = std::stoul(argv[2]);
            if (parsed == 0 || parsed > 10000000ul) throw std::invalid_argument("range");
            ticks = static_cast<std::uint32_t>(parsed);
        } catch (...) {
            std::cerr << "ticks must be an integer from 1 to 10000000\n";
            return 64;
        }
    }
    return runReplay(replay, ticks);
}
