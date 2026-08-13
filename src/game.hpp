#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <string>
#include <vector>

namespace ta {

enum class Weapon { RapidFire, ExplosiveCannon, ArcaneBeam, FrostBlaster, SniperRailgun };
enum class TowerChassis { Vanguard, Bastion, Catalyst };
enum class SupportModule { None, CreditRelay, StasisField, RepairDrones, CorrosionAmp };
enum class Upgrade { PiercingShots, Ricochet, Overclock, ClusterBombs, Shockwave, FireballShells,
                     ChainLightning, FreezingBlast, BurningShot, BlackHole, EmergencyRepair, Scavenger,
                     WindShear, PoisonCoil, TeleportTrap };
enum class Skull { None, Swarm, GlassCannon, Haste, Greed };
using SkullMask = std::uint8_t;
enum class EnemyType { Grunt, Runner, Tank, Shielded, Swarmling, Teleporter, Boss };
enum class Ultimate { MeteorRain, BulletStorm, AbsoluteZero, GravityShift, EnergySurge };
enum class UltimateEvolution {
    None,
    SolarAftermath, ExtinctionSpear, ShatteredSky,
    ResonantArsenal, SuppressiveGrid, ExecutionProtocol,
    BrittleSingularity, PermafrostEngine, ColdConductor,
    EventHorizon, ChronoReversal, MassDriver,
    OverdriveLink, ChainReactor, TerminalDischarge
};
enum class UltimateModule : std::uint8_t {
    MeteorQuickCharge, MeteorOverload,
    BulletSuppressor, BulletFocus,
    ZeroField, ZeroShatter,
    GravityWell, GravityReversal,
    SurgeOverdrive, SurgeDischarge
};
enum class TowerSkin { Azure, Ember, Nebula, Verdant, Gold };
enum class Arena { Moonbase, EmberCrater, NeonRuins };

struct Vec2 { float x = 0.0f, y = 0.0f; };

constexpr std::size_t SkillSlotCount = 5;

enum class SkillId : std::uint8_t {
    GravityWell, PhaseMine, VanguardDrop, ForwardBarracks, RuinHex,
    RallyBeacon, SentryFabricator, CryoField, DroneSwarm, ResonancePulse, Count
};

enum class SkillTargetMode : std::uint8_t { None, WorldPoint, Area, Enemy, Ally, Placement, Lane, Direction };

enum class SkillVisualPhase : std::uint8_t { Cast, Trigger, Hit, Expire, Spawn };

struct SkillVisualEvent {
    std::uint32_t id = 0;
    SkillId skill = SkillId::GravityWell;
    SkillVisualPhase phase = SkillVisualPhase::Cast;
    Vec2 position{};
    float radius = 0.0f;
    int remainingTicks = 0;
    std::string branchId;
};

struct SkillDefinition {
    std::string id;
    std::string display;
    std::string shortDescription;
    std::string longDescription;
    std::string iconId;
    std::string effect;
    std::string targetMode;
    int cooldownTicks = 90;
    int charges = 1;
    int durationTicks = 90;
    float range = 700.0f;
    float radius = 100.0f;
    float valueA = 1.0f;
    float valueB = 1.0f;
    std::vector<std::string> tags;
    std::vector<std::string> operations;
};

struct SkillNodeDefinition {
    std::string id;
    std::string skillId;
    std::string parentId;
    std::string branchId;
    std::string display;
    std::string description;
    std::string iconLayer;
    int tier = 1;
    int maxRank = 1;
    std::uint32_t cost = 25;
    float cooldownScale = 1.0f;
    float radiusScale = 1.0f;
    float valueScale = 1.0f;
    int chargesDelta = 0;
};

struct SkillLoadout {
    std::array<SkillId, SkillSlotCount> skills{{SkillId::GravityWell, SkillId::PhaseMine, SkillId::VanguardDrop, SkillId::RuinHex, SkillId::ForwardBarracks}};
    std::array<std::string, SkillSlotCount> nodeBuilds{};
};

struct TargetSpec {
    SkillTargetMode mode = SkillTargetMode::None;
    Vec2 world{};
    int entityId = -1;
    Vec2 direction{};
};

struct SkillCastRequest {
    std::uint32_t sequence = 0;
    std::uint32_t tick = 0;
    std::uint8_t slot = 0;
    SkillId skill = SkillId::GravityWell;
    TargetSpec target{};
};

struct SkillSnapshot {
    SkillId skill = SkillId::GravityWell;
    SkillTargetMode targetMode = SkillTargetMode::None;
    int cooldownRemaining = 0;
    int cooldownMaximum = 0;
    int charges = 0;
    int maximumCharges = 0;
    bool selected = false;
    bool validTarget = false;
    std::string iconId;
    std::string branchId;
};

struct AlliedUnit {
    int id = 0;
    Vec2 pos{};
    float hp = 0.0f;
    float maxHp = 0.0f;
    float speed = 0.0f;
    float damage = 0.0f;
    float damageScale = 1.0f;
    float speedScale = 1.0f;
    int buffTicks = 0;
    float radius = 10.0f;
    int attackCooldownTicks = 0;
    int lifetimeTicks = 0;
    SkillId ownerSkill = SkillId::VanguardDrop;
    std::string role;
    bool alive = true;
};

struct DeployableBuilding {
    int id = 0;
    Vec2 pos{};
    float hp = 0.0f;
    float maxHp = 0.0f;
    int lifetimeTicks = 0;
    int spawnCooldownTicks = 0;
    int attackCooldownTicks = 0;
    SkillId ownerSkill = SkillId::ForwardBarracks;
    std::string role;
    bool alive = true;
};

struct SkillZone {
    int id = 0;
    Vec2 center{};
    float radius = 0.0f;
    int remainingTicks = 0;
    int armTicks = 0;
    float valueA = 0.0f;
    float valueB = 0.0f;
    SkillId ownerSkill = SkillId::GravityWell;
    bool triggered = false;
    bool alive = true;
};

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
    float vulnerability = 0.0f;
    int vulnerabilityTicks = 0;
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
    int damageDealt = 0;
    int reactionTriggers = 0;
    int statusApplications = 0;
    int skillCasts = 0;
    int failedSkillCasts = 0;
    std::array<int, static_cast<std::size_t>(SkillId::Count)> skillDamage{};
    std::array<int, static_cast<std::size_t>(SkillId::Count)> skillHealing{};
    std::array<int, static_cast<std::size_t>(SkillId::Count)> skillTargets{};
    std::array<int, static_cast<std::size_t>(SkillId::Count)> skillControlTicks{};
    std::array<int, static_cast<std::size_t>(SkillId::Count)> skillSummons{};
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

struct ContentMetadata {
    std::string id;
    std::string display;
    std::string shortDescription;
    std::string longDescription;
    std::vector<std::string> strengths;
    std::vector<std::string> weaknesses;
    std::vector<std::string> synergyTags;
    std::vector<std::string> prerequisites;
    std::vector<std::string> exclusions;
    int maxStacks = 1;
    std::string iconId;
    std::string effect;
};

struct RunTypeMetadata {
    std::string id;
    std::string display;
    std::string description;
    std::string shortDescription;
    std::string longDescription;
    std::string rules;
    std::string iconId;
};

struct ContentConfig {
    std::array<ContentMetadata, 3> chassisMetadata{};
    std::array<ContentMetadata, 5> weaponMetadata{};
    std::array<ContentMetadata, 15> upgradeMetadata{};
    std::array<ContentMetadata, 5> ultimateMetadata{};
    std::array<ContentMetadata, 5> supportMetadata{};
    std::array<ContentMetadata, 4> currencyMetadata{};
    std::array<ContentMetadata, 10> workshopMetadata{};
    std::array<std::uint32_t, 10> workshopBaseCost{{80, 60, 60, 60, 60, 60, 55, 55, 55, 55}};
    std::array<std::uint32_t, 10> workshopCostStep{{45, 35, 35, 35, 35, 35, 30, 30, 30, 30}};
    std::array<std::uint32_t, 10> workshopMaxLevel{{20, 20, 20, 20, 20, 20, 20, 20, 20, 20}};
    std::array<ContentMetadata, 5> skullMetadata{};
    std::array<ContentMetadata, 3> arenaMetadata{};
    std::array<ContentMetadata, 7> enemyMetadata{};
    std::array<ContentMetadata, 15> evolutionMetadata{};
    std::array<std::uint32_t, 15> ultimateEvolutionCost{{5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5}};
    std::array<ContentMetadata, 10> ultimateModuleMetadata{};
    std::array<std::uint32_t, 10> ultimateModuleCost{{180, 180, 180, 180, 180, 180, 180, 180, 180, 180}};
    std::array<float, 10> ultimateModuleCooldownScale{{0.78f, 1.22f, 0.82f, 1.18f, 0.78f, 1.20f, 0.82f, 1.18f, 0.78f, 1.22f}};
    std::array<float, 10> ultimateModuleDamageScale{{0.86f, 1.22f, 0.86f, 1.20f, 0.82f, 1.18f, 0.84f, 1.16f, 0.82f, 1.24f}};
    std::array<ContentMetadata, 5> synergyMetadata{};
    // These packs are heap-backed so adding authored entity metadata does not
    // inflate every GameSim instance's stack footprint.
    std::vector<ContentMetadata> statusMetadata;
    std::vector<ContentMetadata> allyMetadata;
    std::vector<ContentMetadata> buildingMetadata;
    std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)> skillDefinitions{};
    std::vector<SkillNodeDefinition> skillNodes;
    std::uint32_t skillCatalogHash = 0;
    std::size_t maxAlliedUnits = 64;
    std::size_t maxBuildings = 16;
    std::size_t maxSkillZones = 24;
    std::array<RunTypeMetadata, 3> runTypeMetadata{};
    std::array<float, 5> weaponDamage{{18.0f, 88.0f, 23.0f, 12.0f, 260.0f}};
    std::array<int, 5> weaponCooldown{{6, 24, 2, 14, 42}};
    std::array<float, 5> projectileSpeed{{720.0f, 440.0f, 0.0f, 540.0f, 1200.0f}};
    std::array<int, 5> ultimateCooldownTicks{{540, 540, 540, 540, 540}};
    std::array<float, 5> ultimateDamageScale{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<float, 3> chassisWeaponDamageScale{{1.0f, 0.92f, 1.0f}};
    std::array<float, 3> chassisWeaponCooldownScale{{1.0f, 1.10f, 1.0f}};
    std::array<float, 3> chassisUltimateCooldownScale{{1.0f, 1.10f, 0.82f}};
    std::array<int, 3> chassisLivesBonus{{0, 4, 0}};
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
    std::uint32_t ultimateEvolutionCatalogHash = 0;
    std::uint32_t dailyChallengeCatalogHash = 0;
    std::uint32_t supportModuleCatalogHash = 0;
    std::uint32_t skillEntityCatalogHash = 0;
    std::array<int, 3> runExpectedMinutes{{8, 15, 8}};
    std::array<int, 3> runWaveLimit{{10, 0, 10}};
    std::array<float, 3> runRewardMultiplier{{1.0f, 0.75f, 1.0f}};
    std::array<int, 3> runWorkshopActive{{1, 1, 0}};
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
    bool activateSkill(std::size_t slot, const TargetSpec& target, std::string* error = nullptr);
    bool previewSkillTarget(std::size_t slot, const TargetSpec& target, std::string* error = nullptr) const;
    void chooseUpgrade(int choice);
    bool rerollUpgradeChoices();
    void setWeapon(Weapon weapon);
    void setChassis(TowerChassis chassis);
    void setSupport(SupportModule support);
    void setSkull(Skull skull);
    void toggleSkull(Skull skull);
    void setSkullMask(SkullMask mask);
    void setUltimate(Ultimate ultimate);
    void setUltimateEvolution(UltimateEvolution evolution);
    void setUltimateModule(UltimateModule module);
    void setAutoUltimate(bool enabled);
    void setEndless(bool enabled);
    void setSkin(TowerSkin skin);
    void setArena(Arena arena);
    void setContentConfig(const ContentConfig& content);
    void setWorkshopProgress(std::uint8_t towerCoreLevel, const std::array<std::uint8_t, 5>& moduleLevels);
    void setSupportProgress(const std::array<std::uint8_t, 5>& supportLevels);
    void setSkillLoadout(const SkillLoadout& loadout);
    void setSkillRules(const std::vector<SkillId>& required, const std::vector<SkillId>& forbidden, const std::vector<std::string>& allowedBranches = {});
    bool skillLoadoutSatisfiesRules(std::string* error = nullptr) const;

    bool isGameOver() const { return gameOver; }
    bool isVictory() const { return victory; }
    bool upgradePending() const { return upgradeChoicePending; }
    int rerollsRemaining() const { return upgradeRerolls; }
    float ultimateRatio() const;
    int waveNumber() const { return wave; }
    int currencyAmount() const { return currency; }
    int livesRemaining() const { return lives; }
    int enemiesRemaining() const;
    int enemiesSpawnedThisWave() const { return spawnedThisWave; }
    int enemiesTargetThisWave() const { return waveSpawnTarget; }
    const SimStats& stats() const { return counters; }
    Weapon weapon() const { return selectedWeapon; }
    TowerChassis chassis() const { return selectedChassis; }
    SupportModule support() const { return selectedSupport; }
    Skull skull() const { return selectedSkull; }
    SkullMask skullMask() const { return selectedSkulls; }
    bool hasSkull(Skull skull) const;
    float skullScoreMultiplier() const;
    Ultimate ultimate() const { return selectedUltimate; }
    UltimateEvolution ultimateEvolution() const { return selectedEvolution; }
    UltimateModule ultimateModule() const { return selectedUltimateModule; }
    bool autoUltimate() const { return automaticUltimate; }
    bool endless() const { return endlessMode; }
    TowerSkin skin() const { return selectedSkin; }
    Arena arena() const { return selectedArena; }
    const ContentConfig& contentConfig() const { return content; }
    std::uint32_t initialSeed() const { return seed; }
    const std::vector<Enemy>& enemies() const { return enemyList; }
    const std::vector<Projectile>& projectiles() const { return projectileList; }
    const std::vector<Upgrade>& pendingChoices() const { return choices; }
    const std::vector<Upgrade>& upgrades() const { return ownedUpgrades; }
    const SkillLoadout& skillLoadout() const { return skillLoadoutState; }
    SkillId skill(std::size_t slot) const { return skillLoadoutState.skills[std::min(slot, SkillSlotCount - 1u)]; }
    SkillSnapshot skillSnapshot(std::size_t slot) const;
    const std::vector<AlliedUnit>& alliedUnits() const { return alliedUnitsList; }
    const std::vector<DeployableBuilding>& deployableBuildings() const { return buildings; }
    const std::vector<SkillZone>& skillZones() const { return zones; }
    const std::vector<SkillVisualEvent>& skillVisualEvents() const { return skillVisualEventsList; }
    std::uint32_t lastSkillCastSequence() const { return nextSkillCastSequence == 0 ? 0 : nextSkillCastSequence - 1; }
    const std::string& lastSkillError() const { return skillError; }
    std::uint32_t stateHash() const;
    std::string statusText() const;
    std::string failureGuidance() const;
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
    void updateSkills();
    void updateAlliedUnits();
    void updateBuildings();
    void updateSkillZones();
    void updateSkillVisualEvents();
    bool validateSkillTarget(std::size_t slot, const TargetSpec& target, std::string* error) const;
    bool castSkill(const SkillCastRequest& request, std::string* error);
    bool executeAuthoredSkill(const SkillCastRequest& request, const SkillDefinition& definition, float radius, float valueA, float valueB);
    const SkillDefinition& skillDefinition(SkillId id) const;
    int skillNodeRank(std::size_t slot, const std::string& nodeId) const;
    void spawnAlliedUnit(Vec2 position, SkillId owner, const std::string& role, int lifetime, float health, float damage, float speed);
    void spawnBuilding(Vec2 position, SkillId owner, const std::string& role, int lifetime, float health);
    void applySkillDamage(Enemy& enemy, float damage, SkillId owner = SkillId::Count);
    void emitSkillVisualEvent(SkillId skill, SkillVisualPhase phase, Vec2 position, float radius, int duration, const std::string& branch = {});
    int findEnemyIndex(int id) const;
    int findAllyIndex(int id) const;
    float pathY(float x, int enemyId) const;

    std::uint32_t seed = 0;
    int tickCount = 0;
    int wave = 1;
    int lives = 20;
    int maxLives = 20;
    int currency = 0;
    int spawnedThisWave = 0;
    int waveSpawnTarget = 8;
    int spawnCooldown = 0;
    int fireCooldown = 0;
    int ultimateCooldown = 0;
    int ultimateMaxCooldown = TickRate * 18;
    int ultimateBoostTicks = 0;
    int nextEnemyId = 1;
    bool gameOver = false;
    bool victory = false;
    bool upgradeChoicePending = false;
    int upgradeRerolls = 1;
    std::uint8_t workshopTowerCoreLevel = 0;
    std::array<std::uint8_t, 5> workshopModuleLevels{{0, 0, 0, 0, 0}};
    std::array<std::uint8_t, 5> workshopSupportLevels{{0, 0, 0, 0, 0}};
    Weapon selectedWeapon = Weapon::RapidFire;
    TowerChassis selectedChassis = TowerChassis::Vanguard;
    SupportModule selectedSupport = SupportModule::None;
    Skull selectedSkull = Skull::None;
    SkullMask selectedSkulls = 0;
    Ultimate selectedUltimate = Ultimate::MeteorRain;
    UltimateEvolution selectedEvolution = UltimateEvolution::None;
    UltimateModule selectedUltimateModule = static_cast<UltimateModule>(255);
    bool automaticUltimate = false;
    bool endlessMode = false;
    TowerSkin selectedSkin = TowerSkin::Azure;
    Arena selectedArena = Arena::Moonbase;
    int bulletStormTicks = 0;
    std::vector<Enemy> enemyList;
    std::vector<Projectile> projectileList;
    std::vector<Upgrade> ownedUpgrades;
    std::vector<Upgrade> choices;
    SkillLoadout skillLoadoutState{};
    std::array<int, SkillSlotCount> skillCooldowns{{0, 0, 0, 0, 0}};
    std::array<int, SkillSlotCount> skillCharges{{1, 1, 1, 1, 1}};
    std::uint32_t nextSkillCastSequence = 1;
    std::string skillError;
    std::vector<SkillId> requiredSkills;
    std::vector<SkillId> forbiddenSkills;
    std::vector<std::string> allowedSkillBranches;
    int nextAllyId = 1;
    int nextBuildingId = 1;
    int nextZoneId = 1;
    std::vector<AlliedUnit> alliedUnitsList;
    std::vector<DeployableBuilding> buildings;
    std::vector<SkillZone> zones;
    std::uint32_t nextSkillVisualEventId = 1;
    std::vector<SkillVisualEvent> skillVisualEventsList;
    SimStats counters{};
    ContentConfig content{};
};

const char* weaponName(Weapon weapon);
const char* weaponDescription(Weapon weapon);
const char* chassisName(TowerChassis chassis);
const char* chassisDescription(TowerChassis chassis);
const char* supportModuleName(SupportModule support);
const char* supportModuleDescription(SupportModule support);
const char* skullName(Skull skull);
const char* skullDescription(Skull skull);
const char* upgradeName(Upgrade upgrade);
const char* upgradeDescription(Upgrade upgrade);
const char* enemyTypeName(EnemyType type);
const char* ultimateName(Ultimate ultimate);
const char* ultimateDescription(Ultimate ultimate);
const char* skillIdString(SkillId skill);
SkillId skillIdFromString(const std::string& id);
const char* skillName(SkillId skill);
const char* skillDescription(SkillId skill);
const char* skillTargetModeName(SkillTargetMode mode);
const char* ultimateEvolutionName(UltimateEvolution evolution);
const char* ultimateEvolutionDescription(UltimateEvolution evolution);
const char* towerSkinName(TowerSkin skin);
const char* arenaName(Arena arena);

} // namespace ta
