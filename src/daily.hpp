#pragma once

#include "game.hpp"

#include <cstdint>

namespace ta {

struct DailyChallenge {
    std::uint32_t dateKey = 0;
    std::uint32_t seed = 1;
    Weapon recommendedWeapon = Weapon::RapidFire;
    Skull skull = Skull::Swarm;
    Arena arena = Arena::Moonbase;
    std::uint32_t bonusShards = 20;
};

DailyChallenge challengeForDate(std::uint32_t dateKey);
DailyChallenge currentDailyChallenge();

} // namespace ta
