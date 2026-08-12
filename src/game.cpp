#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ta {
namespace {
constexpr float TowerX = 1110.0f;
constexpr float TowerY = 360.0f;
constexpr float ExitX = 1180.0f;

float distanceSquared(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

GameSim::GameSim(std::uint32_t initialSeed) { reset(initialSeed); }

void GameSim::reset(std::uint32_t initialSeed) {
    seed = initialSeed == 0 ? 1u : initialSeed;
    rngState = seed;
    tickCount = 0;
    wave = 1;
    lives = content.skullLives[hasSkull(Skull::GlassCannon) ? static_cast<std::size_t>(Skull::GlassCannon) : 0];
    currency = 0;
    spawnedThisWave = 0;
    waveSpawnTarget = 8;
    spawnCooldown = 0;
    fireCooldown = 0;
    ultimateMaxCooldown = content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)];
    ultimateCooldown = 0;
    bulletStormTicks = 0;
    nextEnemyId = 1;
    gameOver = false;
    victory = false;
    upgradeChoicePending = false;
    enemyList.clear();
    projectileList.clear();
    ownedUpgrades.clear();
    choices.clear();
    counters = {};
}

std::uint32_t GameSim::nextRandom() {
    // PCG-style small deterministic generator; no platform-dependent distribution.
    rngState = rngState * 747796405u + 2891336453u;
    std::uint32_t x = ((rngState >> ((rngState >> 28u) + 4u)) ^ rngState) * 277803737u;
    return (x >> 22u) ^ x;
}

float GameSim::random01() { return static_cast<float>(nextRandom() % 10000u) / 10000.0f; }

bool GameSim::hasUpgrade(Upgrade upgrade) const {
    return std::find(ownedUpgrades.begin(), ownedUpgrades.end(), upgrade) != ownedUpgrades.end();
}

float GameSim::upgradeValueA(Upgrade upgrade) const { return content.upgradeValueA[static_cast<std::size_t>(upgrade)]; }
float GameSim::upgradeValueB(Upgrade upgrade) const { return content.upgradeValueB[static_cast<std::size_t>(upgrade)]; }

bool GameSim::hasSkull(Skull skull) const {
    if (skull == Skull::None) return selectedSkulls == 0;
    return (selectedSkulls & (static_cast<SkullMask>(1u) << static_cast<unsigned int>(skull))) != 0;
}

float GameSim::skullScoreMultiplier() const {
    float multiplier = 1.0f;
    for (unsigned int value = 1; value <= static_cast<unsigned int>(Skull::Greed); ++value) {
        if ((selectedSkulls & (static_cast<SkullMask>(1u) << value)) != 0) multiplier *= content.skullScoreMultiplier[value];
    }
    return multiplier;
}

void GameSim::setWeapon(Weapon weapon) {
    if (tickCount == 0 && enemyList.empty()) selectedWeapon = weapon;
}

void GameSim::setSkull(Skull skull) {
    if (tickCount != 0 || !enemyList.empty()) return;
    selectedSkulls = skull == Skull::None ? 0 : (static_cast<SkullMask>(1u) << static_cast<unsigned int>(skull));
    selectedSkull = skull;
}

void GameSim::setSkullMask(SkullMask mask) {
    if (tickCount != 0 || !enemyList.empty()) return;
    const SkullMask validBits = static_cast<SkullMask>((1u << (static_cast<unsigned int>(Skull::Greed) + 1u)) - 2u);
    selectedSkulls = mask & validBits;
    selectedSkull = Skull::None;
    for (unsigned int value = 1; value <= static_cast<unsigned int>(Skull::Greed); ++value) {
        if ((selectedSkulls & (static_cast<SkullMask>(1u) << value)) != 0) { selectedSkull = static_cast<Skull>(value); break; }
    }
}

void GameSim::toggleSkull(Skull skull) {
    if (skull == Skull::None || tickCount != 0 || !enemyList.empty()) return;
    const SkullMask bit = static_cast<SkullMask>(1u) << static_cast<unsigned int>(skull);
    setSkullMask(selectedSkulls ^ bit);
}

void GameSim::setUltimate(Ultimate ultimate) {
    if (tickCount == 0 && enemyList.empty()) {
        selectedUltimate = ultimate;
        ultimateMaxCooldown = content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)];
    }
}

void GameSim::setAutoUltimate(bool enabled) {
    if (tickCount == 0 && enemyList.empty()) automaticUltimate = enabled;
}

void GameSim::setSkin(TowerSkin skin) {
    if (tickCount == 0 && enemyList.empty()) selectedSkin = skin;
}

void GameSim::setArena(Arena arena) {
    if (tickCount == 0 && enemyList.empty()) selectedArena = arena;
}

float GameSim::pathY(float x, int enemyId) const {
    const std::size_t index = static_cast<std::size_t>(selectedArena);
    const float idOffset = index == 0 ? 37.0f : (index == 1 ? 23.0f : 51.0f);
    return 360.0f + std::sin((x + enemyId * idOffset) * content.arenaPathFrequency[index]) * content.arenaPathAmplitude[index];
}

void GameSim::spawnWaveIfNeeded() {
    if (spawnedThisWave != 0 || !enemyList.empty() || wave > 10) return;
    waveSpawnTarget = content.waveEnemyBudget[static_cast<std::size_t>(wave - 1)];
    // Swarm increases regular-wave pressure, but the final wave is authored as
    // a single boss encounter and must stay that way under every modifier.
    if (hasSkull(Skull::Swarm) && wave < 10) {
        waveSpawnTarget = static_cast<int>(std::ceil(static_cast<float>(waveSpawnTarget) * content.skullSpawnScale[static_cast<std::size_t>(Skull::Swarm)]));
    }
    spawnedThisWave = 0;
}

void GameSim::spawnEnemy(bool boss) {
    Enemy enemy;
    enemy.id = nextEnemyId++;
    enemy.pos = {92.0f, pathY(92.0f, enemy.id)};
    enemy.boss = boss;
    const float scale = (1.0f + static_cast<float>(wave - 1) * 0.15f) * content.arenaHealthScale[static_cast<std::size_t>(selectedArena)];
    enemy.type = EnemyType::Boss;
    if (!boss) {
        enemy.type = EnemyType::Grunt;
        const auto& weights = content.waveEnemyTypeWeight[static_cast<std::size_t>(std::clamp(wave, 1, 10) - 1)];
        float totalWeight = 0.0f;
        for (std::size_t type = 0; type < 6; ++type) totalWeight += weights[type];
        if (totalWeight > 0.0f) {
            float roll = random01() * totalWeight;
            for (std::size_t type = 0; type < 6; ++type) {
                roll -= weights[type];
                if (roll <= 0.0f) { enemy.type = static_cast<EnemyType>(type); break; }
            }
        }
    }
    const std::size_t enemyIndex = static_cast<std::size_t>(enemy.type);
    enemy.radius = content.enemyRadius[enemyIndex];
    enemy.damageResistance = content.enemyDamageResistance[enemyIndex];
    enemy.teleportCooldown = content.enemyTeleportCooldown[enemyIndex];
    enemy.maxHp = (boss ? 100.0f : 70.0f + random01() * 35.0f) * content.enemyHealthScale[enemyIndex] * scale;
    enemy.hp = enemy.maxHp;
    const float arenaSpeed = content.arenaSpeedScale[static_cast<std::size_t>(selectedArena)];
    // A boss uses a fixed base so its authored 0.42 speed scale preserves the
    // original 18 px/s phase-one pace without consuming another RNG value.
    const float baseSpeed = boss ? 42.857143f : 42.0f + random01() * 20.0f;
    enemy.speed = baseSpeed * content.enemySpeedScale[enemyIndex] * arenaSpeed * (hasSkull(Skull::Haste) ? content.skullSpeedScale[static_cast<std::size_t>(Skull::Haste)] : 1.0f);
    enemyList.push_back(enemy);
    ++spawnedThisWave;
}

void GameSim::updateEnemies() {
    for (Enemy& enemy : enemyList) {
        if (!enemy.alive) continue;
        if (enemy.stun > 0.0f) enemy.stun -= 1.0f / TickRate;
        if (enemy.slow > 0.0f) enemy.slow -= 1.0f / TickRate;
        if (enemy.teleportCooldown > 0.0f) enemy.teleportCooldown -= 1.0f / TickRate;
        if (enemy.burn > 0.0f) {
            enemy.burn -= 1.0f / TickRate;
            if (enemy.burnTicks-- % 5 == 0) applyDamage(enemy, enemy.burnDps / TickRate * 5.0f);
        }
        if (enemy.poison > 0.0f) {
            enemy.poison -= 1.0f / TickRate;
            if (enemy.poisonTicks-- % 7 == 0) applyDamage(enemy, enemy.poisonDps / TickRate * 7.0f);
        }
        if (!enemy.alive || enemy.stun > 0.0f) continue;
        if (enemy.boss && enemy.phase == 1 && enemy.hp <= enemy.maxHp * 0.55f) {
            enemy.phase = 2;
            const float arenaSpeed = content.arenaSpeedScale[static_cast<std::size_t>(selectedArena)];
            enemy.speed = 66.666664f * content.enemySpeedScale[static_cast<std::size_t>(EnemyType::Boss)] * arenaSpeed * (hasSkull(Skull::Haste) ? content.skullSpeedScale[static_cast<std::size_t>(Skull::Haste)] : 1.0f);
            enemy.damageResistance = 0.20f;
        }
        if (enemy.type == EnemyType::Teleporter && enemy.teleportCooldown <= 0.0f) {
            enemy.pos.x = std::max(92.0f, enemy.pos.x - 115.0f);
            enemy.teleportCooldown = content.enemyTeleportCooldown[static_cast<std::size_t>(EnemyType::Teleporter)] + random01() * 1.5f;
        }
        const float slowFactor = enemy.slow > 0.0f ? 0.45f : 1.0f;
        enemy.pos.x += enemy.speed * slowFactor / TickRate;
        enemy.pos.y = pathY(enemy.pos.x, enemy.id);
        if (enemy.pos.x >= ExitX) {
            enemy.alive = false;
            --lives;
            ++counters.leaks;
            if (lives <= 0) gameOver = true;
        }
    }
    enemyList.erase(std::remove_if(enemyList.begin(), enemyList.end(), [](const Enemy& e) { return !e.alive; }), enemyList.end());
}

void GameSim::damageArea(Vec2 center, float radius, float damage, bool burn) {
    const float radiusSq = radius * radius;
    for (Enemy& enemy : enemyList) {
        if (!enemy.alive || distanceSquared(center, enemy.pos) > radiusSq) continue;
        applyDamage(enemy, damage);
        if (burn) {
            enemy.burn = upgradeValueA(Upgrade::FireballShells);
            enemy.burnDps = upgradeValueB(Upgrade::FireballShells) + wave;
            enemy.burnTicks = static_cast<int>(TickRate * upgradeValueA(Upgrade::FireballShells));
        }
    }
}

void GameSim::applyDamage(Enemy& enemy, float damage) {
    if (!enemy.alive) return;
    enemy.hp -= damage * (1.0f - enemy.damageResistance);
    if (enemy.hp <= 0.0f) resolveDeath(enemy);
}

void GameSim::resolveDeath(Enemy& enemy) {
    if (!enemy.alive) return;
    enemy.alive = false;
    ++counters.kills;
    currency += enemy.boss ? 100 : 5;
    if (hasSkull(Skull::Greed)) currency += enemy.boss ? content.skullBossCurrencyBonus[static_cast<std::size_t>(Skull::Greed)] : content.skullCurrencyBonus[static_cast<std::size_t>(Skull::Greed)];
    counters.score += static_cast<int>((enemy.boss ? 1000 : 100) * skullScoreMultiplier());
    if (hasUpgrade(Upgrade::BlackHole) && !enemy.boss) damageArea(enemy.pos, upgradeValueA(Upgrade::BlackHole), upgradeValueB(Upgrade::BlackHole), false);
}

void GameSim::chainDamage(Vec2 origin, int sourceId, float damage) {
    // Chain lightning selects the two nearest distinct targets, making the effect
    // deterministic regardless of vector insertion order.
    int visitedFirst = sourceId;
    int visitedSecond = -1;
    for (int jump = 0; jump < 2; ++jump) {
        Enemy* best = nullptr;
        float bestDistance = 210.0f * 210.0f;
        for (Enemy& candidate : enemyList) {
            if (!candidate.alive || candidate.id == visitedFirst || candidate.id == visitedSecond) continue;
            const float distance = distanceSquared(origin, candidate.pos);
            if (distance < bestDistance) { bestDistance = distance; best = &candidate; }
        }
        if (best == nullptr) return;
        applyDamage(*best, damage);
        if (hasUpgrade(Upgrade::FreezingBlast)) best->slow = upgradeValueA(Upgrade::FreezingBlast);
        origin = best->pos;
        visitedSecond = best->id;
    }
}

void GameSim::updateProjectiles() {
    for (Projectile& projectile : projectileList) {
        if (!projectile.alive) continue;
        projectile.pos.x += projectile.velocity.x / TickRate;
        projectile.pos.y += projectile.velocity.y / TickRate;
        if (projectile.pos.x < 0.0f || projectile.pos.x > Width || projectile.pos.y < 0.0f || projectile.pos.y > Height) {
            projectile.alive = false;
            continue;
        }
        for (Enemy& enemy : enemyList) {
            if (!enemy.alive || distanceSquared(projectile.pos, enemy.pos) > (projectile.radius + enemy.radius) * (projectile.radius + enemy.radius)) continue;
            applyDamage(enemy, projectile.damage);
            if (hasUpgrade(Upgrade::BurningShot)) {
                enemy.burn = upgradeValueA(Upgrade::BurningShot);
                enemy.burnDps = upgradeValueB(Upgrade::BurningShot) + wave;
                enemy.burnTicks = static_cast<int>(TickRate * upgradeValueA(Upgrade::BurningShot));
            }
            if (hasUpgrade(Upgrade::PoisonCoil)) {
                enemy.poison = upgradeValueA(Upgrade::PoisonCoil);
                enemy.poisonDps = upgradeValueB(Upgrade::PoisonCoil) + wave * 0.5f;
                enemy.poisonTicks = static_cast<int>(TickRate * upgradeValueA(Upgrade::PoisonCoil));
            }
            if (hasUpgrade(Upgrade::FreezingBlast)) enemy.slow = upgradeValueA(Upgrade::FreezingBlast);
            if (hasUpgrade(Upgrade::TeleportTrap)) {
                enemy.pos.x = std::max(92.0f, enemy.pos.x - upgradeValueA(Upgrade::TeleportTrap));
                if (hasUpgrade(Upgrade::PoisonCoil)) applyDamage(enemy, upgradeValueB(Upgrade::TeleportTrap) + wave * 2.0f);
            }
            if (hasUpgrade(Upgrade::ChainLightning)) chainDamage(enemy.pos, enemy.id, projectile.damage * ((hasUpgrade(Upgrade::FreezingBlast) && enemy.slow > 0.0f) ? upgradeValueB(Upgrade::ChainLightning) : upgradeValueA(Upgrade::ChainLightning)));
            if (hasUpgrade(Upgrade::Shockwave) && projectile.explosive) {
                for (Enemy& nearby : enemyList) {
                    if (nearby.alive && nearby.id != enemy.id && distanceSquared(nearby.pos, enemy.pos) <= upgradeValueB(Upgrade::Shockwave) * upgradeValueB(Upgrade::Shockwave)) nearby.stun = upgradeValueA(Upgrade::Shockwave);
                }
            }
            if (projectile.explosive) damageArea(enemy.pos, hasUpgrade(Upgrade::ClusterBombs) ? upgradeValueA(Upgrade::ClusterBombs) : 68.0f, projectile.damage * (hasUpgrade(Upgrade::ClusterBombs) ? upgradeValueB(Upgrade::ClusterBombs) : 0.45f), hasUpgrade(Upgrade::FireballShells));
            if (hasUpgrade(Upgrade::BurningShot) && hasUpgrade(Upgrade::WindShear)) {
                // Fire + wind: a secondary tornado-like ring that burns nearby targets.
                damageArea(enemy.pos, upgradeValueA(Upgrade::WindShear), projectile.damage * upgradeValueB(Upgrade::WindShear), true);
            }
            if (projectile.pierces > 0) { --projectile.pierces; continue; }
            if (projectile.bounces > 0) { --projectile.bounces; projectile.velocity.x *= -0.8f; continue; }
            projectile.alive = false;
            break;
        }
    }
    projectileList.erase(std::remove_if(projectileList.begin(), projectileList.end(), [](const Projectile& p) { return !p.alive; }), projectileList.end());
}

void GameSim::fireWeapon() {
    if (fireCooldown > 0) { --fireCooldown; return; }
    Enemy* target = nullptr;
    for (Enemy& enemy : enemyList) {
        if (!enemy.alive || distanceSquared(enemy.pos, {TowerX, TowerY}) > 760.0f * 760.0f) continue;
        if (target == nullptr || enemy.pos.x > target->pos.x) target = &enemy;
    }
    if (target == nullptr) return;
    Projectile projectile;
    projectile.pos = {TowerX, TowerY};
    const float dx = target->pos.x - TowerX;
    const float dy = target->pos.y - TowerY;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float damageScale = 1.0f + (hasUpgrade(Upgrade::Scavenger) ? upgradeValueA(Upgrade::Scavenger) : 0.0f);
    switch (selectedWeapon) {
        case Weapon::RapidFire:
            projectile.damage = content.weaponDamage[0] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[0], dy / length * content.projectileSpeed[0]};
            projectile.pierces = hasUpgrade(Upgrade::PiercingShots) ? static_cast<int>(upgradeValueA(Upgrade::PiercingShots)) : 0;
            projectile.bounces = hasUpgrade(Upgrade::Ricochet) ? static_cast<int>(upgradeValueA(Upgrade::Ricochet)) : 0;
            fireCooldown = hasUpgrade(Upgrade::Overclock) ? std::max(1, static_cast<int>(content.weaponCooldown[0] / upgradeValueA(Upgrade::Overclock))) : content.weaponCooldown[0];
            break;
        case Weapon::ExplosiveCannon:
            projectile.damage = content.weaponDamage[1] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[1], dy / length * content.projectileSpeed[1]};
            projectile.explosive = true;
            fireCooldown = content.weaponCooldown[1];
            break;
        case Weapon::ArcaneBeam:
            damageArea(target->pos, 35.0f, content.weaponDamage[2] * damageScale, false);
            if (hasUpgrade(Upgrade::ChainLightning)) chainDamage(target->pos, target->id, content.weaponDamage[2] * (target->slow > 0.0f ? upgradeValueB(Upgrade::ChainLightning) : upgradeValueA(Upgrade::ChainLightning)));
            fireCooldown = content.weaponCooldown[2];
            if (bulletStormTicks > 0) fireCooldown = std::max(1, fireCooldown / 3);
            ++counters.shotsFired;
            return;
        case Weapon::FrostBlaster:
            projectile.damage = content.weaponDamage[3] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[3], dy / length * content.projectileSpeed[3]};
            projectile.explosive = true;
            fireCooldown = content.weaponCooldown[3];
            break;
        case Weapon::SniperRailgun:
            projectile.damage = content.weaponDamage[4] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[4], dy / length * content.projectileSpeed[4]};
            projectile.pierces = hasUpgrade(Upgrade::PiercingShots) ? static_cast<int>(upgradeValueA(Upgrade::PiercingShots) + 1.0f) : 0;
            fireCooldown = hasUpgrade(Upgrade::Overclock) ? std::max(1, static_cast<int>(content.weaponCooldown[4] / upgradeValueA(Upgrade::Overclock))) : content.weaponCooldown[4];
            break;
    }
    if (bulletStormTicks > 0) fireCooldown = std::max(1, fireCooldown / 3);
    ++counters.shotsFired;
    projectileList.push_back(projectile);
}

void GameSim::createUpgradeChoices() {
    choices.clear();
    const Upgrade pool[] = {Upgrade::PiercingShots, Upgrade::Ricochet, Upgrade::Overclock, Upgrade::ClusterBombs,
                            Upgrade::Shockwave, Upgrade::FireballShells, Upgrade::ChainLightning, Upgrade::FreezingBlast,
                            Upgrade::BurningShot, Upgrade::BlackHole, Upgrade::EmergencyRepair, Upgrade::Scavenger,
                            Upgrade::WindShear, Upgrade::PoisonCoil, Upgrade::TeleportTrap};
    while (choices.size() < 3) {
        float totalWeight = 0.0f;
        const auto affinity = [this](Upgrade upgrade) {
            switch (upgrade) {
                case Upgrade::PiercingShots:
                case Upgrade::Ricochet:
                case Upgrade::Overclock:
                    return (selectedWeapon == Weapon::RapidFire || selectedWeapon == Weapon::SniperRailgun) ? 1.8f : 0.65f;
                case Upgrade::ClusterBombs:
                case Upgrade::Shockwave:
                case Upgrade::FireballShells:
                    return selectedWeapon == Weapon::ExplosiveCannon ? 1.8f : 0.75f;
                case Upgrade::ChainLightning:
                    return (selectedWeapon == Weapon::ArcaneBeam || selectedWeapon == Weapon::FrostBlaster) ? 1.7f : 0.8f;
                case Upgrade::FreezingBlast:
                    return selectedWeapon == Weapon::FrostBlaster ? 1.9f : 0.75f;
                case Upgrade::BurningShot:
                    return (selectedWeapon == Weapon::ExplosiveCannon || selectedWeapon == Weapon::ArcaneBeam) ? 1.5f : 0.85f;
                case Upgrade::WindShear:
                    return selectedWeapon == Weapon::ExplosiveCannon ? 1.5f : 0.85f;
                case Upgrade::BlackHole:
                case Upgrade::EmergencyRepair:
                case Upgrade::Scavenger:
                case Upgrade::PoisonCoil:
                case Upgrade::TeleportTrap:
                    return 1.0f;
            }
            return 1.0f;
        };
        bool selected = false;
        for (std::size_t index = 0; index < 15; ++index) {
            const Upgrade candidate = pool[index];
            if (!hasUpgrade(candidate) && std::find(choices.begin(), choices.end(), candidate) == choices.end()) totalWeight += content.upgradeWeight[index] * affinity(candidate);
        }
        if (totalWeight <= 0.0f) break;
        float roll = random01() * totalWeight;
        for (std::size_t index = 0; index < 15; ++index) {
            const Upgrade candidate = pool[index];
            if (hasUpgrade(candidate) || std::find(choices.begin(), choices.end(), candidate) != choices.end()) continue;
            roll -= content.upgradeWeight[index] * affinity(candidate);
            if (roll <= 0.0f) { choices.push_back(candidate); selected = true; break; }
        }
        if (!selected) break;
    }
    if (choices.size() == 3) upgradeChoicePending = true;
}

void GameSim::applyUpgrade(Upgrade upgrade) {
    ownedUpgrades.push_back(upgrade);
    ++counters.upgrades;
    if (upgrade == Upgrade::EmergencyRepair) lives = std::min(20, lives + static_cast<int>(upgradeValueA(Upgrade::EmergencyRepair)));
    if (upgrade == Upgrade::Scavenger) currency += static_cast<int>(upgradeValueB(Upgrade::Scavenger));
}

void GameSim::chooseUpgrade(int choice) {
    if (!upgradeChoicePending || choice < 0 || choice >= static_cast<int>(choices.size())) return;
    applyUpgrade(choices[choice]);
    choices.clear();
    upgradeChoicePending = false;
}

void GameSim::activateUltimate() {
    if (ultimateCooldown > 0 || gameOver || victory || upgradeChoicePending) return;
    ++counters.ultimates;
    ultimateCooldown = ultimateMaxCooldown;
    const float damage = (125.0f + wave * 22.0f) * content.ultimateDamageScale[static_cast<std::size_t>(selectedUltimate)];
    switch (selectedUltimate) {
        case Ultimate::MeteorRain:
            damageArea({420.0f, 300.0f}, 260.0f, damage, true);
            damageArea({680.0f, 420.0f}, 260.0f, damage, true);
            damageArea({880.0f, 290.0f}, 220.0f, damage * 0.8f, true);
            break;
        case Ultimate::BulletStorm:
            bulletStormTicks = TickRate * 5;
            damageArea({650.0f, 360.0f}, 480.0f, damage * 0.25f, false);
            break;
        case Ultimate::AbsoluteZero:
            for (Enemy& enemy : enemyList) { enemy.slow = 6.0f; enemy.stun = 3.0f; applyDamage(enemy, damage * 0.35f); }
            break;
        case Ultimate::GravityShift:
            for (Enemy& enemy : enemyList) { enemy.pos.x = std::max(92.0f, enemy.pos.x - 180.0f); enemy.stun = 1.5f; }
            damageArea({650.0f, 360.0f}, 260.0f, damage * 0.45f, false);
            break;
        case Ultimate::EnergySurge:
            damageArea({650.0f, 360.0f}, 520.0f, damage * 1.15f, selectedWeapon == Weapon::ExplosiveCannon);
            currency += 25;
            break;
    }
}

void GameSim::tick() {
    if (gameOver || victory || upgradeChoicePending) return;
    ++tickCount;
    counters.ticks = tickCount;
    if (ultimateCooldown > 0) --ultimateCooldown;
    if (bulletStormTicks > 0) --bulletStormTicks;
    spawnWaveIfNeeded();
    if (wave <= 10 && spawnedThisWave < waveSpawnTarget) {
        if (spawnCooldown <= 0) {
            spawnEnemy(wave == 10 && spawnedThisWave == 0);
            spawnCooldown = std::max(4, content.waveSpawnInterval[static_cast<std::size_t>(wave - 1)]);
        } else --spawnCooldown;
    }
    updateEnemies();
    updateProjectiles();
    fireWeapon();
    if (automaticUltimate && ultimateCooldown == 0 && !upgradeChoicePending) {
        bool bossPhaseTrigger = false;
        for (const Enemy& enemy : enemyList) if (enemy.boss && enemy.phase >= 2) { bossPhaseTrigger = true; break; }
        if (enemyList.size() >= 8 || bossPhaseTrigger) activateUltimate();
    }
    if (enemyList.empty() && spawnedThisWave >= waveSpawnTarget) {
        if (wave >= 10) { victory = true; counters.wave = wave; return; }
        ++wave;
        counters.wave = wave;
        spawnedThisWave = 0;
        spawnCooldown = 0;
        createUpgradeChoices();
    }
    counters.wave = wave;
    counters.score += std::max(0, wave - 1);
}

float GameSim::ultimateRatio() const { return 1.0f - static_cast<float>(ultimateCooldown) / ultimateMaxCooldown; }

int GameSim::enemiesRemaining() const { return static_cast<int>(enemyList.size()); }

std::uint32_t GameSim::stateHash() const {
    std::uint32_t hash = 2166136261u;
    auto add = [&hash](std::uint32_t value) { hash ^= value; hash *= 16777619u; };
    auto addFloat = [&add](float value) { std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); add(bits); };
    auto addBool = [&add](bool value) { add(value ? 1u : 0u); };
    auto addSize = [&add](std::size_t value) { add(static_cast<std::uint32_t>(value)); };
    add(seed); add(rngState); add(static_cast<std::uint32_t>(tickCount)); add(static_cast<std::uint32_t>(wave));
    add(static_cast<std::uint32_t>(lives)); add(static_cast<std::uint32_t>(currency));
    add(static_cast<std::uint32_t>(spawnedThisWave)); add(static_cast<std::uint32_t>(waveSpawnTarget));
    add(static_cast<std::uint32_t>(spawnCooldown)); add(static_cast<std::uint32_t>(fireCooldown));
    add(static_cast<std::uint32_t>(ultimateCooldown)); add(static_cast<std::uint32_t>(ultimateMaxCooldown)); add(static_cast<std::uint32_t>(bulletStormTicks));
    add(static_cast<std::uint32_t>(nextEnemyId));
    addBool(gameOver); addBool(victory); addBool(upgradeChoicePending); addBool(automaticUltimate);
    add(static_cast<std::uint32_t>(selectedWeapon)); add(static_cast<std::uint32_t>(selectedSkull)); add(static_cast<std::uint32_t>(selectedSkulls));
    add(static_cast<std::uint32_t>(selectedUltimate)); add(static_cast<std::uint32_t>(selectedArena));
    add(static_cast<std::uint32_t>(counters.ticks)); add(static_cast<std::uint32_t>(counters.wave)); add(static_cast<std::uint32_t>(counters.kills));
    add(static_cast<std::uint32_t>(counters.leaks)); add(static_cast<std::uint32_t>(counters.upgrades)); add(static_cast<std::uint32_t>(counters.ultimates)); add(static_cast<std::uint32_t>(counters.shotsFired)); add(static_cast<std::uint32_t>(counters.score));
    addSize(enemyList.size());
    for (const Enemy& enemy : enemyList) {
        add(static_cast<std::uint32_t>(enemy.id)); add(static_cast<std::uint32_t>(enemy.type)); add(static_cast<std::uint32_t>(enemy.phase));
        addBool(enemy.boss); addBool(enemy.alive); addFloat(enemy.pos.x); addFloat(enemy.pos.y); addFloat(enemy.hp); addFloat(enemy.maxHp); addFloat(enemy.speed); addFloat(enemy.radius);
        addFloat(enemy.slow); addFloat(enemy.stun); addFloat(enemy.burn); addFloat(enemy.burnDps); add(static_cast<std::uint32_t>(enemy.burnTicks));
        addFloat(enemy.poison); addFloat(enemy.poisonDps); add(static_cast<std::uint32_t>(enemy.poisonTicks)); addFloat(enemy.damageResistance); addFloat(enemy.teleportCooldown);
    }
    addSize(projectileList.size());
    for (const Projectile& projectile : projectileList) {
        addBool(projectile.alive); addBool(projectile.explosive); addFloat(projectile.pos.x); addFloat(projectile.pos.y); addFloat(projectile.velocity.x); addFloat(projectile.velocity.y);
        addFloat(projectile.damage); addFloat(projectile.radius); add(static_cast<std::uint32_t>(projectile.pierces)); add(static_cast<std::uint32_t>(projectile.bounces));
    }
    addSize(ownedUpgrades.size());
    for (Upgrade upgrade : ownedUpgrades) add(static_cast<std::uint32_t>(upgrade));
    addSize(choices.size());
    for (Upgrade upgrade : choices) add(static_cast<std::uint32_t>(upgrade));
    return hash;
}

std::string GameSim::statusText() const {
    if (gameOver) return "RUN FAILED";
    if (victory) return "TOWER ASCENDED";
    if (upgradeChoicePending) return "CHOOSE UPGRADE";
    return "DEFEND THE TOWER";
}

RunSummary GameSim::runSummary() const {
    RunSummary result;
    result.victory = victory;
    result.arena = selectedArena;
    result.scoreMultiplier = skullScoreMultiplier();
    result.score = counters.score;
    result.wave = counters.wave;
    result.kills = counters.kills;
    result.leaks = counters.leaks;
    result.durationTicks = counters.ticks;
    return result;
}

const char* weaponName(Weapon weapon) {
    switch (weapon) {
        case Weapon::RapidFire: return "RAPID FIRE";
        case Weapon::ExplosiveCannon: return "CANNON";
        case Weapon::ArcaneBeam: return "ARCANE BEAM";
        case Weapon::FrostBlaster: return "FROST";
        case Weapon::SniperRailgun: return "RAILGUN";
    }
    return "UNKNOWN";
}

const char* skullName(Skull skull) {
    switch (skull) {
        case Skull::None: return "NONE";
        case Skull::Swarm: return "SWARM";
        case Skull::GlassCannon: return "GLASS CANNON";
        case Skull::Haste: return "HASTE";
        case Skull::Greed: return "GREED";
    }
    return "UNKNOWN";
}

const char* upgradeName(Upgrade upgrade) {
    switch (upgrade) {
        case Upgrade::PiercingShots: return "PIERCE";
        case Upgrade::Ricochet: return "RICOCHET";
        case Upgrade::Overclock: return "OVERCLOCK";
        case Upgrade::ClusterBombs: return "CLUSTER";
        case Upgrade::Shockwave: return "SHOCKWAVE";
        case Upgrade::FireballShells: return "FIREBALL";
        case Upgrade::ChainLightning: return "CHAIN LIGHTNING";
        case Upgrade::FreezingBlast: return "FREEZE";
        case Upgrade::BurningShot: return "BURNING";
        case Upgrade::BlackHole: return "BLACK HOLE";
        case Upgrade::EmergencyRepair: return "REPAIR";
        case Upgrade::Scavenger: return "SCAVENGER";
        case Upgrade::WindShear: return "WIND SHEAR";
        case Upgrade::PoisonCoil: return "POISON COIL";
        case Upgrade::TeleportTrap: return "TELEPORT TRAP";
    }
    return "UNKNOWN";
}

const char* upgradeDescription(Upgrade upgrade) {
    switch (upgrade) {
        case Upgrade::PiercingShots: return "SHOTS PASS TARGETS";
        case Upgrade::Ricochet: return "SHOTS BOUNCE ON HIT";
        case Upgrade::Overclock: return "FIRE RATE DOUBLES";
        case Upgrade::ClusterBombs: return "WIDER SPLASH BLASTS";
        case Upgrade::Shockwave: return "IMPACTS STUN NEARBY";
        case Upgrade::FireballShells: return "SHELLS LEAVE BURN";
        case Upgrade::ChainLightning: return "JUMPS TO TWO FOES";
        case Upgrade::FreezingBlast: return "HITS SLOW ENEMIES";
        case Upgrade::BurningShot: return "DAMAGE OVER TIME";
        case Upgrade::BlackHole: return "DEATH PULLS FOES";
        case Upgrade::EmergencyRepair: return "RESTORE FOUR LIVES";
        case Upgrade::Scavenger: return "DAMAGE AND CREDITS";
        case Upgrade::WindShear: return "AMPLIFIES FIRE AREA";
        case Upgrade::PoisonCoil: return "POISON AND DISPLACE";
        case Upgrade::TeleportTrap: return "PUSH FOES BACK";
    }
    return "UNKNOWN EFFECT";
}

const char* enemyTypeName(EnemyType type) {
    switch (type) {
        case EnemyType::Grunt: return "GRUNT";
        case EnemyType::Runner: return "RUNNER";
        case EnemyType::Tank: return "TANK";
        case EnemyType::Shielded: return "SHIELDED";
        case EnemyType::Swarmling: return "SWARMLING";
        case EnemyType::Teleporter: return "TELEPORTER";
        case EnemyType::Boss: return "BOSS";
    }
    return "UNKNOWN";
}

const char* ultimateName(Ultimate ultimate) {
    switch (ultimate) {
        case Ultimate::MeteorRain: return "METEOR RAIN";
        case Ultimate::BulletStorm: return "BULLET STORM";
        case Ultimate::AbsoluteZero: return "ABSOLUTE ZERO";
        case Ultimate::GravityShift: return "GRAVITY SHIFT";
        case Ultimate::EnergySurge: return "ENERGY SURGE";
    }
    return "UNKNOWN";
}

const char* towerSkinName(TowerSkin skin) {
    switch (skin) {
        case TowerSkin::Azure: return "AZURE";
        case TowerSkin::Ember: return "EMBER";
        case TowerSkin::Nebula: return "NEBULA";
        case TowerSkin::Verdant: return "VERDANT";
        case TowerSkin::Gold: return "GOLD";
    }
    return "UNKNOWN";
}

const char* arenaName(Arena arena) {
    switch (arena) {
        case Arena::Moonbase: return "MOONBASE";
        case Arena::EmberCrater: return "EMBER CRATER";
        case Arena::NeonRuins: return "NEON RUINS";
    }
    return "UNKNOWN";
}

} // namespace ta
