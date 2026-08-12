#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace ta {

enum class Weapon { RapidFire, ExplosiveCannon, ArcaneBeam, FrostBlaster, SniperRailgun };
enum class Upgrade { PiercingShots, Ricochet, Overclock, ClusterBombs, Shockwave, FireballShells,
                     ChainLightning, FreezingBlast, BurningShot, BlackHole, EmergencyRepair, Scavenger,
                     WindShear, PoisonCoil, TeleportTrap };
enum class Skull { None, Swarm, GlassCannon, Haste, Greed };
using SkullMask = std::uint8_t;
enum class EnemyType { Grunt, Runner, Tank, Shielded, Swarmling, Teleporter, Boss };
enum class Ultimate { MeteorRain, BulletStorm, AbsoluteZero, GravityShift, EnergySurge };
enum class TowerSkin { Azure, Ember, Nebula, Verdant, Gold };
enum class Arena { Moonbase, EmberCrater, NeonRuins };

struct Vec2 { float x = 0.0f, y = 0.0f; };

struct Enemy {
    int id = 0;
    Vec2 pos{};
    float hp = 0.0f;
    float maxHp = 0.0f;
    float speed = 0.0f;
    float radius = 14.0f;
    float slow = 0.0f;
    float stun = 0.0f;
    float burn = 0.0f;
    float burnDps = 0.0f;
    int burnTicks = 0;
    float poison = 0.0f;
    float poisonDps = 0.0f;
    int poisonTicks = 0;
    int attackCooldownTicks = 0;
    int telegraphTicks = 0;
    EnemyType type = EnemyType::Grunt;
    float damageResistance = 0.0f;
    float teleportCooldown = 0.0f;
    int phase = 1;
    bool boss = false;
    bool alive = true;
};

struct Projectile {
    Vec2 pos{};
    Vec2 velocity{};
    float damage = 0.0f;
    float radius = 5.0f;
    int pierces = 0;
    int bounces = 0;
    bool explosive = false;
    bool alive = true;
};

struct SimStats {
    int ticks = 0;
    int wave = 1;
    int kills = 0;
    int leaks = 0;
    int upgrades = 0;
    int ultimates = 0;
    int shotsFired = 0;
    int bossAttacks = 0;
    int score = 0;
};

struct RunSummary {
    bool victory = false;
    Arena arena = Arena::Moonbase;
    float scoreMultiplier = 1.0f;
    int score = 0;
    int wave = 1;
    int kills = 0;
    int leaks = 0;
    int durationTicks = 0;
};

struct ContentConfig {
    std::array<float, 5> weaponDamage{{18.0f, 88.0f, 23.0f, 12.0f, 260.0f}};
    std::array<int, 5> weaponCooldown{{6, 24, 2, 14, 42}};
    std::array<float, 5> projectileSpeed{{720.0f, 440.0f, 0.0f, 540.0f, 1200.0f}};
    std::array<int, 5> ultimateCooldownTicks{{540, 540, 540, 540, 540}};
    std::array<float, 5> ultimateDamageScale{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<int, 10> waveEnemyBudget{{9, 11, 13, 15, 17, 19, 21, 23, 25, 1}};
    std::array<int, 10> waveSpawnInterval{{18, 17, 16, 15, 14, 13, 12, 11, 10, 1}};
    // Per-wave enemy mix weights in EnemyType enum order. Bosses are spawned by
    // the final-wave rule and their authored weight is reserved for tooling.
    std::array<std::array<float, 7>, 10> waveEnemyTypeWeight{{
        {{100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
        {{80.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
        {{65.0f, 20.0f, 15.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
        {{60.0f, 15.0f, 15.0f, 10.0f, 0.0f, 0.0f, 0.0f}},
        {{50.0f, 15.0f, 15.0f, 10.0f, 10.0f, 0.0f, 0.0f}},
        {{45.0f, 15.0f, 15.0f, 10.0f, 10.0f, 5.0f, 0.0f}},
        {{40.0f, 15.0f, 15.0f, 10.0f, 10.0f, 10.0f, 0.0f}},
        {{35.0f, 15.0f, 20.0f, 10.0f, 10.0f, 10.0f, 0.0f}},
        {{30.0f, 15.0f, 20.0f, 12.0f, 10.0f, 13.0f, 0.0f}},
        {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f}}
    }};
    std::array<float, 5> skullScoreMultiplier{{1.0f, 1.25f, 1.40f, 1.30f, 1.50f}};
    std::array<float, 5> skullSpawnScale{{1.0f, 1.50f, 1.0f, 1.0f, 1.0f}};
    std::array<int, 5> skullLives{{20, 20, 10, 20, 20}};
    std::array<float, 5> skullSpeedScale{{1.0f, 1.0f, 1.0f, 1.25f, 1.0f}};
    std::array<int, 5> skullCurrencyBonus{{0, 0, 0, 0, 3}};
    std::array<int, 5> skullBossCurrencyBonus{{0, 0, 0, 0, 50}};
    std::array<float, 15> upgradeWeight{{1.0f, 1.0f, 0.9f, 1.0f, 0.9f, 0.85f, 0.8f, 1.0f, 0.9f, 0.75f, 0.8f, 1.0f, 0.8f, 0.8f, 0.75f}};
    // Generic authored magnitudes per upgrade. Their meaning is documented in
    // upgrades.json and interpreted by the tested combat primitives.
    std::array<float, 15> upgradeValueA{{2.0f, 1.0f, 2.0f, 92.0f, 1.25f, 4.0f, 0.42f, 2.0f, 3.0f, 85.0f, 4.0f, 0.12f, 125.0f, 5.0f, 90.0f}};
    std::array<float, 15> upgradeValueB{{0.0f, 0.0f, 0.0f, 0.45f, 115.0f, 13.0f, 0.70f, 0.45f, 11.0f, 12.0f, 0.0f, 20.0f, 0.25f, 7.0f, 18.0f}};
    std::array<float, 3> arenaHealthScale{{1.0f, 1.15f, 1.0f}};
    std::array<float, 3> arenaSpeedScale{{0.94f, 1.0f, 1.12f}};
    std::array<float, 3> arenaPathAmplitude{{120.0f, 155.0f, 82.0f}};
    std::array<float, 3> arenaPathFrequency{{0.012f, 0.009f, 0.018f}};
    // Enemy tuning is authored in enemies.json in EnemyType enum order.
    std::array<float, 7> enemyHealthScale{{1.0f, 0.60f, 2.35f, 1.45f, 0.36f, 1.10f, 18.0f}};
    std::array<float, 7> enemySpeedScale{{1.0f, 1.75f, 0.55f, 0.85f, 1.35f, 1.0f, 0.42f}};
    std::array<float, 7> enemyDamageResistance{{0.0f, 0.0f, 0.0f, 0.45f, 0.0f, 0.0f, 0.0f}};
    std::array<float, 7> enemyRadius{{14.0f, 11.0f, 21.0f, 17.0f, 8.0f, 15.0f, 34.0f}};
    std::array<float, 7> enemyTeleportCooldown{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.5f, 0.0f}};
    int bossAttackCooldownTicks = 450;
    int bossTelegraphTicks = 15;
    int bossAttackLives = 2;
};

bool loadContentConfig(const std::string& directory, ContentConfig& output, std::string* error = nullptr);
std::string defaultContentDirectory();
std::uint32_t contentFingerprint(const ContentConfig& content);

class GameSim {
public:
    // Combat uses compact, deterministic world units. The client maps this
    // space onto the larger design canvas required by the art/UI specification.
    static constexpr int Width = 1280;
    static constexpr int Height = 720;
    static constexpr int DesignWidth = 1920;
    static constexpr int DesignHeight = 1080;
    static constexpr float WorldScale = 1.5f;
    static constexpr int TickRate = 30;

    explicit GameSim(std::uint32_t seed = 0xC0FFEEu);

    void reset(std::uint32_t seed);
    void tick();
    void activateUltimate();
    void chooseUpgrade(int choice);
    void setWeapon(Weapon weapon);
    void setSkull(Skull skull);
    void toggleSkull(Skull skull);
    void setSkullMask(SkullMask mask);
    void setUltimate(Ultimate ultimate);
    void setAutoUltimate(bool enabled);
    void setSkin(TowerSkin skin);
    void setArena(Arena arena);
    void setContentConfig(const ContentConfig& content);

    bool isGameOver() const { return gameOver; }
    bool isVictory() const { return victory; }
    bool upgradePending() const { return upgradeChoicePending; }
    float ultimateRatio() const;
    int waveNumber() const { return wave; }
    int currencyAmount() const { return currency; }
    int livesRemaining() const { return lives; }
    int enemiesRemaining() const;
    int enemiesSpawnedThisWave() const { return spawnedThisWave; }
    int enemiesTargetThisWave() const { return waveSpawnTarget; }
    const SimStats& stats() const { return counters; }
    Weapon weapon() const { return selectedWeapon; }
    Skull skull() const { return selectedSkull; }
    SkullMask skullMask() const { return selectedSkulls; }
    bool hasSkull(Skull skull) const;
    float skullScoreMultiplier() const;
    Ultimate ultimate() const { return selectedUltimate; }
    bool autoUltimate() const { return automaticUltimate; }
    TowerSkin skin() const { return selectedSkin; }
    Arena arena() const { return selectedArena; }
    std::uint32_t initialSeed() const { return seed; }
    const std::vector<Enemy>& enemies() const { return enemyList; }
    const std::vector<Projectile>& projectiles() const { return projectileList; }
    const std::vector<Upgrade>& pendingChoices() const { return choices; }
    const std::vector<Upgrade>& upgrades() const { return ownedUpgrades; }
    std::uint32_t stateHash() const;
    std::string statusText() const;
    RunSummary runSummary() const;

private:
    std::uint32_t rngState = 0;
    std::uint32_t nextRandom();
    float random01();
    bool hasUpgrade(Upgrade upgrade) const;
    float upgradeValueA(Upgrade upgrade) const;
    float upgradeValueB(Upgrade upgrade) const;
    void spawnWaveIfNeeded();
    void spawnEnemy(bool boss = false);
    void updateEnemies();
    void updateProjectiles();
    void fireWeapon();
    void resolveDeath(Enemy& enemy);
    void applyDamage(Enemy& enemy, float damage);
    void chainDamage(Vec2 origin, int sourceId, float damage);
    void createUpgradeChoices();
    void applyUpgrade(Upgrade upgrade);
    void damageArea(Vec2 center, float radius, float damage, bool burn);
    float pathY(float x, int enemyId) const;

    std::uint32_t seed = 0;
    int tickCount = 0;
    int wave = 1;
    int lives = 20;
    int currency = 0;
    int spawnedThisWave = 0;
    int waveSpawnTarget = 8;
    int spawnCooldown = 0;
    int fireCooldown = 0;
    int ultimateCooldown = 0;
    int ultimateMaxCooldown = TickRate * 18;
    int nextEnemyId = 1;
    bool gameOver = false;
    bool victory = false;
    bool upgradeChoicePending = false;
    Weapon selectedWeapon = Weapon::RapidFire;
    Skull selectedSkull = Skull::None;
    SkullMask selectedSkulls = 0;
    Ultimate selectedUltimate = Ultimate::MeteorRain;
    bool automaticUltimate = false;
    TowerSkin selectedSkin = TowerSkin::Azure;
    Arena selectedArena = Arena::Moonbase;
    int bulletStormTicks = 0;
    std::vector<Enemy> enemyList;
    std::vector<Projectile> projectileList;
    std::vector<Upgrade> ownedUpgrades;
    std::vector<Upgrade> choices;
    SimStats counters{};
    ContentConfig content{};
};

const char* weaponName(Weapon weapon);
const char* skullName(Skull skull);
const char* upgradeName(Upgrade upgrade);
const char* upgradeDescription(Upgrade upgrade);
const char* enemyTypeName(EnemyType type);
const char* ultimateName(Ultimate ultimate);
const char* towerSkinName(TowerSkin skin);
const char* arenaName(Arena arena);

} // namespace ta
