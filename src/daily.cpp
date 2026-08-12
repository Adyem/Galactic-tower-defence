#include "daily.hpp"

#include <ctime>

namespace ta {

DailyChallenge challengeForDate(std::uint32_t dateKey) {
    std::uint32_t hash = dateKey ^ 0xA53C9E17u;
    hash ^= hash >> 16; hash *= 2246822519u; hash ^= hash >> 13; hash *= 3266489917u; hash ^= hash >> 16;
    DailyChallenge result;
    result.dateKey = dateKey;
    result.seed = hash == 0 ? 1u : hash;
    result.recommendedWeapon = static_cast<Weapon>(hash % 5u);
    result.skull = static_cast<Skull>(1 + (hash / 5u) % 4u);
    result.arena = static_cast<Arena>((hash / 19u) % 3u);
    result.bonusShards = 20;
    return result;
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
