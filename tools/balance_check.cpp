#include "game.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
void advance(ta::GameSim& sim, int maxTicks) {
    for (int tick = 0; tick < maxTicks && !sim.isGameOver() && !sim.isVictory(); ++tick) {
        if (sim.upgradePending()) sim.chooseUpgrade(0);
        if (tick % (ta::GameSim::TickRate * 8) == 0) sim.activateUltimate();
        sim.tick();
    }
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
    int total = 0;
    int victories = 0;
    int failures = 0;
    int nonTerminal = 0;
    long long scoreTotal = 0;
    int minTicks = 0;
    int maxTicks = 0;
    int skullCombinations = 0;
    for (int arena = 0; arena < 3; ++arena) {
        for (int weapon = 0; weapon < 5; ++weapon) {
            for (int run = 0; run < runsPerCombination; ++run) {
                const std::uint32_t seed = 0xA5000000u + static_cast<std::uint32_t>(arena * 10000 + weapon * 100 + run + 1);
                ta::GameSim sim(seed);
                sim.setContentConfig(content);
                sim.setArena(static_cast<ta::Arena>(arena));
                sim.setWeapon(static_cast<ta::Weapon>(weapon));
                sim.reset(seed);
                advance(sim, 100000);
                ++total;
                if (sim.isVictory()) ++victories;
                else if (sim.isGameOver()) ++failures;
                else ++nonTerminal;
                scoreTotal += sim.stats().score;
                minTicks = minTicks == 0 ? sim.stats().ticks : std::min(minTicks, sim.stats().ticks);
                maxTicks = std::max(maxTicks, sim.stats().ticks);
            }
        }
    }
    // Exercise every valid combination of the four authored skull bits. A
    // combination may win or fail, but it must always reach a terminal state.
    for (int combination = 0; combination < 16; ++combination) {
        ta::SkullMask mask = 0;
        for (int skull = 1; skull <= 4; ++skull) if ((combination & (1 << (skull - 1))) != 0) mask = static_cast<ta::SkullMask>(mask | (1u << skull));
        ++skullCombinations;
        for (int arena = 0; arena < 3; ++arena) for (int weapon = 0; weapon < 5; ++weapon) {
            const std::uint32_t seed = 0xC7000000u + static_cast<std::uint32_t>(combination * 10000 + arena * 100 + weapon + 1);
            ta::GameSim sim(seed);
            sim.setContentConfig(content);
            sim.setSkullMask(mask);
            sim.setArena(static_cast<ta::Arena>(arena));
            sim.setWeapon(static_cast<ta::Weapon>(weapon));
            sim.reset(seed);
            advance(sim, 100000);
            ++total;
            if (sim.isVictory()) ++victories;
            else if (sim.isGameOver()) ++failures;
            else ++nonTerminal;
            scoreTotal += sim.stats().score;
            minTicks = minTicks == 0 ? sim.stats().ticks : std::min(minTicks, sim.stats().ticks);
            maxTicks = std::max(maxTicks, sim.stats().ticks);
        }
    }
    const double terminalRate = total == 0 ? 0.0 : static_cast<double>(total - nonTerminal) * 100.0 / static_cast<double>(total);
    const double averageScore = total == 0 ? 0.0 : static_cast<double>(scoreTotal) / static_cast<double>(total);
    std::cout << "Tower Ascend balance matrix: runs=" << total << " skull_combinations=" << skullCombinations
              << " victories=" << victories << " failures=" << failures << " non_terminal=" << nonTerminal
              << " terminal_rate=" << std::fixed << std::setprecision(1) << terminalRate << "%"
              << " avg_score=" << std::setprecision(1) << averageScore
              << " ticks=" << minTicks << ".." << maxTicks << '\n';
    return nonTerminal == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
