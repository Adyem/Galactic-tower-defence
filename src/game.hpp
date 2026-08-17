#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ta {

enum class Weapon { RapidFire, ExplosiveCannon, ArcaneBeam, FrostBlaster, SniperRailgun };
enum class TowerChassis { Vanguard, Bastion, Catalyst };
enum class SupportModule { None, CreditRelay, StasisField, RepairDrones, CorrosionAmp };
enum class Upgrade { PiercingShots, Ricochet, Overclock, ClusterBombs, Shockwave, FireballShells,
                     ChainLightning, FreezingBlast, BurningShot, BlackHole, EmergencyRepair, Scavenger,
                     WindShear, PoisonCoil, SteadyAim };
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
    RallyBeacon, SentryFabricator, CryoField, DroneSwarm, ResonancePulse,
    ArcBolt, ChainLightning, TemporalAnchor, PatientZero, ScrapCache,
    Wanted, AlphaBeast, MortarBarrage, RiftGate, GuardianWard, LoadedDice,
    BloodLance, LifeSiphon, HemorrhageField, BloodGolem, LastPulse, TreasonMark, RiotWhisper, PuppetThread, FalseOrders, SharedAgony, Thunderhead, FlashFlood, ThermalSurge, EyeOfTheStorm, BulwarkWall, TrapFoundry, Accelerate, Delay, Rewind, BorrowedTime, DeadeyeShot, Harpoon, ExploitWeakness, CollectorDrone, VectorSwarm, Mutation, RuptureHost, Quarantine, MineLayer, JuryRiggedTurret, StripForParts, ImprovisedArsenal, SpotterDrone, RailCannon, ClusterShell, WalkingBarrage, SpatialCollapse, Banish, PhaseExchange, EventHorizon, Intercept, Challenge, Sanctuary, Judgment, Misfortune, LuckyShot, StackDeck, DoubleNothing, Feed, Adaptation, PackCall, HuntCommand, Count
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

struct SkillAuthoredMetadata {
    std::vector<std::string> synergyGroups;
    std::vector<std::string> searchKeywords;
    std::string equippedPassiveId;
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
    int healthCost = 0;
    std::string resourceId;
    int resourceCost = 0;
    int resourceRefund = 0;
    std::vector<std::string> tags;
    std::vector<std::string> operations;
    std::uint8_t authoredMetadataIndex = 255u;
};

struct SkillDefinitionPack {
    std::shared_ptr<std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)>> data =
        std::make_shared<std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)>>();
    SkillDefinitionPack() = default;
    SkillDefinitionPack(const SkillDefinitionPack& other)
        : data(std::make_shared<std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)>>(*other.data)) {}
    SkillDefinitionPack& operator=(const SkillDefinitionPack& other) {
        if (this != &other) data = std::make_shared<std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)>>(*other.data);
        return *this;
    }
    SkillDefinitionPack(SkillDefinitionPack&&) noexcept = default;
    SkillDefinitionPack& operator=(SkillDefinitionPack&&) noexcept = default;
    SkillDefinition& operator[](std::size_t index) { return data->at(index); }
    const SkillDefinition& operator[](std::size_t index) const { return data->at(index); }
    constexpr std::size_t size() const { return static_cast<std::size_t>(SkillId::Count); }
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
    float durationScale = 1.0f;
    float radiusScale = 1.0f;
    float valueScale = 1.0f;
    float basicDamageScale = 1.0f;
    int chargesDelta = 0;
};

struct SkillReactionDefinition {
    std::string id;
    std::string effect = "damage";
    int reactionId = 0;
    int priority = 0;
    std::vector<std::string> requiredStates;
    std::vector<std::string> consumedStates;
    std::vector<std::string> preservedStates;
    int internalCooldownTicks = 6;
    int maxGenerationDepth = 0;
    float damageScale = 1.0f;
    float controlScale = 1.0f;
    float controlValue = 0.0f;
    float secondaryRadius = 0.0f;
    float secondaryDamageScale = 0.0f;
};

struct SkillLoadout {
    std::array<SkillId, SkillSlotCount> skills{{SkillId::GravityWell, SkillId::PhaseMine, SkillId::VanguardDrop, SkillId::RuinHex, SkillId::ForwardBarracks}};
    std::array<std::string, SkillSlotCount> nodeBuilds{};
    std::string doctrineId;
};

struct SkillLoadoutIdentity {
    std::string primaryGroup;
    std::string secondaryGroup;
    int primaryCount = 0;
    int secondaryCount = 0;
    std::vector<std::string> activeGroups;
    std::vector<std::string> equippedPassives;
    std::string doctrineId;
};

struct ClassDoctrineDefinition {
    const char* id = "";
    const char* group = "";
    const char* display = "";
    const char* description = "";
    int unlockCount = 3;
};

const std::array<ClassDoctrineDefinition, 30>& classDoctrineCatalog();
std::vector<ClassDoctrineDefinition> availableClassDoctrines(const SkillLoadoutIdentity& identity);
const ClassDoctrineDefinition* classDoctrineForId(const std::string& id);

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
    int resolvedDurationTicks = 0;
    float resolvedRadius = 0.0f;
    float resolvedRange = 0.0f;
    float resolvedValueA = 0.0f;
    float resolvedValueB = 0.0f;
    int charges = 0;
    int maximumCharges = 0;
    bool selected = false;
    bool validTarget = false;
    std::string resourceId;
    int resourceCost = 0;
    int resourceAvailable = 0;
    int healthCost = 0;
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
    float damageReduction = 0.0f;
    bool nextAttackCooldownReduced = false;
    int buffTicks = 0;
    int injuryTicks = 0;
    int accelerationTailTicks = 0;
    float radius = 10.0f;
    int attackCooldownTicks = 0;
    int lifetimeTicks = 0;
    SkillId ownerSkill = SkillId::VanguardDrop;
    std::string role;
    bool alive = true;
    int downedTicks = 0;
};

struct DeployableBuilding {
    int id = 0;
    Vec2 pos{};
    float hp = 0.0f;
    float maxHp = 0.0f;
    int lifetimeTicks = 0;
    int spawnCooldownTicks = 0;
    int attackCooldownTicks = 0;
    float footprintRadius = 45.0f;
    float effectValue = 0.0f;
    float networkRangeScale = 1.0f;
    float networkActionScale = 1.0f;
    float networkRearmScale = 1.0f;
    int charges = 0;
    SkillId ownerSkill = SkillId::ForwardBarracks;
    std::string role;
    bool alive = true;
    float actionSpeedScale = 1.0f;
    int actionSpeedTicks = 0;
    int linkedBuildingId = 0;
    int linkedPrimeTicks = 0;
    int rampTicks = 0;
    int rampTargetId = 0;
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
    bool pullsToEdge = false;
    int processedTicks = 0;
    Vec2 predictedPosition{};
    int predictedEnemyId = 0;
    Vec2 secondaryCenter{};
    bool gravityAftermathTriggered = false;
    bool gravitySingularityLocked = false;
};

struct ResourceSnapshot {
    int scrap = 0;
    int scrapOnField = 0;
    int scrapReserved = 0;
    int scrapCarryover = 0;
    int scrapInTransit = 0;
    int scrapCarryCap = 30;
    int activeDrones = 0;
    int claimedDrones = 0;
    int biomass = 0;
    int paradox = 0;
    int instability = 0;
    int resolve = 0;
    int fate = 0;
    int trophies = 0;
    int targetingData = 0;
    int bond = 0;
    int discord = 0;
    int charge = 0;
    int buildSupply = 0;
    int buildSupplyCap = 100;
};

struct BattlefieldRemain {
    int id = 0;
    Vec2 pos{};
    int value = 0;
    int biomassValue = 0;
    EnemyType source = EnemyType::Grunt;
    int createdTick = 0;
    int expiryTick = 0;
    int claimedByDrone = 0;
    bool consumed = false;
};

struct RecoveryDrone {
    int id = 0;
    Vec2 pos{};
    float speed = 120.0f;
    int carrying = 0;
    int boostTicks = 0;
    int targetRemainId = 0;
    bool active = true;
};

struct EconomyState {
    ResourceSnapshot resources{};
    std::vector<BattlefieldRemain> remains;
    std::vector<RecoveryDrone> drones;
    int nextRemainId = 1;
    int nextDroneId = 1;
    int allowanceWave = 0;
    int starterBundleWave = 0;
    int mineFoundryWave = 0;
    int trapNetworkWave = 0;
    int turretBatteryWave = 0;
    int salvageBatteryWave = 0;
    int salvageModuleReady = 0;
    int salvagerConstructionMask = 0;
    int salvagerMasterworkReady = 0;
    int arsenalAmmoTicks = 0;
    int arsenalAmmoPayouts = 0;
    int arsenalInventoryTicks = 0;
    int arsenalInventoryScrap = 0;
    int legionSummonCasts = 0;
    int legionMinorOrders = 0;
    int legionLastOrderType = 0;
    int architectNetworkMask = 0;
    int architectNetworkReady = 0;
    int arcanistCadence = 0;
    int arcanistAfterimageReady = 0;
    int arcanistArcanumReady = 0;
    int chronomancerOperationMask = 0;
    int chronomancerStableMomentReady = 0;
    int stormReactions = 0;
    int fateBoostTicks = 0;
    std::array<int, 8> fateQueue{{0, 0, 0, 0, 0, 0, 0, 0}};
    int fateQueueSize = 0;
    int fateQueueSerial = 0;
    int fateUnfavorableBank = 0;
    int fateHouseTicks = 0;
    int fateRewriteReady = 0;
    int fateDoomedOutcomeReady = 0;
    int fatePreviewEvent = -1;
    int fateCategoryMask = 0;
    int plagueDistinctInfectedCount = 0;
    int plagueFreeMutationReady = 0;
    std::array<int, 64> plagueInfectedIds{};
    int artilleristAccurateImpacts = 0;
    int artilleristFireSolutionReady = 0;
    int voidSpatialOperationMask = 0;
    int voidFixedPointReady = 0;
    int guardianWardTicks = 0;
    int nextBountyId = 1;
    int activeBountyId = 0;
    int activeBountyTargetId = 0;
    int bountyAgeTicks = 0;
    int bountyIsolationTicks = 0;
    int bountyObjectivesCompleted = 0;
    int bountyKillingMomentumReady = 0;
    int bountyMomentumObjective = -1;
    std::uint32_t bountyTagMask = 0;
    std::string bountyRetainedWeakness;
    int bountyRetainedWeaknessReady = 0;
    int bountyCollectorReady = 0;
    int timeFractureTicks = 0;
    int chronomancerDebtBurstTicks = 0;
    int beastAdaptation = 0;
    int beastAdaptationTicks = 0;
    int beastAdaptationStreak = 0;
    bool beastAdaptationPersistent = false;
    std::uint32_t beastTraitMask = 0;
    int beastSignatureTrait = 0;
    int beastSignatureWave = 0;
    int beastParticipationTicks = 0;
    int beastPounceEmpoweredTicks = 0;
    int beastCommandTargetId = 0;
    int beastCommandTicks = 0;
    int beastPackTakedownReady = 0;
    int beastHuntPinReady = 0;
    int activeVowTicks = 0;
    int vowStartingLives = 0;
    int activeVowKind = -1;
    int activeVowProgress = 0;
    int activeVowTarget = 0;
    int vowsCompleted = 0;
    int oathVowTypeMask = 0;
    int oathExemplarReady = 0;
    int oathRewardChoiceA = 0;
    int oathRewardChoiceB = 0;
    int bloodDebt = 0;
    int bloodEclipseTicks = 0;
    int bloodEclipseHealth = 0;
    int bloodHeartFragments = 0;
    int bloodReservoirReady = 0;
    int bloodHarvestShield = 0;
    int bloodGolemReserve = 0;
    int bloodPulseEmpowerTicks = 0;
    int usurperInfightingKills = 0;
    int usurperRebelEchoes = 0;
    int usurperRiotReady = 0;
    int usurperCivilWarReady = 0;
    int stormLastReaction = 0;
    int stormReactionChain = 0;
    int stormPerfectTicks = 0;
    int stormTidalMemoryReady = 0;
    int pandemicTicks = 0;
    int pandemicPrimeStrain = 0;
    int pandemicPrimeHostId = 0;
    int plagueSymbioticWave = 0;
    std::array<int, 3> stormResonanceIds{{0, 0, 0}};
    int stormResonanceCount = 0;
    std::array<int, 3> bountyObjectiveKinds{{-1, -1, -1}};
    std::array<int, 3> bountyObjectiveProgress{{0, 0, 0}};
    std::array<int, 3> bountyObjectiveTargets{{1, 1, 1}};
};

struct Enemy {
    int id = 0;
    Vec2 pos{};
    std::array<Vec2, 8> pathHistory{};
    int pathHistoryCount = 0;
    int pathHistoryHead = 0;
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
    int shockTicks = 0;
    int soakTicks = 0;
    int freezeTicks = 0;
    int cryoWhiteoutTicks = 0;
    int galeTicks = 0;
    int stormReactionCooldownTicks = 0;
    int lastStormReactionId = 0;
    int stormReactionGenerationDepth = 0;
    int stormLastSetupSkill = -1;
    int stormLastReactionSkill = -1;
    int bountyTicks = 0;
    int bountyId = 0;
    int infectionTicks = 0;
    int infectionStacks = 0;
    int infectionGeneration = 0;
    int infectionStrain = 0;
    bool pandemicSpreadUsed = false;
    int predictedTicks = 0;
    Vec2 predictedPosition{};
    int spatialCooldownTicks = 0;
    int banishedTicks = 0;
    Vec2 banishReturnPosition{};
    bool banishReturnArmed = false;
    int challengeTicks = 0;
    int temporalDelayTicks = 0;
    int temporalCancelTicks = 0;
    int temporalEchoTicks = 0;
    int temporalAnchorTicks = 0;
    Vec2 temporalAnchorPosition{};
    float temporalAnchorHealth = 0.0f;
    bool temporalAnchorValid = false;
    bool temporalAnchorProtected = false;
    int allegiance = 0;
    int allegianceTicks = 0;
    bool usurperInheritedMark = false;
    bool usurperTreasonMark = false;
    int sharedAgonyTicks = 0;
    bool ruinBrittleTriggered = false;
    int confusionTicks = 0;
    std::string weaknessTag;
    bool weaknessRewarded = false;
    float vulnerability = 0.0f;
    int vulnerabilityTicks = 0;
    int attackCooldownTicks = 0;
    int telegraphTicks = 0;
    EnemyType type = EnemyType::Grunt;
    float damageResistance = 0.0f;
    float teleportCooldown = 0.0f;
    int signalJamTicks = 0;
    std::vector<int> trapContactIds;
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

struct BountyObjectiveDefinition {
    std::string id;
    std::string display;
    std::string description;
    int kind = -1;
    int target = 1;
    int weight = 1;
    std::string event;
    bool bossAllowed = true;
    int bossSubstituteKind = -1;
};

struct PlagueMutationDefinition {
    std::string id;
    int strain = 0;
    float damageScale = 1.0f;
    float spreadRadius = 92.0f;
    float hostileDamage = 0.0f;
    int biomassValue = 1;
    std::string behavior;
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
    SkillDefinitionPack skillDefinitions{};
    std::shared_ptr<std::array<SkillAuthoredMetadata, static_cast<std::size_t>(SkillId::Count)>> skillMetadata;
    std::vector<SkillNodeDefinition> skillNodes;
    std::vector<SkillReactionDefinition> skillReactions;
    std::vector<BountyObjectiveDefinition> bountyObjectives;
    std::vector<PlagueMutationDefinition> plagueMutations;
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
    std::array<float, 15> upgradeWeight{{1.0f, 1.0f, 0.9f, 1.0f, 0.9f, 0.85f, 0.8f, 1.0f, 0.9f, 0.75f, 0.8f, 1.0f, 0.8f, 0.8f, 0.8f}};
    // Generic authored magnitudes per upgrade. Their meaning is documented in
    // upgrades.json and interpreted by the tested combat primitives.
    std::array<float, 15> upgradeValueA{{2.0f, 1.0f, 2.0f, 92.0f, 1.25f, 4.0f, 0.42f, 2.0f, 3.0f, 85.0f, 4.0f, 0.12f, 125.0f, 5.0f, 0.06f}};
    std::array<float, 15> upgradeValueB{{0.0f, 0.0f, 0.0f, 0.45f, 115.0f, 13.0f, 0.70f, 0.45f, 11.0f, 12.0f, 0.0f, 20.0f, 0.25f, 7.0f, 0.0f}};
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
    std::uint32_t bountyObjectiveCatalogHash = 0;
    std::uint32_t plagueMutationCatalogHash = 0;
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

    // Run checkpoints are simulation-only snapshots. They intentionally copy
    // all combat/economy state but do not involve the renderer or SDL.
    GameSim checkpoint() const;
    void restoreCheckpoint(const GameSim& checkpoint);

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
    void cycleMutationStrain(int delta);
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
    int maxLivesAllowed() const { return maxLives; }
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
    SkillLoadoutIdentity skillLoadoutIdentity() const;
    bool hasResonantUltimate() const;
    std::string resonantUltimateName() const;
    SkillId skill(std::size_t slot) const { return skillLoadoutState.skills[std::min(slot, SkillSlotCount - 1u)]; }
    SkillSnapshot skillSnapshot(std::size_t slot) const;
    const std::vector<AlliedUnit>& alliedUnits() const { return alliedUnitsList; }
    const std::vector<DeployableBuilding>& deployableBuildings() const { return buildings; }
    const std::vector<SkillZone>& skillZones() const { return zones; }
    ResourceSnapshot resources() const;
    int arcanistCadence() const { return economyState->arcanistCadence; }
    bool arcanistArcanumReady() const { return economyState->arcanistArcanumReady != 0; }
    bool arcanistAfterimageReady() const { return economyState->arcanistAfterimageReady != 0; }
    int activeBountyId() const { return economyState->activeBountyId; }
    int bountyAgeTicks() const { return economyState->bountyAgeTicks; }
    int bountyObjectivesCompleted() const { return economyState->bountyObjectivesCompleted; }
    bool bountyKillingMomentumReady() const { return economyState->bountyKillingMomentumReady != 0; }
    int bountyMomentumObjective() const { return economyState->bountyMomentumObjective; }
    int bountyObjectiveKind(std::size_t index) const { return index < economyState->bountyObjectiveKinds.size() ? economyState->bountyObjectiveKinds[index] : -1; }
    int bountyObjectiveProgress(std::size_t index) const { return index < economyState->bountyObjectiveProgress.size() ? economyState->bountyObjectiveProgress[index] : 0; }
    int bountyObjectiveTarget(std::size_t index) const { return index < economyState->bountyObjectiveTargets.size() ? economyState->bountyObjectiveTargets[index] : 1; }
    const BountyObjectiveDefinition* bountyObjectiveDefinition(std::size_t index) const;
    bool timeFractureActive() const { return economyState->timeFractureTicks > 0; }
    int beastAdaptation() const { return economyState->beastAdaptation; }
    int beastAdaptationTicks() const { return economyState->beastAdaptationTicks; }
    int beastAdaptationStreak() const { return economyState->beastAdaptationStreak; }
    bool beastAdaptationPersistent() const { return economyState->beastAdaptationPersistent; }
    bool beastPounceEmpowered() const { return economyState->beastPounceEmpoweredTicks > 0; }
    bool beastPackTakedownReady() const { return economyState->beastPackTakedownReady != 0; }
    bool beastHuntPinReady() const { return economyState->beastHuntPinReady != 0; }
    int mutationStrain() const { return mutationStrainSelection; }
    int plagueDistinctInfectedCount() const { return economyState->plagueDistinctInfectedCount; }
    bool plagueFreeMutationReady() const { return economyState->plagueFreeMutationReady != 0; }
    int artilleristAccurateImpacts() const { return economyState->artilleristAccurateImpacts; }
    bool artilleristFireSolutionReady() const { return economyState->artilleristFireSolutionReady != 0; }
    int voidSpatialOperationMask() const { return economyState->voidSpatialOperationMask; }
    bool voidFixedPointReady() const { return economyState->voidFixedPointReady != 0; }
    int activeVowTicks() const { return economyState->activeVowTicks; }
    int activeVowKind() const { return economyState->activeVowKind; }
    int activeVowProgress() const { return economyState->activeVowProgress; }
    int activeVowTarget() const { return economyState->activeVowTarget; }
    int vowsCompleted() const { return economyState->vowsCompleted; }
    int oathVowTypeMask() const { return economyState->oathVowTypeMask; }
    bool oathExemplarReady() const { return economyState->oathExemplarReady != 0; }
    int oathRewardChoiceA() const { return economyState->oathRewardChoiceA; }
    int oathRewardChoiceB() const { return economyState->oathRewardChoiceB; }
    bool chooseOathReward(int choice);
    int bloodDebt() const { return economyState->bloodDebt; }
    int bloodHarvestShield() const { return economyState->bloodHarvestShield; }
    int bloodGolemReserve() const { return economyState->bloodGolemReserve; }
    int salvageModuleReady() const { return economyState->salvageModuleReady; }
    int salvagerConstructionMask() const { return economyState->salvagerConstructionMask; }
    bool salvagerMasterworkReady() const { return economyState->salvagerMasterworkReady != 0; }
    int legionSummonCasts() const { return economyState->legionSummonCasts; }
    int legionMinorOrders() const { return economyState->legionMinorOrders; }
    int legionLastOrderType() const { return economyState->legionLastOrderType; }
    int architectNetworkMask() const { return economyState->architectNetworkMask; }
    bool architectNetworkReady() const { return economyState->architectNetworkReady != 0; }
    int chronomancerOperationMask() const { return economyState->chronomancerOperationMask; }
    bool chronomancerStableMomentReady() const { return economyState->chronomancerStableMomentReady != 0; }
    int buildSupply() const { return economyState->resources.buildSupply; }
    int buildSupplyCap() const { return economyState->resources.buildSupplyCap; }
    int stormPerfectTicks() const { return economyState->stormPerfectTicks; }
    int stormResonanceCount() const { return economyState->stormResonanceCount; }
    int stormResonanceId(std::size_t index) const { return index < economyState->stormResonanceIds.size() && index < static_cast<std::size_t>(economyState->stormResonanceCount) ? economyState->stormResonanceIds[index] : 0; }
    std::uint32_t stormTargetStateMask(Vec2 center, float radius) const;
    std::uint32_t stormTargetReactionMask(Vec2 center, float radius) const;
    int pandemicTicks() const { return economyState->pandemicTicks; }
    int pandemicPrimeStrain() const { return economyState->pandemicPrimeStrain; }
    int fateEventAt(std::size_t index) const { return index < static_cast<std::size_t>(economyState->fateQueueSize) ? economyState->fateQueue[index] : -1; }
    int fateQueueSize() const { return economyState->fateQueueSize; }
    int fateQueuePreviewCount() const { return hasEquippedSkillNode("fate_deck_mastery") ? 6 : (hasEquippedSkillNode("fate_deck") ? 5 : 4); }
    int fateUnfavorableBank() const { return economyState->fateUnfavorableBank; }
    int fateHouseTicks() const { return economyState->fateHouseTicks; }
    int fateBoostTicks() const { return economyState->fateBoostTicks; }
    bool fateRewriteReady() const { return economyState->fateRewriteReady != 0; }
    int fatePreviewEvent() const { return economyState->fatePreviewEvent; }
    int beastSignatureTrait() const { return economyState->beastSignatureTrait; }
    int beastParticipationTicks() const { return economyState->beastParticipationTicks; }
    int bloodEclipseHealth() const { return economyState->bloodEclipseHealth; }
    int bloodHeartFragments() const { return economyState->bloodHeartFragments; }
    bool bloodReservoirReady() const { return economyState->bloodReservoirReady != 0; }
    int usurperInfightingKills() const { return economyState->usurperInfightingKills; }
    const std::vector<BattlefieldRemain>& battlefieldRemains() const { return economyState->remains; }
    const std::vector<RecoveryDrone>& recoveryDrones() const { return economyState->drones; }
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
    void applyDamage(Enemy& enemy, float damage, bool allowSharedAgonyEcho = true);
    void chainDamage(Vec2 origin, int sourceId, float damage);
    void createUpgradeChoices();
    void applyUpgrade(Upgrade upgrade);
    void damageArea(Vec2 center, float radius, float damage, bool burn);
    void updateSkills();
    void updateAlliedUnits();
    void updateBuildings();
    void updateSkillZones();
    void initializeFateQueue();
    int drawFateEvent();
    int deterministicFateEvent(int serial) const;
    void updateEconomyEntities();
    void createBattlefieldRemain(const Enemy& enemy, int value, int biomassValue = 0);
    bool hasSkillGroup(const std::string& group) const;
    bool hasEquippedSkillNode(const std::string& nodeId) const;
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
    int mutationStrainSelection = 1;
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
    std::shared_ptr<EconomyState> economyState = std::make_shared<EconomyState>();
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
