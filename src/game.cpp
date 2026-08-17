#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

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

int* resourcePointer(ResourceSnapshot& resources, const std::string& id) {
    if (id == "scrap") return &resources.scrap;
    if (id == "biomass") return &resources.biomass;
    if (id == "paradox") return &resources.paradox;
    if (id == "instability") return &resources.instability;
    if (id == "resolve") return &resources.resolve;
    if (id == "fate") return &resources.fate;
    if (id == "trophies") return &resources.trophies;
    if (id == "targeting_data") return &resources.targetingData;
    if (id == "bond") return &resources.bond;
    return nullptr;
}

bool authoredOperationIsKnown(const std::string& operation) {
    static constexpr const char* operations[] = {
        "accelerate", "adapt_beast", "apply_freeze", "apply_gale", "apply_ignite", "apply_shock", "apply_slow", "apply_soak", "apply_weakness",
        "banish", "blood_field", "blood_golem", "blood_strike", "borrow_time", "capture_snapshot", "challenge", "cluster_shell", "collector_drone",
        "consume_shock", "create_zone", "damage_area", "damage_over_time", "deadeye_shot", "delay_event", "delayed_damage", "displace", "double_nothing",
        "event_horizon", "exploit_weakness", "eye_storm", "false_orders", "fate_boost", "feed_beast", "flash_flood", "generate_scrap", "harpoon",
        "heal_allies", "hunt_command", "improvised_arsenal", "infect", "intercept", "judgment", "jury_rigged_turret", "last_pulse", "life_siphon",
        "lucky_shot", "mark_bounty", "mine_layer", "misfortune", "mutation", "pack_call", "phase_exchange", "place_trap", "place_wall", "pounce",
        "puppet_thread", "quarantine", "rail_cannon", "resolve_reaction", "rewind_enemies", "riot_whisper", "rupture_host", "sanctuary", "shared_agony",
        "spatial_collapse", "spotter_drone", "stack_deck", "strip_for_parts", "treason_mark", "vector_swarm", "walking_barrage", "ward", "boost_deployed_collectors", "buff_allies"
    };
    return std::find(std::begin(operations), std::end(operations), operation) != std::end(operations);
}

const int* resourcePointer(const ResourceSnapshot& resources, const std::string& id) {
    return resourcePointer(const_cast<ResourceSnapshot&>(resources), id);
}

const char* bountyWeaknessForSkill(SkillId skill) {
    switch (skill) {
        case SkillId::ArcBolt: case SkillId::ChainLightning: case SkillId::MortarBarrage: case SkillId::RailCannon: case SkillId::ClusterShell: case SkillId::WalkingBarrage: case SkillId::DeadeyeShot: return "projectile";
        case SkillId::VanguardDrop: case SkillId::ForwardBarracks: case SkillId::RallyBeacon: case SkillId::DroneSwarm: case SkillId::AlphaBeast: case SkillId::Feed: return "summon";
        case SkillId::SentryFabricator: case SkillId::BulwarkWall: case SkillId::TrapFoundry: case SkillId::MineLayer: case SkillId::JuryRiggedTurret: case SkillId::ImprovisedArsenal: return "structure";
        case SkillId::CryoField: case SkillId::Thunderhead: case SkillId::FlashFlood: case SkillId::ThermalSurge: case SkillId::EyeOfTheStorm: return "elemental";
        default: return "direct";
    }
}

const BountyObjectiveDefinition* objectiveForKind(const ContentConfig& content, int kind) {
    for (const BountyObjectiveDefinition& objective : content.bountyObjectives) if (objective.kind == kind) return &objective;
    return nullptr;
}

bool objectiveUses(const ContentConfig& content, int kind, const char* event) {
    const BountyObjectiveDefinition* objective = objectiveForKind(content, kind);
    return objective != nullptr && objective->event == event;
}

void assignBountyObjectives(const ContentConfig& content, std::uint32_t seed, int targetId, bool bossTarget, EconomyState& state) {
    state.bountyObjectiveKinds = {{-1, -1, -1}};
    state.bountyObjectiveProgress = {{0, 0, 0}};
    state.bountyObjectiveTargets = {{1, 1, 1}};
    for (std::size_t slot = 0; slot < state.bountyObjectiveKinds.size(); ++slot) {
        int totalWeight = 0;
        for (const BountyObjectiveDefinition& candidate : content.bountyObjectives) {
            int resolvedKind = candidate.kind;
            if (bossTarget && !candidate.bossAllowed) resolvedKind = candidate.bossSubstituteKind;
            if (std::find(state.bountyObjectiveKinds.begin(), state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot), resolvedKind) == state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot)) totalWeight += candidate.weight;
        }
        const std::uint32_t serial = static_cast<std::uint32_t>(std::max(0, targetId)) + seed + static_cast<std::uint32_t>(slot) * 7u;
        int roll = totalWeight > 0 ? static_cast<int>(serial % static_cast<std::uint32_t>(totalWeight)) : 0;
        const BountyObjectiveDefinition* selected = nullptr;
        for (const BountyObjectiveDefinition& candidate : content.bountyObjectives) {
            const int resolvedKind = bossTarget && !candidate.bossAllowed ? candidate.bossSubstituteKind : candidate.kind;
            const bool unused = std::find(state.bountyObjectiveKinds.begin(), state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot), resolvedKind) == state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot);
            if (!unused) continue;
            roll -= candidate.weight;
            if (roll < 0) { selected = &candidate; break; }
        }
        if (selected == nullptr) {
            for (const BountyObjectiveDefinition& candidate : content.bountyObjectives) {
                const int resolvedKind = bossTarget && !candidate.bossAllowed ? candidate.bossSubstituteKind : candidate.kind;
                if (std::find(state.bountyObjectiveKinds.begin(), state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot), resolvedKind) == state.bountyObjectiveKinds.begin() + static_cast<std::ptrdiff_t>(slot)) { selected = &candidate; break; }
            }
        }
        if (selected != nullptr) {
            state.bountyObjectiveKinds[slot] = bossTarget && !selected->bossAllowed ? selected->bossSubstituteKind : selected->kind;
            const BountyObjectiveDefinition* resolved = objectiveForKind(content, state.bountyObjectiveKinds[slot]);
            state.bountyObjectiveTargets[slot] = resolved == nullptr ? selected->target : resolved->target;
        }
    }
}

bool evolutionMatchesUltimate(UltimateEvolution evolution, Ultimate ultimate) {
    if (evolution == UltimateEvolution::None) return true;
    const int evolutionIndex = static_cast<int>(evolution) - 1;
    return evolutionIndex >= 0 && evolutionIndex / 3 == static_cast<int>(ultimate);
}

bool moduleMatchesUltimate(UltimateModule module, Ultimate ultimate) {
    const unsigned int moduleIndex = static_cast<unsigned int>(module);
    return moduleIndex < 10u && moduleIndex / 2u == static_cast<unsigned int>(ultimate);
}

float ultimateModuleScale(const ContentConfig& content, UltimateModule module, Ultimate ultimate, bool cooldown) {
    if (!moduleMatchesUltimate(module, ultimate)) return 1.0f;
    const std::size_t index = static_cast<std::size_t>(module);
    return cooldown ? content.ultimateModuleCooldownScale[index] : content.ultimateModuleDamageScale[index];
}

constexpr const char* UpgradeIds[] = {
    "piercing_shots", "ricochet", "overclock", "cluster_bombs", "shockwave", "fireball_shells",
    "chain_lightning", "freezing_blast", "burning_shot", "black_hole", "emergency_repair", "scavenger",
    "wind_shear", "poison_coil", "steady_aim"
};

bool upgradeIdMatches(Upgrade upgrade, const std::string& id) {
    const std::size_t index = static_cast<std::size_t>(upgrade);
    return index < std::size(UpgradeIds) && id == UpgradeIds[index];
}

SkillTargetMode targetModeFromString(const std::string& mode) {
    if (mode == "world_point") return SkillTargetMode::WorldPoint;
    if (mode == "area") return SkillTargetMode::Area;
    if (mode == "enemy") return SkillTargetMode::Enemy;
    if (mode == "ally") return SkillTargetMode::Ally;
    if (mode == "placement") return SkillTargetMode::Placement;
    if (mode == "lane") return SkillTargetMode::Lane;
    if (mode == "direction") return SkillTargetMode::Direction;
    return SkillTargetMode::None;
}

const char* skillIdForIndex(std::size_t index) {
    static constexpr const char* ids[] = {"gravity_well", "phase_mine", "vanguard_drop", "forward_barracks", "ruin_hex", "rally_beacon", "sentry_fabricator", "cryo_field", "drone_swarm", "resonance_pulse", "arc_bolt", "chain_lightning", "temporal_anchor", "patient_zero", "scrap_cache", "wanted", "alpha_beast", "mortar_barrage", "rift_gate", "guardian_ward", "loaded_dice", "blood_lance", "life_siphon", "hemorrhage_field", "blood_golem", "last_pulse", "treason_mark", "riot_whisper", "puppet_thread", "false_orders", "shared_agony", "thunderhead", "flash_flood", "thermal_surge", "eye_of_the_storm", "bulwark_wall", "trap_foundry", "accelerate", "delay", "rewind", "borrowed_time", "deadeye_shot", "harpoon", "exploit_weakness", "collector_drone", "vector_swarm", "mutation", "rupture_host", "quarantine", "mine_layer", "jury_rigged_turret", "strip_for_parts", "improvised_arsenal", "spotter_drone", "rail_cannon", "cluster_shell", "walking_barrage", "spatial_collapse", "banish", "phase_exchange", "event_horizon", "intercept", "challenge", "sanctuary", "judgment", "misfortune", "lucky_shot", "stack_deck", "double_nothing", "feed", "adaptation", "pack_call", "hunt_command"};
    return index < std::size(ids) ? ids[index] : "unknown_skill";
}

SkillDefinition fallbackSkillDefinition(SkillId skill) {
    SkillDefinition result;
    result.id = skillIdForIndex(static_cast<std::size_t>(skill));
    result.display = skillName(skill);
    result.shortDescription = skillDescription(skill);
    result.longDescription = skillDescription(skill);
    result.iconId = result.id;
    result.targetMode = "area";
    result.effect = result.id;
    result.cooldownTicks = 180;
    result.durationTicks = 120;
    result.range = 760.0f;
    result.radius = 100.0f;
    result.valueA = 1.0f;
    result.valueB = 1.0f;
    result.tags = {"skill"};
    result.operations = {};
    return result;
}

} // namespace

GameSim::GameSim(std::uint32_t initialSeed) { reset(initialSeed); }

GameSim GameSim::checkpoint() const {
    GameSim snapshot(*this);
    snapshot.economyState = std::make_shared<EconomyState>(*economyState);
    return snapshot;
}

void GameSim::restoreCheckpoint(const GameSim& checkpointState) {
    if (this == &checkpointState) return;
    *this = checkpointState;
    economyState = std::make_shared<EconomyState>(*checkpointState.economyState);
}

void GameSim::reset(std::uint32_t initialSeed) {
    seed = initialSeed == 0 ? 1u : initialSeed;
    rngState = seed;
    tickCount = 0;
    wave = 1;
    maxLives = content.skullLives[hasSkull(Skull::GlassCannon) ? static_cast<std::size_t>(Skull::GlassCannon) : 0] + content.chassisLivesBonus[static_cast<std::size_t>(selectedChassis)] + static_cast<int>(workshopTowerCoreLevel / 5u);
    lives = maxLives;
    currency = 0;
    spawnedThisWave = 0;
    waveSpawnTarget = 8;
    spawnCooldown = 0;
    fireCooldown = 0;
    ultimateMaxCooldown = std::max(1, static_cast<int>(static_cast<float>(content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)]) * content.chassisUltimateCooldownScale[static_cast<std::size_t>(selectedChassis)] * ultimateModuleScale(content, selectedUltimateModule, selectedUltimate, true)));
    ultimateCooldown = 0;
    bulletStormTicks = 0;
    ultimateBoostTicks = 0;
    nextEnemyId = 1;
    mutationStrainSelection = 1;
    gameOver = false;
    victory = false;
    upgradeChoicePending = false;
    upgradeRerolls = 1;
    enemyList.clear();
    projectileList.clear();
    alliedUnitsList.clear();
    buildings.clear();
    zones.clear();
    if (!economyState) economyState = std::make_shared<EconomyState>();
    economyState->resources = {};
    economyState->resources.scrapCarryCap = skillLoadoutState.doctrineId == "salvager_scrapyard" ? 22 : 30;
    economyState->resources.buildSupplyCap = 100;
    economyState->resources.buildSupply = hasSkillGroup("architect") ? economyState->resources.buildSupplyCap : 0;
    economyState->remains.clear();
    economyState->drones.clear();
    economyState->nextRemainId = 1;
    economyState->nextDroneId = 1;
    economyState->allowanceWave = 0;
    economyState->starterBundleWave = 0;
    economyState->mineFoundryWave = 0;
    economyState->trapNetworkWave = 0;
    economyState->turretBatteryWave = 0;
    economyState->salvageBatteryWave = 0;
    economyState->salvageModuleReady = 0;
    economyState->salvagerConstructionMask = 0;
    economyState->salvagerMasterworkReady = 0;
    economyState->legionSummonCasts = 0;
    economyState->legionMinorOrders = 0;
    economyState->legionLastOrderType = 0;
    economyState->architectNetworkMask = 0;
    economyState->architectNetworkReady = 0;
    economyState->arcanistCadence = 0;
    economyState->arcanistAfterimageReady = 0;
    economyState->arcanistArcanumReady = 0;
    economyState->chronomancerOperationMask = 0;
    economyState->chronomancerStableMomentReady = 0;
    economyState->stormReactions = 0;
    economyState->fateBoostTicks = 0;
    economyState->fateQueue.fill(0);
    economyState->fateQueueSize = 0;
    economyState->fateQueueSerial = 0;
    economyState->fateUnfavorableBank = 0;
    economyState->fateHouseTicks = 0;
    economyState->fateRewriteReady = 0;
    economyState->fateDoomedOutcomeReady = 0;
    economyState->fatePreviewEvent = -1;
    economyState->fateCategoryMask = 0;
    economyState->plagueDistinctInfectedCount = 0;
    economyState->plagueFreeMutationReady = 0;
    economyState->plagueInfectedIds.fill(0);
    economyState->artilleristAccurateImpacts = 0;
    economyState->artilleristFireSolutionReady = 0;
    economyState->voidSpatialOperationMask = 0;
    economyState->voidFixedPointReady = 0;
    economyState->guardianWardTicks = 0;
    economyState->nextBountyId = 1;
    economyState->activeBountyId = 0;
    economyState->activeBountyTargetId = 0;
    economyState->bountyAgeTicks = 0;
    economyState->bountyIsolationTicks = 0;
    economyState->bountyObjectivesCompleted = 0;
    economyState->bountyKillingMomentumReady = 0;
    economyState->bountyMomentumObjective = -1;
    economyState->bountyTagMask = 0;
    economyState->bountyRetainedWeakness.clear();
    economyState->bountyRetainedWeaknessReady = 0;
    economyState->bountyCollectorReady = 0;
    economyState->timeFractureTicks = 0;
    economyState->chronomancerDebtBurstTicks = 0;
    economyState->beastAdaptation = 0;
    economyState->beastAdaptationTicks = 0;
    economyState->beastAdaptationStreak = 0;
    economyState->beastAdaptationPersistent = false;
    economyState->beastTraitMask = 0;
    economyState->beastSignatureTrait = 0;
    economyState->beastSignatureWave = 0;
    economyState->beastParticipationTicks = 0;
    economyState->beastPounceEmpoweredTicks = 0;
    economyState->beastCommandTargetId = 0;
    economyState->beastCommandTicks = 0;
    economyState->beastPackTakedownReady = 0;
    economyState->beastHuntPinReady = 0;
    economyState->bountyObjectiveKinds = {{-1, -1, -1}};
    economyState->bountyObjectiveProgress = {{0, 0, 0}};
    economyState->bountyObjectiveTargets = {{1, 1, 1}};
    economyState->activeVowTicks = 0;
    economyState->vowStartingLives = lives;
    economyState->activeVowKind = -1;
    economyState->activeVowProgress = 0;
    economyState->activeVowTarget = 0;
    economyState->vowsCompleted = 0;
    economyState->oathVowTypeMask = 0;
    economyState->oathExemplarReady = 0;
    economyState->oathRewardChoiceA = 0;
    economyState->oathRewardChoiceB = 0;
    economyState->bloodDebt = 0;
    economyState->bloodEclipseTicks = 0;
    economyState->bloodEclipseHealth = 0;
    economyState->bloodHeartFragments = 0;
    economyState->bloodReservoirReady = 0;
    economyState->bloodHarvestShield = 0;
    economyState->bloodGolemReserve = 0;
    economyState->bloodPulseEmpowerTicks = 0;
    economyState->arsenalAmmoTicks = 0;
    economyState->arsenalAmmoPayouts = 0;
    economyState->arsenalInventoryTicks = 0;
    economyState->arsenalInventoryScrap = 0;
    economyState->usurperInfightingKills = 0;
    economyState->usurperRebelEchoes = 0;
    economyState->usurperRiotReady = 0;
    economyState->usurperCivilWarReady = 0;
    economyState->resources.discord = 0;
    economyState->resources.charge = 0;
    economyState->stormLastReaction = 0;
    economyState->stormReactionChain = 0;
    economyState->stormPerfectTicks = 0;
    economyState->stormTidalMemoryReady = 0;
    economyState->pandemicTicks = 0;
    economyState->pandemicPrimeStrain = 0;
    economyState->pandemicPrimeHostId = 0;
    economyState->plagueSymbioticWave = 0;
    economyState->stormResonanceIds = {{0, 0, 0}};
    economyState->stormResonanceCount = 0;
    const int salvagerSkills = [&]() {
        int count = 0;
        if (content.skillMetadata) for (const SkillId skill : skillLoadoutState.skills) {
            const std::size_t index = static_cast<std::size_t>(skill);
            if (index < content.skillMetadata->size() && std::find(content.skillMetadata->at(index).synergyGroups.begin(), content.skillMetadata->at(index).synergyGroups.end(), "salvager") != content.skillMetadata->at(index).synergyGroups.end()) ++count;
        }
        return count;
    }();
    if (salvagerSkills > 0) economyState->resources.scrap = salvagerSkills >= 3 ? 20 : 8;
    int plaguewrightSkills = 0;
    if (content.skillMetadata) for (const SkillId skill : skillLoadoutState.skills) {
        const std::size_t index = static_cast<std::size_t>(skill);
        if (index < content.skillMetadata->size() && std::find(content.skillMetadata->at(index).synergyGroups.begin(), content.skillMetadata->at(index).synergyGroups.end(), "plaguewright") != content.skillMetadata->at(index).synergyGroups.end()) ++plaguewrightSkills;
    }
    if (plaguewrightSkills > 0) economyState->resources.biomass = plaguewrightSkills >= 3 ? 16 : 10;
    int fatebinderSkills = 0;
    if (content.skillMetadata) for (const SkillId skill : skillLoadoutState.skills) {
        const std::size_t index = static_cast<std::size_t>(skill);
        if (index < content.skillMetadata->size() && std::find(content.skillMetadata->at(index).synergyGroups.begin(), content.skillMetadata->at(index).synergyGroups.end(), "fatebinder") != content.skillMetadata->at(index).synergyGroups.end()) ++fatebinderSkills;
    }
    if (fatebinderSkills > 0) economyState->resources.fate = fatebinderSkills >= 3 ? 16 : 10;
    initializeFateQueue();
    if (salvagerSkills > 0) {
        const int droneCount = salvagerSkills >= 5 ? 3 : (salvagerSkills >= 3 ? 2 : 1);
        const float nodeSpeed = hasEquippedSkillNode("scrap_drone") ? (hasEquippedSkillNode("scrap_drone_mastery") ? 1.55f : 1.25f) : 1.0f;
        for (int index = 0; index < droneCount; ++index) economyState->drones.push_back({economyState->nextDroneId++, {180.0f + index * 18.0f, 360.0f}, (120.0f + index * 10.0f) * nodeSpeed, 0, 0, 0, true});
    }
    skillCooldowns.fill(0);
    nextSkillCastSequence = 1;
    nextAllyId = 1;
    nextBuildingId = 1;
    nextZoneId = 1;
    int beastmasterSkills = 0;
    if (content.skillMetadata) for (const SkillId skill : skillLoadoutState.skills) {
        const std::size_t index = static_cast<std::size_t>(skill);
        if (index < content.skillMetadata->size() && std::find(content.skillMetadata->at(index).synergyGroups.begin(), content.skillMetadata->at(index).synergyGroups.end(), "beastmaster") != content.skillMetadata->at(index).synergyGroups.end()) ++beastmasterSkills;
    }
    if (beastmasterSkills > 0) {
        float basicDamage = 14.0f + static_cast<float>(beastmasterSkills - 1) * 4.0f;
        for (const SkillNodeDefinition& node : content.skillNodes) {
            if (node.skillId != "alpha_beast" || node.basicDamageScale == 1.0f) continue;
            for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
                const int rank = skillNodeRank(slot, node.id);
                if (rank > 0) basicDamage *= std::pow(node.basicDamageScale, static_cast<float>(rank));
            }
        }
        const float health = 120.0f + static_cast<float>(beastmasterSkills - 1) * 20.0f;
        spawnAlliedUnit({260.0f, 360.0f}, SkillId::AlphaBeast, "beast", -1, health, basicDamage, 58.0f);
        economyState->resources.bond = std::min(100, 20 + beastmasterSkills * 10);
    }
    nextSkillVisualEventId = 1;
    skillVisualEventsList.clear();
    skillError.clear();
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        const SkillDefinition& definition = skillDefinition(skillLoadoutState.skills[slot]);
        skillCharges[slot] = std::max(1, definition.charges);
    }
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

int GameSim::deterministicFateEvent(int serial) const {
    // Fate previews use a private, non-consuming stream. This keeps peeking
    // and reordering independent from combat RNG while remaining replay-safe.
    std::uint32_t value = seed ^ 0x9E3779B9u;
    value ^= static_cast<std::uint32_t>(serial + 1) * 0x85EBCA6Bu;
    value ^= value >> 16u;
    value *= 0xC2B2AE35u;
    value ^= value >> 13u;
    return static_cast<int>(value % 4u);
}

void GameSim::initializeFateQueue() {
    economyState->fateQueueSize = static_cast<int>(economyState->fateQueue.size());
    economyState->fateQueueSerial = 0;
    for (int& event : economyState->fateQueue) event = deterministicFateEvent(economyState->fateQueueSerial++);
}

int GameSim::drawFateEvent() {
    if (economyState->fateQueueSize <= 0) initializeFateQueue();
    int event = economyState->fateQueue[0];
    if (economyState->fateHouseTicks > 0 && event == 0 && economyState->fateUnfavorableBank >= 3) {
        event = 3;
        economyState->fateUnfavorableBank = 0;
    }
    if (event == 0 && hasEquippedSkillNode("fate_loaded")) event = hasEquippedSkillNode("fate_loaded_mastery") ? 2 : 1;
    for (int index = 1; index < economyState->fateQueueSize; ++index) economyState->fateQueue[static_cast<std::size_t>(index - 1)] = economyState->fateQueue[static_cast<std::size_t>(index)];
    if (economyState->fateQueueSize < static_cast<int>(economyState->fateQueue.size())) ++economyState->fateQueueSize;
    economyState->fateQueue[static_cast<std::size_t>(economyState->fateQueueSize - 1)] = deterministicFateEvent(economyState->fateQueueSerial++);
    const SkillLoadoutIdentity identity = skillLoadoutIdentity();
    if (identity.primaryGroup == "fatebinder" && identity.primaryCount >= 5) {
        economyState->fateCategoryMask |= 1 << std::clamp(event, 0, 3);
        int categories = economyState->fateCategoryMask;
        int distinct = 0;
        while (categories != 0) { distinct += categories & 1; categories >>= 1; }
        if (distinct >= 3) { economyState->fateRewriteReady = 1; economyState->fateCategoryMask = 0; }
    }
    return event;
}

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

void GameSim::setChassis(TowerChassis chassis) {
    if (tickCount == 0 && enemyList.empty()) selectedChassis = chassis;
}

void GameSim::setSupport(SupportModule support) {
    if (tickCount == 0 && enemyList.empty()) selectedSupport = support;
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
        if (!evolutionMatchesUltimate(selectedEvolution, selectedUltimate)) selectedEvolution = UltimateEvolution::None;
        if (!moduleMatchesUltimate(selectedUltimateModule, selectedUltimate)) selectedUltimateModule = static_cast<UltimateModule>(255);
        ultimateMaxCooldown = std::max(1, static_cast<int>(static_cast<float>(content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)]) * content.chassisUltimateCooldownScale[static_cast<std::size_t>(selectedChassis)] * ultimateModuleScale(content, selectedUltimateModule, selectedUltimate, true)));
    }
}

void GameSim::setAutoUltimate(bool enabled) {
    (void)enabled;
    // Retained as a compatibility shim for old profiles/replays. New runs are
    // always manual-only; the legacy flag is never allowed to alter combat.
    automaticUltimate = false;
}

void GameSim::cycleMutationStrain(int delta) {
    if (delta == 0) return;
    int next = (mutationStrainSelection - 1 + delta) % 4;
    if (next < 0) next += 4;
    mutationStrainSelection = next + 1;
}

void GameSim::setEndless(bool enabled) {
    if (tickCount == 0 && enemyList.empty()) endlessMode = enabled;
}

void GameSim::setSkin(TowerSkin skin) {
    if (tickCount == 0 && enemyList.empty()) selectedSkin = skin;
}

void GameSim::setArena(Arena arena) {
    if (tickCount == 0 && enemyList.empty()) selectedArena = arena;
}

void GameSim::setUltimateEvolution(UltimateEvolution evolution) {
    if (tickCount == 0 && enemyList.empty() && evolutionMatchesUltimate(evolution, selectedUltimate)) selectedEvolution = evolution;
}

void GameSim::setUltimateModule(UltimateModule module) {
    if (tickCount == 0 && enemyList.empty() && (static_cast<unsigned int>(module) >= 10u || moduleMatchesUltimate(module, selectedUltimate))) {
        selectedUltimateModule = module;
        ultimateMaxCooldown = std::max(1, static_cast<int>(static_cast<float>(content.ultimateCooldownTicks[static_cast<std::size_t>(selectedUltimate)]) * content.chassisUltimateCooldownScale[static_cast<std::size_t>(selectedChassis)] * ultimateModuleScale(content, selectedUltimateModule, selectedUltimate, true)));
    }
}

void GameSim::setWorkshopProgress(std::uint8_t towerCoreLevel, const std::array<std::uint8_t, 5>& moduleLevels) {
    if (tickCount != 0 || !enemyList.empty()) return;
    workshopTowerCoreLevel = std::min<std::uint8_t>(towerCoreLevel, 20);
    workshopModuleLevels = moduleLevels;
    for (std::uint8_t& level : workshopModuleLevels) level = std::min<std::uint8_t>(level, 20);
}

void GameSim::setSupportProgress(const std::array<std::uint8_t, 5>& supportLevels) {
    if (tickCount != 0 || !enemyList.empty()) return;
    workshopSupportLevels = supportLevels;
    for (std::uint8_t& level : workshopSupportLevels) level = std::min<std::uint8_t>(level, 20);
}

const SkillDefinition& GameSim::skillDefinition(SkillId id) const {
    static const std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)> fallbacks = [] {
        std::array<SkillDefinition, static_cast<std::size_t>(SkillId::Count)> values{};
        for (std::size_t index = 0; index < values.size(); ++index) values[index] = fallbackSkillDefinition(static_cast<SkillId>(index));
        return values;
    }();
    const std::size_t index = std::min(static_cast<std::size_t>(id), fallbacks.size() - 1u);
    return content.skillDefinitions[index].id.empty() ? fallbacks[index] : content.skillDefinitions[index];
}

SkillLoadoutIdentity GameSim::skillLoadoutIdentity() const {
    SkillLoadoutIdentity identity;
    std::vector<std::string> groups;
    std::vector<int> counts;
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        const std::size_t skillIndex = static_cast<std::size_t>(skillLoadoutState.skills[slot]);
        const SkillAuthoredMetadata* metadata = content.skillMetadata && skillIndex < content.skillMetadata->size() ? &content.skillMetadata->at(skillIndex) : nullptr;
        if (metadata && !metadata->equippedPassiveId.empty() && std::find(identity.equippedPassives.begin(), identity.equippedPassives.end(), metadata->equippedPassiveId) == identity.equippedPassives.end()) identity.equippedPassives.push_back(metadata->equippedPassiveId);
        if (!metadata) continue;
        for (const std::string& group : metadata->synergyGroups) {
            const auto found = std::find(groups.begin(), groups.end(), group);
            if (found == groups.end()) { groups.push_back(group); counts.push_back(1); }
            else ++counts[static_cast<std::size_t>(std::distance(groups.begin(), found))];
        }
    }
    std::vector<std::size_t> order(groups.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (counts[left] != counts[right]) return counts[left] > counts[right];
        return groups[left] < groups[right];
    });
    for (const std::size_t index : order) identity.activeGroups.push_back(groups[index]);
    if (!order.empty()) {
        identity.primaryGroup = groups[order[0]];
        identity.primaryCount = counts[order[0]];
    }
    if (order.size() > 1u) {
        identity.secondaryGroup = groups[order[1]];
        identity.secondaryCount = counts[order[1]];
    }
    identity.doctrineId = skillLoadoutState.doctrineId;
    return identity;
}

bool GameSim::hasResonantUltimate() const {
    return skillLoadoutIdentity().primaryCount >= 3;
}

std::string GameSim::resonantUltimateName() const {
    if (!hasResonantUltimate()) return {};
    const std::string& group = skillLoadoutIdentity().primaryGroup;
    if (group == "arcanist") return "GRAND EQUATION";
    if (group == "legion") return "PLANETARY MUSTER";
    if (group == "bloodbinder") return "RED ECLIPSE";
    if (group == "usurper") return "COUP D'ETAT";
    if (group == "architect") return "CITADEL PROTOCOL";
    if (group == "stormcaller") return "PERFECT STORM";
    if (group == "chronomancer") return "TIME FRACTURE";
    if (group == "bounty_hunter") return "MOST WANTED";
    if (group == "plaguewright") return "PANDEMIC GENESIS";
    if (group == "salvager") return "EVERYTHING IS AMMUNITION";
    if (group == "beastmaster") return "APEX EVOLUTION";
    if (group == "artillerist") return "ORBITAL FIRE MISSION";
    if (group == "void_shepherd") return "IMPOSSIBLE GEOMETRY";
    if (group == "oathkeeper") return "UNBROKEN OATH";
    if (group == "fatebinder") return "HOUSE ALWAYS WINS";
    return "RESONANT ULTIMATE";
}

const BountyObjectiveDefinition* GameSim::bountyObjectiveDefinition(std::size_t index) const {
    if (index >= economyState->bountyObjectiveKinds.size()) return nullptr;
    const int kind = economyState->bountyObjectiveKinds[index];
    for (const BountyObjectiveDefinition& definition : content.bountyObjectives) {
        if (definition.kind == kind) return &definition;
    }
    return nullptr;
}

const std::array<ClassDoctrineDefinition, 30>& classDoctrineCatalog() {
    static const std::array<ClassDoctrineDefinition, 30> doctrines{{
        {"arcanist_focus", "arcanist", "ARCANE FOCUS", "Direct casts build 1 extra Cadence every third generator cast; finishers gain a small damage bonus.", 3},
        {"arcanist_control", "arcanist", "CONTROL MATRIX", "Area and control casts extend vulnerability windows, rewarding field setup before damage.", 3},
        {"stormcaller_resonance", "stormcaller", "PURE RESONANCE", "Every third elemental reaction repeats at reduced strength without consuming a new setup cast.", 3},
        {"stormcaller_weather", "stormcaller", "LIVING WEATHER", "Shock and Soak persist longer, but direct damage is slightly less efficient.", 3},
        {"legion_muster", "legion", "FIELD MUSTER", "Summoned squads deploy with extra health and begin with a short morale buff.", 3},
        {"legion_swarm", "legion", "MANY MEN", "Each active allied unit adds a small damage bonus to other allied units, capped by the authored unit limit.", 3},
        {"bloodbinder_sacrifice", "bloodbinder", "RED TITHE", "Blood-fueled casts cost more tower life but gain stronger damage and healing conversion.", 3},
        {"bloodbinder_symbiosis", "bloodbinder", "LIVING CIRCUIT", "Summoned allies periodically return a small amount of life while the tower remains under pressure.", 3},
        {"usurper_collapse", "usurper", "REGIME COLLAPSE", "Marked enemies lose more vulnerability resistance when surrounded by other marked enemies.", 3},
        {"usurper_puppeteer", "usurper", "PUPPET COURT", "Control effects last longer and controlled enemies deal reduced damage to allied units.", 3},
        {"architect_trapfoundry", "architect", "TRAP FOUNDRY", "Placement skills gain a second charge over time and traps arm faster.", 3},
        {"architect_bastion", "architect", "BASTION GRID", "Structures gain durability and nearby structures share a small attack-speed bonus.", 3},
        {"plaguewright_necrotic", "plaguewright", "NECROTIC BLOOM", "Infections deal stronger damage and leave higher-value remains when their host dies.", 3},
        {"plaguewright_symbiotic", "plaguewright", "SYMBIOTIC HOST", "The first infected host killed each wave briefly returns as a fragile allied carrier.", 3},
        {"salvager_logistics", "salvager", "RECOVERY NETWORK", "Collector drones move faster and field remains last longer before expiring.", 3},
        {"salvager_scrapyard", "salvager", "SCRAPYARD", "Scrap remains are worth more, but the carry cap is lower to force frequent spending.", 3},
        {"chronomancer_anchor", "chronomancer", "ANCHOR THEORY", "Temporal zones delay enemy resolution longer and reduce later Paradox decay.", 3},
        {"chronomancer_debt", "chronomancer", "BORROWED TIME", "Refreshing a skill builds Paradox debt that can be converted into a short burst of cooldown speed.", 3},
        {"bounty_hunter_deadeye", "bounty_hunter", "DEADEYE", "Bounty marks reward isolation and convert Trophy payouts into focused single-target damage.", 3},
        {"bounty_hunter_collector", "bounty_hunter", "TROPHY COLLECTOR", "Marked kills improve the next marked target and make bounty timing more important than raw speed.", 3},
        {"beastmaster_pack", "beastmaster", "PACK BOND", "A bonded companion gains damage and speed from surviving allies, preserving the small-roster identity.", 3},
        {"beastmaster_adaptation", "beastmaster", "ADAPTIVE HIDE", "Companions gain a temporary trait from the dominant enemy threat each wave.", 3},
        {"artillerist_spotter", "artillerist", "FORWARD SPOTTER", "Prediction windows are longer and accurate delayed impacts generate more Targeting Data.", 3},
        {"artillerist_walking", "artillerist", "WALKING BARRAGE", "Repeated bombardments march across the lane, trading precision for sustained area denial.", 3},
        {"void_shepherd_geometry", "void_shepherd", "IMPOSSIBLE GEOMETRY", "Spatial fields displace targets farther while generating additional Instability.", 3},
        {"void_shepherd_stability", "void_shepherd", "STABLE COORDINATES", "Spatial casts become safer and their control zones last longer with less Instability pressure.", 3},
        {"oathkeeper_sanctuary", "oathkeeper", "SANCTUARY", "Successful protection builds Resolve faster and wards restore a small amount of allied health.", 3},
        {"oathkeeper_judgment", "oathkeeper", "JUDGMENT", "Resolve can be spent on retaliatory damage after a protected target is attacked.", 3},
        {"fatebinder_loaded", "fatebinder", "STACK THE DECK", "Fate previews expose more upcoming outcomes and loaded casts last longer.", 3},
        {"fatebinder_house", "fatebinder", "HOUSE ALWAYS WINS", "Unfavorable deterministic outcomes bank toward a guaranteed favorable event.", 3}
    }};
    return doctrines;
}

std::vector<ClassDoctrineDefinition> availableClassDoctrines(const SkillLoadoutIdentity& identity) {
    std::vector<ClassDoctrineDefinition> result;
    for (const ClassDoctrineDefinition& doctrine : classDoctrineCatalog()) {
        for (std::size_t index = 0; index < identity.activeGroups.size(); ++index) {
            const bool matches = identity.activeGroups[index] == doctrine.group &&
                ((index == 0u && identity.primaryCount >= doctrine.unlockCount) || (index == 1u && identity.secondaryCount >= doctrine.unlockCount));
            if (matches) { result.push_back(doctrine); break; }
        }
    }
    return result;
}

const ClassDoctrineDefinition* classDoctrineForId(const std::string& id) {
    for (const ClassDoctrineDefinition& doctrine : classDoctrineCatalog()) if (id == doctrine.id) return &doctrine;
    return nullptr;
}

bool GameSim::hasSkillGroup(const std::string& group) const {
    if (!content.skillMetadata) return false;
    for (const SkillId skill : skillLoadoutState.skills) {
        const std::size_t index = static_cast<std::size_t>(skill);
        if (index >= content.skillMetadata->size()) continue;
        const auto& groups = content.skillMetadata->at(index).synergyGroups;
        if (std::find(groups.begin(), groups.end(), group) != groups.end()) return true;
    }
    return false;
}

bool GameSim::hasEquippedSkillNode(const std::string& nodeId) const {
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) if (skillNodeRank(slot, nodeId) > 0) return true;
    return false;
}

ResourceSnapshot GameSim::resources() const {
    ResourceSnapshot snapshot = economyState->resources;
    snapshot.scrapOnField = 0;
    snapshot.scrapReserved = 0;
    snapshot.scrapCarryover = std::min(snapshot.scrap, snapshot.scrapCarryCap);
    snapshot.scrapInTransit = 0;
    for (const BattlefieldRemain& remain : economyState->remains) {
        if (remain.consumed) continue;
        snapshot.scrapOnField += remain.value;
        if (remain.claimedByDrone != 0) snapshot.scrapReserved += remain.value;
    }
    snapshot.activeDrones = 0;
    snapshot.claimedDrones = 0;
    for (const RecoveryDrone& drone : economyState->drones) {
        if (!drone.active) continue;
        ++snapshot.activeDrones;
        if (drone.targetRemainId != 0) ++snapshot.claimedDrones;
        if (drone.carrying > 0) {
            snapshot.scrapInTransit += drone.carrying;
        } else if (drone.targetRemainId != 0) {
            const auto target = std::find_if(economyState->remains.begin(), economyState->remains.end(), [&](const BattlefieldRemain& remain) {
                return remain.id == drone.targetRemainId && !remain.consumed;
            });
            if (target != economyState->remains.end()) snapshot.scrapInTransit += std::max(0, target->value);
        }
    }
    return snapshot;
}

std::uint32_t GameSim::stormTargetStateMask(Vec2 center, float radius) const {
    const float radiusSquared = radius * radius;
    std::uint32_t mask = 0;
    for (const Enemy& enemy : enemyList) {
        if (!enemy.alive || distanceSquared(enemy.pos, center) > radiusSquared) continue;
        if (enemy.shockTicks > 0) mask |= 1u;
        if (enemy.soakTicks > 0) mask |= 2u;
        if (enemy.burn > 0.0f) mask |= 4u;
        if (enemy.freezeTicks > 0) mask |= 8u;
        if (enemy.galeTicks > 0) mask |= 16u;
    }
    return mask;
}

std::uint32_t GameSim::stormTargetReactionMask(Vec2 center, float radius) const {
    const float radiusSquared = radius * radius;
    const auto hasState = [](const Enemy& enemy, const std::string& state) {
        if (state == "shock") return enemy.shockTicks > 0;
        if (state == "soak") return enemy.soakTicks > 0;
        if (state == "freeze") return enemy.freezeTicks > 0;
        if (state == "ignite") return enemy.burn > 0.0f;
        if (state == "gale") return enemy.galeTicks > 0;
        if (state == "direct_hit") return true;
        if (state == "soak_or_freeze") return enemy.soakTicks > 0 || enemy.freezeTicks > 0;
        return false;
    };
    std::uint32_t mask = 0;
    for (const Enemy& enemy : enemyList) {
        if (!enemy.alive || distanceSquared(enemy.pos, center) > radiusSquared) continue;
        for (const SkillReactionDefinition& reaction : content.skillReactions) {
            if (reaction.reactionId <= 0 || reaction.reactionId >= 32) continue;
            if (std::all_of(reaction.requiredStates.begin(), reaction.requiredStates.end(), [&](const std::string& state) { return hasState(enemy, state); })) {
                mask |= (1u << static_cast<unsigned int>(reaction.reactionId));
            }
        }
    }
    return mask;
}

int GameSim::skillNodeRank(std::size_t slot, const std::string& nodeId) const {
    if (slot >= SkillSlotCount || nodeId.empty()) return 0;
    const std::string& build = skillLoadoutState.nodeBuilds[slot];
    const std::string needle = nodeId + ":";
    const std::size_t start = build.find(needle);
    if (start == std::string::npos) return 0;
    const std::size_t valueStart = start + needle.size();
    const std::size_t valueEnd = build.find(',', valueStart);
    try { return std::max(0, std::stoi(build.substr(valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart))); } catch (...) { return 0; }
}

void GameSim::setSkillLoadout(const SkillLoadout& loadout) {
    if (tickCount != 0 || !enemyList.empty()) return;
    std::array<bool, static_cast<std::size_t>(SkillId::Count)> seen{};
    for (SkillId id : loadout.skills) {
        const std::size_t index = static_cast<std::size_t>(id);
        if (index >= seen.size() || seen[index]) return;
        seen[index] = true;
    }
    skillLoadoutState = loadout;
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        skillCooldowns[slot] = 0;
        skillCharges[slot] = std::max(1, skillDefinition(skillLoadoutState.skills[slot]).charges);
    }
}

void GameSim::setSkillRules(const std::vector<SkillId>& required, const std::vector<SkillId>& forbidden, const std::vector<std::string>& allowedBranches) {
    if (tickCount != 0 || !enemyList.empty()) return;
    requiredSkills = required;
    forbiddenSkills = forbidden;
    allowedSkillBranches = allowedBranches;
}

bool GameSim::skillLoadoutSatisfiesRules(std::string* error) const {
    // Talent builds are part of the authored run identity.  Reject stale or
    // cross-skill node ids here instead of silently treating them as base
    // skills (which would make a replay appear valid while simulating a
    // different loadout).  Profile fixtures may intentionally provide a
    // partial branch path, so parent-chain completeness remains the Workshop
    // editor's responsibility; the runtime still validates node ownership,
    // rank bounds, and duplicate ids.  Branch choice remains a Workshop
    // concern because deterministic test fixtures and migration shims may
    // carry authored multi-branch snapshots.
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
        const std::string& build = skillLoadoutState.nodeBuilds[slot];
        if (build.empty()) continue;
        std::vector<std::string> selectedNodes;
        std::size_t cursor = 0;
        while (cursor < build.size()) {
            const std::size_t comma = build.find(',', cursor);
            const std::string entry = build.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
            const std::size_t colon = entry.find(':');
            int rank = 0;
            if (colon == std::string::npos || entry.substr(0, colon).empty()) {
                if (error) *error = "INVALID SKILL NODE BUILD";
                return false;
            }
            try { rank = std::stoi(entry.substr(colon + 1)); } catch (...) { rank = 0; }
            const std::string nodeId = entry.substr(0, colon);
            const auto node = std::find_if(content.skillNodes.begin(), content.skillNodes.end(), [&](const SkillNodeDefinition& candidate) { return candidate.id == nodeId; });
            if (node == content.skillNodes.end() || node->skillId != skillIdString(skillLoadoutState.skills[slot]) || rank <= 0 || rank > node->maxRank || std::find(selectedNodes.begin(), selectedNodes.end(), nodeId) != selectedNodes.end()) {
                if (error) *error = "INVALID SKILL NODE BUILD";
                return false;
            }
            selectedNodes.push_back(nodeId);
            if (comma == std::string::npos) break;
            cursor = comma + 1;
        }
    }
    for (const SkillId required : requiredSkills) {
        bool present = false;
        for (const SkillId equipped : skillLoadoutState.skills) if (equipped == required) { present = true; break; }
        if (!present) { if (error) *error = "REQUIRED SKILL NOT EQUIPPED"; return false; }
    }
    for (const SkillId forbidden : forbiddenSkills) {
        for (const SkillId equipped : skillLoadoutState.skills) if (equipped == forbidden) { if (error) *error = "FORBIDDEN SKILL EQUIPPED"; return false; }
    }
    if (!allowedSkillBranches.empty()) {
        for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
            for (const SkillNodeDefinition& node : content.skillNodes) {
                if (node.skillId != skillIdString(skillLoadoutState.skills[slot]) || node.tier < 2 || skillNodeRank(slot, node.id) <= 0) continue;
                const std::string selectedBranch = std::string(skillIdString(skillLoadoutState.skills[slot])) + ":" + node.branchId;
                if (std::find(allowedSkillBranches.begin(), allowedSkillBranches.end(), selectedBranch) == allowedSkillBranches.end()) {
                    if (error) *error = "SKILL BRANCH NOT ALLOWED TODAY";
                    return false;
                }
            }
        }
    }
    return true;
}

int GameSim::findEnemyIndex(int id) const {
    for (std::size_t index = 0; index < enemyList.size(); ++index) if (enemyList[index].alive && enemyList[index].id == id) return static_cast<int>(index);
    return -1;
}

int GameSim::findAllyIndex(int id) const {
    for (std::size_t index = 0; index < alliedUnitsList.size(); ++index) if (alliedUnitsList[index].alive && alliedUnitsList[index].id == id) return static_cast<int>(index);
    return -1;
}

bool GameSim::validateSkillTarget(std::size_t slot, const TargetSpec& target, std::string* error) const {
    if (slot >= SkillSlotCount) { if (error) *error = "INVALID SKILL SLOT"; return false; }
    const SkillDefinition& definition = skillDefinition(skillLoadoutState.skills[slot]);
    const SkillTargetMode expected = targetModeFromString(definition.targetMode);
    if (target.mode != expected) { if (error) *error = "WRONG TARGET MODE"; return false; }
    if (skillCooldowns[slot] > 0) { if (error) *error = "SKILL COOLING DOWN"; return false; }
    if (skillCharges[slot] <= 0) { if (error) *error = "NO CHARGES"; return false; }
    if (expected == SkillTargetMode::None) return true;
    if (expected == SkillTargetMode::Enemy && findEnemyIndex(target.entityId) < 0) { if (error) *error = "NO VALID ENEMY"; return false; }
    if (expected == SkillTargetMode::Ally && findAllyIndex(target.entityId) < 0) { if (error) *error = "NO VALID ALLY"; return false; }
    if (expected == SkillTargetMode::Direction && target.direction.x * target.direction.x + target.direction.y * target.direction.y < 0.000001f) { if (error) *error = "DIRECTION REQUIRED"; return false; }
    if (expected == SkillTargetMode::WorldPoint || expected == SkillTargetMode::Area || expected == SkillTargetMode::Placement || expected == SkillTargetMode::Lane || expected == SkillTargetMode::Direction) {
        if (target.world.x < 92.0f || target.world.x > static_cast<float>(Width - 40) || target.world.y < 24.0f || target.world.y > static_cast<float>(Height - 24)) { if (error) *error = "TARGET OUT OF BOUNDS"; return false; }
        const float dx = target.world.x - TowerX;
        const float dy = target.world.y - TowerY;
        if (dx * dx + dy * dy > definition.range * definition.range) { if (error) *error = "TARGET OUT OF RANGE"; return false; }
    }
    if (expected == SkillTargetMode::Placement) {
        for (const DeployableBuilding& building : buildings) if (building.alive && distanceSquared(building.pos, target.world) < (definition.radius + 45.0f) * (definition.radius + 45.0f)) { if (error) *error = "PLACEMENT BLOCKED"; return false; }
    }
    return true;
}

bool GameSim::previewSkillTarget(std::size_t slot, const TargetSpec& target, std::string* error) const {
    return validateSkillTarget(slot, target, error);
}

void GameSim::spawnAlliedUnit(Vec2 position, SkillId owner, const std::string& role, int lifetime, float health, float damage, float speed) {
    if (alliedUnitsList.size() >= content.maxAlliedUnits) {
        const auto expired = std::find_if(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& unit) { return unit.lifetimeTicks <= 0 || !unit.alive; });
        if (expired != alliedUnitsList.end()) alliedUnitsList.erase(expired);
        if (alliedUnitsList.size() >= content.maxAlliedUnits) return;
    }
    AlliedUnit unit;
    unit.id = nextAllyId++;
    unit.pos = position;
    const bool legionUnit = skillLoadoutState.doctrineId == "legion_muster" &&
        (role == "soldier" || role == "striker" || role == "bulwark" || role == "drone" || role == "disruptor" || role == "hunter");
    unit.hp = health * (legionUnit ? 1.10f : 1.0f);
    unit.maxHp = unit.hp;
    unit.speed = speed;
    unit.damage = damage * (legionUnit ? 1.05f : 1.0f);
    if (legionUnit) {
        unit.damageScale = 1.08f;
        unit.speedScale = 1.08f;
        unit.buffTicks = TickRate * 2;
    }
    unit.lifetimeTicks = lifetime;
    unit.ownerSkill = owner;
    unit.role = role;
    alliedUnitsList.push_back(unit);
    if (owner != SkillId::Count) ++counters.skillSummons[static_cast<std::size_t>(owner)];
}

void GameSim::spawnBuilding(Vec2 position, SkillId owner, const std::string& role, int lifetime, float health) {
    if (buildings.size() >= content.maxBuildings) {
        const auto expired = std::find_if(buildings.begin(), buildings.end(), [](const DeployableBuilding& building) { return building.lifetimeTicks <= 0 || !building.alive; });
        if (expired != buildings.end()) buildings.erase(expired);
        if (buildings.size() >= content.maxBuildings) return;
    }
    const bool bastionStructure = skillLoadoutState.doctrineId == "architect_bastion" && role != "trap";
    DeployableBuilding building;
    building.id = nextBuildingId++;
    building.pos = position;
    building.hp = health * (bastionStructure ? 1.12f : 1.0f);
    building.maxHp = building.hp;
    building.lifetimeTicks = lifetime;
    building.ownerSkill = owner;
    building.role = role;
    building.spawnCooldownTicks = 30;
    if (skillLoadoutState.doctrineId == "architect_trapfoundry" && role == "trap") building.networkRearmScale = 1.25f;
    buildings.push_back(building);
    if (skillLoadoutState.doctrineId == "architect_bastion") {
        for (DeployableBuilding& nearby : buildings) if (nearby.alive && nearby.id != building.id && distanceSquared(nearby.pos, building.pos) <= 180.0f * 180.0f) {
            if (nearby.role == "trap") nearby.networkRearmScale = std::max(nearby.networkRearmScale, 1.12f);
            else if (nearby.role != "wall") nearby.networkActionScale = std::max(nearby.networkActionScale, 1.10f);
        }
    }
    if (hasSkillGroup("architect")) {
        int structureBit = 0;
        if (role == "wall") structureBit = 1;
        else if (role == "trap") structureBit = 2;
        else if (role == "sentry" || role == "gatling" || role == "mortar" || role == "swarm") structureBit = 4;
        else if (role == "barracks" || role == "armory") structureBit = 8;
        economyState->architectNetworkMask |= structureBit;
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        if (identity.primaryGroup == "architect" && identity.primaryCount >= 5 && economyState->architectNetworkMask == 15) {
            if (economyState->architectNetworkReady == 0) {
                economyState->architectNetworkReady = 1;
                for (DeployableBuilding& networkBuilding : buildings) {
                    if (networkBuilding.role == "wall") {
                        networkBuilding.maxHp *= 1.10f;
                        networkBuilding.hp = std::min(networkBuilding.maxHp, networkBuilding.hp + networkBuilding.maxHp * 0.10f);
                    } else if (networkBuilding.role == "trap") {
                        networkBuilding.networkRearmScale = 1.20f;
                    } else {
                        networkBuilding.networkRangeScale = 1.10f;
                        networkBuilding.networkActionScale = 1.10f;
                    }
                }
                emitSkillVisualEvent(owner, SkillVisualPhase::Hit, position, building.footprintRadius, TickRate * 2, "defense_network");
            }
        }
        if (economyState->architectNetworkReady != 0) {
            DeployableBuilding& networkBuilding = buildings.back();
            if (networkBuilding.role == "trap") networkBuilding.networkRearmScale = 1.20f;
            else if (networkBuilding.role != "wall") {
                networkBuilding.networkRangeScale = 1.10f;
                networkBuilding.networkActionScale = 1.10f;
            }
        }
    }
    if (owner != SkillId::Count) ++counters.skillSummons[static_cast<std::size_t>(owner)];
}

bool GameSim::executeAuthoredSkill(const SkillCastRequest& request, const SkillDefinition& definition, float radius, float valueA, float valueB) {
    // Validate the complete operation list before mutating any state. Content
    // validation catches authored mistakes on disk; this second guard keeps
    // injected/test ContentConfig data from executing only a partial cast.
    for (const std::string& operation : definition.operations) if (!authoredOperationIsKnown(operation)) return false;
    bool executed = false;
    int reactionEvents = 0;
    const bool architectLoadout = hasSkillGroup("architect");
    const auto canSpendBuildSupply = [&](int cost) { return !architectLoadout || economyState->resources.buildSupply >= cost; };
    const auto spendBuildSupply = [&](int cost) { if (architectLoadout) economyState->resources.buildSupply = std::max(0, economyState->resources.buildSupply - cost); };
    const auto recordPlagueHost = [&](const Enemy& enemy) {
        if (!hasSkillGroup("plaguewright") || enemy.boss || enemy.id <= 0) return;
        const auto begin = economyState->plagueInfectedIds.begin();
        const auto end = begin + std::min<std::size_t>(static_cast<std::size_t>(economyState->plagueDistinctInfectedCount), economyState->plagueInfectedIds.size());
        if (std::find(begin, end, enemy.id) != end) return;
        if (economyState->plagueDistinctInfectedCount < static_cast<int>(economyState->plagueInfectedIds.size())) {
            economyState->plagueInfectedIds[static_cast<std::size_t>(economyState->plagueDistinctInfectedCount++)] = enemy.id;
            const SkillLoadoutIdentity identity = skillLoadoutIdentity();
            if (identity.primaryGroup == "plaguewright" && identity.primaryCount >= 5 && economyState->plagueDistinctInfectedCount >= 5) economyState->plagueFreeMutationReady = 1;
        }
    };
    const auto startOathVow = [&](int kind, int duration) {
        if (!hasSkillGroup("oathkeeper") || economyState->activeVowTicks > 0) return;
        economyState->activeVowTicks = std::max(1, duration);
        economyState->vowStartingLives = lives;
        economyState->activeVowKind = std::clamp(kind, 0, 2);
        economyState->activeVowProgress = 0;
        economyState->activeVowTarget = std::max(1, duration);
        if (economyState->oathExemplarReady != 0) {
            economyState->oathExemplarReady = 0;
            economyState->oathRewardChoiceA = 1;
            economyState->oathRewardChoiceB = 2;
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate * 2, "exemplar_offer");
        }
    };
    const auto addVoidInstability = [&](int amount) {
        if (amount <= 0) return;
        if (skillLoadoutState.doctrineId == "void_shepherd_geometry") amount = static_cast<int>(std::ceil(static_cast<float>(amount) * 1.25f));
        else if (skillLoadoutState.doctrineId == "void_shepherd_stability") amount = std::max(1, static_cast<int>(std::floor(static_cast<float>(amount) * 0.60f)));
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        if (identity.primaryGroup == "void_shepherd" && identity.primaryCount >= 5 && economyState->voidFixedPointReady != 0) {
            economyState->voidFixedPointReady = 0;
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, "fixed_point");
            return;
        }
        economyState->resources.instability = std::min(100, economyState->resources.instability + amount);
    };
    const auto recordSalvagerConstruction = [&](int category) {
        if (!hasSkillGroup("salvager")) return;
        economyState->salvagerConstructionMask |= category;
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        if (identity.primaryGroup == "salvager" && identity.primaryCount >= 5 && economyState->salvagerConstructionMask == 7) economyState->salvagerMasterworkReady = 1;
    };
    const auto reactionDefinition = [&](int reactionId) -> const SkillReactionDefinition* {
        const auto found = std::find_if(content.skillReactions.begin(), content.skillReactions.end(), [&](const SkillReactionDefinition& reaction) { return reaction.reactionId == reactionId; });
        return found == content.skillReactions.end() ? nullptr : &*found;
    };
    const auto hasStormState = [&](const Enemy& enemy, const std::string& state) {
        if (state == "shock") return enemy.shockTicks > 0;
        if (state == "soak") return enemy.soakTicks > 0;
        if (state == "freeze") return enemy.freezeTicks > 0;
        if (state == "ignite") return enemy.burn > 0.0f;
        if (state == "gale") return enemy.galeTicks > 0;
        if (state == "direct_hit") return true;
        if (state == "soak_or_freeze") return enemy.soakTicks > 0 || enemy.freezeTicks > 0;
        if (state == "storm_zone") {
            return std::any_of(zones.begin(), zones.end(), [&](const SkillZone& zone) {
                return zone.alive && (zone.ownerSkill == SkillId::Thunderhead || zone.ownerSkill == SkillId::EyeOfTheStorm) && distanceSquared(zone.center, enemy.pos) <= zone.radius * zone.radius;
            });
        }
        return false;
    };
    const auto clearStormState = [&](Enemy& enemy, const std::string& state) {
        if (state == "shock") enemy.shockTicks = 0;
        else if (state == "soak") enemy.soakTicks = 0;
        else if (state == "freeze") enemy.freezeTicks = 0;
        else if (state == "ignite") { enemy.burn = 0.0f; enemy.burnDps = 0.0f; enemy.burnTicks = 0; }
        else if (state == "gale") enemy.galeTicks = 0;
        else if (state == "soak_or_freeze") {
            if (enemy.soakTicks > 0) enemy.soakTicks = 0;
            else enemy.freezeTicks = 0;
        }
    };
    const auto reactionRequirementsMet = [&](const Enemy& enemy, int reactionId) {
        const SkillReactionDefinition* reaction = reactionDefinition(reactionId);
        return reaction != nullptr && std::all_of(reaction->requiredStates.begin(), reaction->requiredStates.end(), [&](const std::string& state) { return hasStormState(enemy, state); });
    };
    const auto consumeReactionStates = [&](Enemy& enemy, int reactionId) {
        const SkillReactionDefinition* reaction = reactionDefinition(reactionId);
        if (reaction == nullptr || economyState->stormPerfectTicks > 0) return;
        for (const std::string& state : reaction->consumedStates) {
            if (std::find(reaction->preservedStates.begin(), reaction->preservedStates.end(), state) == reaction->preservedStates.end()) clearStormState(enemy, state);
        }
    };
    const auto resolveStormReaction = [&](Enemy& enemy, int reactionId, float damage, int generationDepth = 0) {
        if (!enemy.alive || reactionEvents >= 24) return false;
        if (enemy.stormReactionCooldownTicks > 0 && enemy.lastStormReactionId == reactionId) return false;
        const SkillReactionDefinition* reaction = reactionDefinition(reactionId);
        if (reaction == nullptr) return false;
        if (generationDepth > reaction->maxGenerationDepth) return false;
        const bool distinct = economyState->stormLastReaction != reactionId;
        ++reactionEvents;
        enemy.lastStormReactionId = reactionId;
        enemy.stormReactionGenerationDepth = generationDepth;
        enemy.stormLastReactionSkill = static_cast<int>(request.skill);
        enemy.stormReactionCooldownTicks = std::max(enemy.stormReactionCooldownTicks, reaction->internalCooldownTicks);
        economyState->stormLastReaction = reactionId;
        economyState->stormReactionChain = distinct ? economyState->stormReactionChain + 1 : economyState->stormReactionChain;
        const bool perfectStorm = economyState->stormPerfectTicks > 0;
        const bool chainBurst = request.skill == SkillId::ChainLightning && hasEquippedSkillNode("chain_burst");
        const float chainReactionScale = chainBurst ? (1.18f * (hasEquippedSkillNode("chain_burst_mastery") ? 1.12f : 1.0f)) : 1.0f;
        const float chainReachScale = request.skill == SkillId::ChainLightning && hasEquippedSkillNode("chain_bounce")
            ? (1.22f * (hasEquippedSkillNode("chain_bounce_mastery") ? 1.12f : 1.0f))
            : 1.0f;
        const float steamControlScale = request.skill == SkillId::ThermalSurge && reactionId == 6 && hasEquippedSkillNode("thermal_surge_inversion_mastery") ? 1.20f : 1.0f;
        const auto activeCloud = std::find_if(zones.begin(), zones.end(), [&](const SkillZone& zone) {
            return zone.alive && zone.ownerSkill == SkillId::Thunderhead && distanceSquared(zone.center, enemy.pos) <= zone.radius * zone.radius;
        });
        const bool cloudPulseAvailable = perfectStorm && distinct && activeCloud != zones.end();
        const int chargeGain = cloudPulseAvailable ? 0 : (perfectStorm ? 1 : (distinct ? 2 : 1));
        economyState->resources.charge = std::min(6, economyState->resources.charge + chargeGain);
        const int before = counters.damageDealt;
        applyDamage(enemy, damage * reaction->damageScale * chainReactionScale);
        counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
        ++counters.reactionTriggers;
        ++economyState->stormReactions;
        if (reaction->effect == "freeze") {
            enemy.freezeTicks = std::max(enemy.freezeTicks, static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * reaction->controlScale * steamControlScale)));
            enemy.slow = std::max(enemy.slow, reaction->controlValue * steamControlScale);
        } else if (reaction->effect == "shatter") {
            const Vec2 fragmentOrigin = enemy.pos;
            for (Enemy& fragment : enemyList) if (fragment.alive && fragment.id != enemy.id && distanceSquared(fragment.pos, fragmentOrigin) <= (reaction->secondaryRadius * chainReachScale) * (reaction->secondaryRadius * chainReachScale)) {
                applyDamage(fragment, damage * reaction->secondaryDamageScale * chainReactionScale);
                fragment.slow = std::max(fragment.slow, reaction->controlValue);
                if (hasEquippedSkillNode("cryo_shatter_capstone")) {
                    fragment.freezeTicks = std::max(fragment.freezeTicks, TickRate / 2);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, fragment.pos, fragment.radius + 8.0f, TickRate / 2, "glacial_fracture");
                }
            }
            if (hasEquippedSkillNode("cryo_shatter_capstone")) emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, fragmentOrigin, reaction->secondaryRadius * chainReachScale, TickRate / 2, "glacial_fracture");
        } else if (reaction->effect == "stun") {
            enemy.stun = std::max(enemy.stun, reaction->controlValue * steamControlScale);
        } else if (reaction->effect == "slow") {
            enemy.slow = std::max(enemy.slow, reaction->controlValue * steamControlScale);
        } else if (reaction->effect == "spread_burn") {
            enemy.slow = std::max(enemy.slow, reaction->controlValue * steamControlScale);
            for (Enemy& spread : enemyList) if (spread.alive && spread.id != enemy.id && distanceSquared(spread.pos, enemy.pos) <= (reaction->secondaryRadius * chainReachScale) * (reaction->secondaryRadius * chainReachScale)) {
                spread.burn = std::max(spread.burn, 0.45f);
                spread.burnDps = std::max(spread.burnDps, damage * reaction->secondaryDamageScale * chainReactionScale);
                spread.burnTicks = std::max(spread.burnTicks, TickRate / 2);
                spread.stormLastSetupSkill = static_cast<int>(request.skill);
            }
        }
        if (cloudPulseAvailable && reactionEvents < 24) {
            ++reactionEvents;
            applySkillDamage(enemy, damage * reaction->damageScale * chainReactionScale * 0.35f, SkillId::Thunderhead);
            ++counters.reactionTriggers;
            emitSkillVisualEvent(SkillId::Thunderhead, SkillVisualPhase::Hit, enemy.pos, 22.0f, activeCloud->remainingTicks, "perfect_pulse");
        }
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        const bool resonanceReady = identity.primaryGroup == "stormcaller" && (identity.primaryCount >= 5 || hasEquippedSkillNode("chain_capstone"));
        if (resonanceReady && std::find(economyState->stormResonanceIds.begin(), economyState->stormResonanceIds.end(), reactionId) == economyState->stormResonanceIds.end()) {
            if (economyState->stormResonanceCount < static_cast<int>(economyState->stormResonanceIds.size())) economyState->stormResonanceIds[static_cast<std::size_t>(economyState->stormResonanceCount++)] = reactionId;
            if (economyState->stormResonanceCount == static_cast<int>(economyState->stormResonanceIds.size())) {
                if (reactionEvents < 24) {
                    ++reactionEvents;
                    const int beforeEcho = counters.damageDealt;
                    applyDamage(enemy, damage * reaction->damageScale * chainReactionScale * 0.40f);
                    counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - beforeEcho);
                    ++counters.reactionTriggers;
                    if (reaction->effect == "stun") enemy.stun = std::max(enemy.stun, reaction->controlValue * 0.5f);
                    else if (reaction->effect == "slow" || reaction->effect == "spread_burn") enemy.slow = std::max(enemy.slow, reaction->controlValue * 0.5f);
                }
                economyState->stormResonanceIds = {{0, 0, 0}};
                economyState->stormResonanceCount = 0;
            }
        }
        return true;
    };
    for (const std::string& operation : definition.operations) {
        if (operation == "create_zone") {
            if (zones.size() >= content.maxSkillZones) continue;
            zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
            executed = true;
        } else if (operation == "capture_snapshot") {
            const int anchorDuration = skillLoadoutState.doctrineId == "chronomancer_anchor" ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.25f)) : definition.durationTicks;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (enemy.temporalAnchorValid && enemy.temporalAnchorProtected) continue;
                enemy.temporalAnchorPosition = enemy.pos;
                if (hasEquippedSkillNode("anchor_rewind") && enemy.pathHistoryCount > 1) {
                    const int historySize = static_cast<int>(enemy.pathHistory.size());
                    const int historySteps = hasEquippedSkillNode("anchor_rewind_mastery") ? 2 : 1;
                    const int historyIndex = (enemy.pathHistoryHead - std::min(historySteps, enemy.pathHistoryCount - 1) + historySize) % historySize;
                    enemy.temporalAnchorPosition = enemy.pathHistory[static_cast<std::size_t>(historyIndex)];
                }
                enemy.temporalAnchorHealth = enemy.hp;
                enemy.temporalAnchorTicks = anchorDuration;
                enemy.temporalAnchorValid = true;
                if (economyState->chronomancerStableMomentReady != 0) {
                    enemy.temporalAnchorProtected = true;
                    economyState->chronomancerStableMomentReady = 0;
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "damage_area") {
            const int before = counters.damageDealt;
            damageArea(request.target.world, radius, valueA, false);
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
            // CONTROL doctrine turns Arcanist area casts into setup tools as
            // well as damage: only vulnerabilities that already exist are
            // extended, so the doctrine cannot manufacture an infinite
            // debuff loop from an otherwise empty cast.
            if (skillLoadoutState.doctrineId == "arcanist_control") {
                for (Enemy& enemy : enemyList) if (enemy.alive && enemy.vulnerabilityTicks > 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                    enemy.vulnerabilityTicks = std::min(TickRate * 8, enemy.vulnerabilityTicks + TickRate / 2);
                }
            }
            const SkillLoadoutIdentity identity = skillLoadoutIdentity();
            if (request.skill != SkillId::ChainLightning && economyState->arcanistAfterimageReady != 0 && identity.primaryGroup == "arcanist" && identity.primaryCount >= 5) {
                const int beforeEcho = counters.damageDealt;
                damageArea(request.target.world, radius, valueA * 0.30f, false);
                counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - beforeEcho);
                economyState->arcanistAfterimageReady = 0;
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius * 0.72f, TickRate, "afterimage");
            }
            executed = true;
        } else if (operation == "apply_weakness") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.vulnerability = std::max(enemy.vulnerability, valueA);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "apply_shock") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (enemy.soakTicks > 0) {
                    if (resolveStormReaction(enemy, 1, valueA)) consumeReactionStates(enemy, 1);
                }
                enemy.shockTicks = std::max(enemy.shockTicks, definition.durationTicks + (skillLoadoutState.doctrineId == "stormcaller_weather" ? TickRate * 2 : 0));
                enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "apply_soak") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (enemy.shockTicks > 0) {
                    if (resolveStormReaction(enemy, 1, valueA)) consumeReactionStates(enemy, 1);
                }
                enemy.soakTicks = std::max(enemy.soakTicks, definition.durationTicks + (skillLoadoutState.doctrineId == "stormcaller_weather" ? TickRate * 2 : 0));
                enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "apply_freeze") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (enemy.soakTicks > 0) {
                    if (resolveStormReaction(enemy, 2, valueA)) consumeReactionStates(enemy, 2);
                }
                enemy.freezeTicks = std::max(enemy.freezeTicks, definition.durationTicks);
                enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                enemy.stun = std::max(enemy.stun, std::min(1.5f, static_cast<float>(definition.durationTicks) / TickRate));
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                counters.skillControlTicks[static_cast<std::size_t>(request.skill)] += definition.durationTicks;
            }
            executed = true;
        } else if (operation == "apply_ignite") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (enemy.shockTicks > 0) {
                    if (resolveStormReaction(enemy, 5, valueA)) consumeReactionStates(enemy, 5);
                } else if (enemy.soakTicks > 0 || enemy.freezeTicks > 0) {
                    if (resolveStormReaction(enemy, 6, valueA)) consumeReactionStates(enemy, 6);
                }
                const bool frozenElite = enemy.boss && enemy.freezeTicks > 0 && hasEquippedSkillNode("thermal_surge_capstone");
                enemy.burn = std::max(enemy.burn, static_cast<float>(definition.durationTicks) / TickRate);
                enemy.burnDps = std::max(enemy.burnDps, valueA);
                enemy.burnTicks = std::max(enemy.burnTicks, definition.durationTicks);
                if (frozenElite && zones.size() < content.maxSkillZones) {
                    zones.push_back({nextZoneId++, enemy.pos, radius * 0.70f, std::max(1, definition.durationTicks / 2), definition.durationTicks / 4, valueA * 0.55f, valueB, request.skill, false, true});
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, enemy.pos, radius * 0.70f, definition.durationTicks / 2, "thermal_ring");
                }
                enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                if (hasEquippedSkillNode("thermal_surge_wildfire")) {
                    const float spreadRadius = radius * (hasEquippedSkillNode("thermal_surge_wildfire_mastery") ? 1.20f : 1.0f);
                    for (Enemy& spread : enemyList) if (spread.alive && spread.id != enemy.id && distanceSquared(spread.pos, enemy.pos) <= spreadRadius * spreadRadius) {
                        spread.burn = std::max(spread.burn, static_cast<float>(definition.durationTicks) / (TickRate * 2));
                        spread.burnDps = std::max(spread.burnDps, valueA * 0.40f);
                        spread.burnTicks = std::max(spread.burnTicks, definition.durationTicks / 2);
                        spread.stormLastSetupSkill = static_cast<int>(request.skill);
                    }
                }
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "flash_flood") {
            const float length = 280.0f;
            const float dx = request.target.direction.x;
            const float dy = request.target.direction.y;
            const float magnitude = std::sqrt(dx * dx + dy * dy);
            if (magnitude > 0.001f) {
                const float nx = dx / magnitude;
                const float ny = dy / magnitude;
                for (Enemy& enemy : enemyList) if (enemy.alive) {
                    const float ex = enemy.pos.x - request.target.world.x;
                    const float ey = enemy.pos.y - request.target.world.y;
                    const float along = ex * nx + ey * ny;
                    const float side = std::abs(ex * ny - ey * nx);
                    if (along < 0.0f || along > length || side > radius) continue;
                    const int before = counters.damageDealt;
                    applyDamage(enemy, valueA);
                    counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
                    const float floodPush = hasEquippedSkillNode("flash_flood_deluge") ? 28.0f : 42.0f;
                    enemy.pos.x = std::max(92.0f, enemy.pos.x + nx * std::min(floodPush, length - along));
                    enemy.pos.y += ny * std::min(floodPush, length - along);
                    if (hasEquippedSkillNode("flash_flood_undertow")) {
                        const float signedSide = ex * ny - ey * nx;
                        const float pull = std::min(std::abs(signedSide), 10.0f * (hasEquippedSkillNode("flash_flood_undertow_mastery") ? 1.35f : 1.0f));
                        enemy.pos.x -= (signedSide >= 0.0f ? ny : -ny) * pull;
                        enemy.pos.y += (signedSide >= 0.0f ? nx : -nx) * pull;
                    }
                    if (enemy.shockTicks > 0) {
                        if (resolveStormReaction(enemy, 1, valueA)) {
                            consumeReactionStates(enemy, 1);
                            if (hasEquippedSkillNode("flash_flood_capstone") || hasEquippedSkillNode("flash_flood_deluge_mastery")) economyState->stormTidalMemoryReady = 1;
                        }
                    } else {
                        const int soakDuration = economyState->stormTidalMemoryReady > 0 ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.5f)) : definition.durationTicks;
                        enemy.soakTicks = std::max(enemy.soakTicks, soakDuration);
                        enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                        if (economyState->stormTidalMemoryReady > 0) economyState->stormTidalMemoryReady = 0;
                    }
                    ++counters.statusApplications;
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                }
            }
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, radius, std::min(definition.durationTicks, 12), "flood");
            executed = true;
        } else if (operation == "eye_storm") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                executed = true;
            }
        } else if (operation == "apply_gale") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.galeTicks = std::max(enemy.galeTicks, definition.durationTicks);
                enemy.stormLastSetupSkill = static_cast<int>(request.skill);
                enemy.vulnerability = std::max(enemy.vulnerability, valueA);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "resolve_reaction") {
            const float reactionRadius = radius * ((request.skill == SkillId::ChainLightning && hasEquippedSkillNode("chain_bounce"))
                ? (1.22f * (hasEquippedSkillNode("chain_bounce_mastery") ? 1.12f : 1.0f))
                : 1.0f);
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= reactionRadius * reactionRadius) {
                const bool shocked = hasStormState(enemy, "shock");
                const bool soaked = hasStormState(enemy, "soak");
                const bool ignited = hasStormState(enemy, "ignite");
                if (!(shocked || soaked || ignited || enemy.freezeTicks > 0)) continue;
                if (reactionRequirementsMet(enemy, 3) && !shocked && !soaked && !ignited) {
                    if (resolveStormReaction(enemy, 3, valueA)) consumeReactionStates(enemy, 3);
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                    continue;
                }
                int reactionId = 0;
                int reactionPriority = 1000000;
                const auto considerReaction = [&](bool legal, int candidateId) {
                    const SkillReactionDefinition* candidate = reactionDefinition(candidateId);
                    if (legal && candidate != nullptr && candidate->priority < reactionPriority) { reactionId = candidateId; reactionPriority = candidate->priority; }
                };
                for (const SkillReactionDefinition& candidate : content.skillReactions) considerReaction(reactionRequirementsMet(enemy, candidate.reactionId), candidate.reactionId);
                if (reactionId == 0) continue;
                if (!resolveStormReaction(enemy, reactionId, valueA)) continue;
                consumeReactionStates(enemy, reactionId);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "apply_slow") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.slow = std::max(enemy.slow, std::max(0.05f, valueA));
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                counters.skillControlTicks[static_cast<std::size_t>(request.skill)] += definition.durationTicks;
            }
            executed = true;
        } else if (operation == "damage_over_time") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                const bool poison = std::find(definition.tags.begin(), definition.tags.end(), "poison") != definition.tags.end() ||
                                    std::find(definition.tags.begin(), definition.tags.end(), "infection") != definition.tags.end();
                if (poison) {
                    enemy.poisonDps = std::max(enemy.poisonDps, valueA);
                    enemy.poisonTicks = std::max(enemy.poisonTicks, definition.durationTicks);
                    enemy.poison = std::max(enemy.poison, static_cast<float>(definition.durationTicks) / TickRate);
                } else {
                    enemy.burnDps = std::max(enemy.burnDps, valueA);
                    enemy.burnTicks = std::max(enemy.burnTicks, definition.durationTicks);
                    enemy.burn = std::max(enemy.burn, static_cast<float>(definition.durationTicks) / TickRate);
                }
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "heal_allies") {
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) {
                const float before = unit.hp;
                unit.hp = std::min(unit.maxHp, unit.hp + valueA);
                counters.skillHealing[static_cast<std::size_t>(request.skill)] += static_cast<int>(std::round(unit.hp - before));
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "buff_allies") {
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) {
                unit.damageScale = std::max(unit.damageScale, valueA);
                unit.speedScale = std::max(unit.speedScale, std::max(1.0f, valueB));
                unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "generate_scrap") {
            const int before = economyState->resources.scrap;
            economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + std::max(0, static_cast<int>(std::round(valueA))));
            (void)before;
            executed = true;
        } else if (operation == "boost_deployed_collectors") {
            for (RecoveryDrone& drone : economyState->drones) drone.boostTicks = std::max(drone.boostTicks, definition.durationTicks);
            executed = !economyState->drones.empty();
        } else if (operation == "consume_shock") {
            Enemy* resonantTarget = nullptr;
            float resonantScore = -1.0f;
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.shockTicks > 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                const int before = counters.damageDealt;
                if (economyState->stormPerfectTicks <= 0) enemy.shockTicks = 0;
                applyDamage(enemy, valueA * (enemy.soakTicks > 0 ? 1.25f : 1.0f));
                if (hasEquippedSkillNode("resonance_dampener_capstone")) enemy.signalJamTicks = std::max(enemy.signalJamTicks, definition.durationTicks);
                counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
                ++counters.reactionTriggers;
                ++economyState->stormReactions;
                if ((skillLoadoutState.doctrineId == "stormcaller_resonance" || hasEquippedSkillNode("chain_capstone")) && economyState->stormReactions % 3 == 0) {
                    applyDamage(enemy, valueA * 0.35f);
                    ++counters.reactionTriggers;
                }
                const float weakenedScore = enemy.vulnerability * 10.0f + (enemy.maxHp > 0.0f ? (1.0f - enemy.hp / enemy.maxHp) : 0.0f);
                if (resonantTarget == nullptr || weakenedScore > resonantScore || (weakenedScore == resonantScore && enemy.id < resonantTarget->id)) {
                    resonantTarget = &enemy;
                    resonantScore = weakenedScore;
                }
            }
            if (hasEquippedSkillNode("resonance_amplifier_capstone") && resonantTarget != nullptr && resonantTarget->alive) {
                applyDamage(*resonantTarget, valueA * 0.45f);
                ++counters.reactionTriggers;
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, resonantTarget->pos, radius * 0.65f, 10, "resonant_break");
            }
            executed = true;
        } else if (operation == "place_wall") {
            const std::size_t before = buildings.size();
            constexpr int buildCost = 30;
            if (!canSpendBuildSupply(buildCost)) continue;
            spawnBuilding(request.target.world, request.skill, "wall", definition.durationTicks, valueA);
            if (buildings.size() > before) {
                DeployableBuilding& wall = buildings.back();
                wall.footprintRadius = radius;
                wall.effectValue = hasEquippedSkillNode("wall_reactive") ? valueB : 0.0f;
                if (hasEquippedSkillNode("wall_rampart")) {
                    DeployableBuilding* nearest = nullptr;
                    float nearestDistance = std::numeric_limits<float>::max();
                    for (DeployableBuilding& candidate : buildings) {
                        if (!candidate.alive || candidate.id == wall.id || candidate.role != "wall" || candidate.linkedBuildingId != 0) continue;
                        const float distance = distanceSquared(candidate.pos, wall.pos);
                        const float linkRange = candidate.footprintRadius + wall.footprintRadius + 28.0f;
                        if (distance > linkRange * linkRange || distance > nearestDistance) continue;
                        if (nearest == nullptr || distance < nearestDistance || (distance == nearestDistance && candidate.id < nearest->id)) {
                            nearest = &candidate;
                            nearestDistance = distance;
                        }
                    }
                    if (nearest != nullptr) {
                        wall.linkedBuildingId = nearest->id;
                        nearest->linkedBuildingId = wall.id;
                        const float rampartScale = 1.12f * (hasEquippedSkillNode("wall_rampart_mastery") ? 1.08f : 1.0f);
                        wall.maxHp *= rampartScale;
                        wall.hp = std::min(wall.maxHp, wall.hp * rampartScale);
                        nearest->maxHp *= rampartScale;
                        nearest->hp = std::min(nearest->maxHp, nearest->hp * rampartScale);
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, wall.pos, wall.footprintRadius + nearest->footprintRadius + 28.0f, TickRate, "rampart_link");
                    }
                }
                spendBuildSupply(buildCost);
            }
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, radius, definition.durationTicks, "wall");
            executed = buildings.size() > before;
        } else if (operation == "place_trap") {
            const std::size_t before = buildings.size();
            constexpr int buildCost = 15;
            if (!canSpendBuildSupply(buildCost)) continue;
            spawnBuilding(request.target.world, request.skill, "trap", definition.durationTicks, 24.0f);
            if (buildings.size() > before) {
                DeployableBuilding& trap = buildings.back();
                const float controlScale = hasEquippedSkillNode("scrap_trap_mastery") ? 1.18f : 1.0f;
                trap.footprintRadius = radius * controlScale;
                trap.effectValue = valueB;
                trap.charges = std::max(1, static_cast<int>(std::round(valueA)));
                if (skillLoadoutState.doctrineId == "architect_trapfoundry") ++trap.charges;
                if (request.skill == SkillId::TrapFoundry && hasEquippedSkillNode("trap_cryo_mastery")) {
                    DeployableBuilding* nearest = nullptr;
                    float nearestDistance = std::numeric_limits<float>::max();
                    for (DeployableBuilding& candidate : buildings) {
                        if (!candidate.alive || candidate.id == trap.id || candidate.role != "trap" || candidate.ownerSkill != SkillId::TrapFoundry) continue;
                        const float candidateDistance = distanceSquared(candidate.pos, trap.pos);
                        if (candidateDistance < nearestDistance || (candidateDistance == nearestDistance && candidate.id < nearest->id)) {
                            nearest = &candidate;
                            nearestDistance = candidateDistance;
                        }
                    }
                    if (nearest != nullptr) {
                        trap.linkedBuildingId = nearest->id;
                        nearest->linkedBuildingId = trap.id;
                    }
                }
                spendBuildSupply(buildCost);
            }
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, radius * (hasEquippedSkillNode("scrap_trap_mastery") ? 1.18f : 1.0f), definition.durationTicks, "trap");
            executed = buildings.size() > before;
        } else if (operation == "accelerate") {
            const bool arsenal = hasEquippedSkillNode("accelerate_arsenal");
            const bool arsenalMastery = hasEquippedSkillNode("accelerate_arsenal_mastery");
            const bool localSingularity = hasEquippedSkillNode("accelerate_capstone");
            const bool summons = hasEquippedSkillNode("accelerate_summons");
            const bool summonsMastery = hasEquippedSkillNode("accelerate_summons_mastery");
            bool firstStructure = true;
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) {
                unit.speedScale = std::max(unit.speedScale, summons ? 1.95f : 1.65f);
                unit.damageScale = std::max(unit.damageScale, summons ? 1.32f : 1.15f);
                unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks);
                if (summonsMastery) unit.accelerationTailTicks = std::max(unit.accelerationTailTicks, definition.durationTicks / 3);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            for (DeployableBuilding& building : buildings) if (building.alive && distanceSquared(building.pos, request.target.world) <= radius * radius) {
                const float structureScale = localSingularity && firstStructure ? 3.0f : (arsenal ? 2.15f : 1.65f);
                building.actionSpeedScale = std::max(building.actionSpeedScale, structureScale);
                building.actionSpeedTicks = std::max(building.actionSpeedTicks, definition.durationTicks);
                const int cooldownAdvance = arsenalMastery ? 14 : 8;
                building.attackCooldownTicks = std::max(0, building.attackCooldownTicks - cooldownAdvance);
                building.spawnCooldownTicks = std::max(0, building.spawnCooldownTicks - cooldownAdvance);
                firstStructure = false;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            economyState->resources.paradox = std::min(100, economyState->resources.paradox + 8);
            executed = true;
        } else if (operation == "delay_event") {
            Enemy* target = nullptr;
            float nearest = radius * radius;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= nearest) {
                const float distance = distanceSquared(enemy.pos, request.target.world);
                if (target == nullptr || distance < nearest || enemy.id < target->id) { nearest = distance; target = &enemy; }
            }
            const bool delayable = target != nullptr &&
                (target->telegraphTicks > 0 || target->attackCooldownTicks <= TickRate ||
                 (target->type == EnemyType::Teleporter && target->teleportCooldown <= 1.0f));
            if (delayable) {
                const int delayTicks = target->boss && hasEquippedSkillNode("delay_suspended") ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.18f)) : (skillLoadoutState.doctrineId == "chronomancer_anchor" ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.20f)) : definition.durationTicks);
                target->temporalDelayTicks = std::max(target->temporalDelayTicks, delayTicks);
                target->vulnerability = std::max(target->vulnerability, valueA);
                target->vulnerabilityTicks = std::max(target->vulnerabilityTicks, delayTicks);
                if (!target->boss && hasEquippedSkillNode("delay_capstone")) target->temporalCancelTicks = std::max(target->temporalCancelTicks, 1);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            if (delayable && hasEquippedSkillNode("delay_stolen")) {
                int cooldownAdvance = TickRate / 2;
                if (hasEquippedSkillNode("delay_stolen_mastery")) cooldownAdvance += TickRate / 3;
                for (std::size_t index = 0; index < SkillSlotCount; ++index) {
                    if (index == request.slot) continue;
                    if (skillCooldowns[index] > 0) { skillCooldowns[index] = std::max(0, skillCooldowns[index] - cooldownAdvance); break; }
                }
            }
            if (delayable) {
                economyState->resources.paradox = std::min(100, economyState->resources.paradox + 12);
                executed = true;
            }
        } else if (operation == "rewind_enemies") {
            const bool preciseRecall = hasEquippedSkillNode("rewind_precise");
            Enemy* preciseTarget = nullptr;
            if (preciseRecall) {
                for (Enemy& candidate : enemyList) {
                    if (!candidate.alive || distanceSquared(candidate.pos, request.target.world) > radius * radius) continue;
                    if (preciseTarget == nullptr || (candidate.boss && !preciseTarget->boss) || candidate.maxHp > preciseTarget->maxHp || (candidate.maxHp == preciseTarget->maxHp && candidate.id < preciseTarget->id)) preciseTarget = &candidate;
                }
            }
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (!preciseRecall || &enemy == preciseTarget)) {
                if (enemy.pathHistoryCount > 1) {
                    const int historySteps = std::clamp(static_cast<int>(std::round(valueA / 24.0f)), 1, enemy.pathHistoryCount - 1);
                    const int historySize = static_cast<int>(enemy.pathHistory.size());
                    const int historyIndex = (enemy.pathHistoryHead - historySteps + historySize) % historySize;
                    enemy.pos = enemy.pathHistory[static_cast<std::size_t>(historyIndex)];
                } else {
                    enemy.pos.x = std::max(92.0f, enemy.pos.x - valueA);
                    enemy.pos.y = pathY(enemy.pos.x, enemy.id);
                }
                enemy.temporalDelayTicks = std::max(enemy.temporalDelayTicks, hasEquippedSkillNode("rewind_precise_mastery") ? TickRate / 2 : TickRate / 3);
                if (hasEquippedSkillNode("rewind_mass_mastery")) enemy.temporalEchoTicks = std::max(enemy.temporalEchoTicks, hasEquippedSkillNode("rewind_mass_mastery") ? 1 : 0);
                if (hasEquippedSkillNode("rewind_capstone")) {
                    enemy.vulnerability = std::max(enemy.vulnerability, 0.22f);
                    enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, TickRate * 2);
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(request.skill)];
                if (preciseRecall) emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, enemy.pos, 34.0f, TickRate / 2, "elite_recall");
            }
            economyState->resources.paradox = std::min(100, economyState->resources.paradox + 16);
            executed = true;
        } else if (operation == "borrow_time") {
            const bool flexibleCredit = hasEquippedSkillNode("borrowed_flexible");
            const bool flexibleMastery = hasEquippedSkillNode("borrowed_flexible_mastery");
            const bool allIn = hasEquippedSkillNode("borrowed_all_in");
            const bool allInMastery = hasEquippedSkillNode("borrowed_all_in_mastery");
            const int maximumRefreshes = allIn ? (allInMastery ? 3 : 2) : 1;
            int refreshed = 0;
            for (std::size_t index = 0; index < SkillSlotCount; ++index) if (index != request.slot && skillCooldowns[index] > 0) {
                const SkillDefinition& borrowed = skillDefinition(skillLoadoutState.skills[index]);
                skillCooldowns[index] = 0;
                skillCharges[index] = std::max(1, borrowed.charges);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                if (++refreshed >= maximumRefreshes) break;
            }
            const int baseDebt = allIn ? (allInMastery ? 42 : 36) : (flexibleCredit ? (flexibleMastery ? 12 : 16) : 24);
            economyState->resources.paradox = std::min(100, economyState->resources.paradox + baseDebt);
            if (refreshed > 0 && skillLoadoutState.doctrineId == "chronomancer_debt") economyState->chronomancerDebtBurstTicks = std::max(economyState->chronomancerDebtBurstTicks, TickRate * 3);
            if (refreshed > 0 && allIn && hasEquippedSkillNode("borrowed_capstone")) economyState->resources.paradox = std::max(0, economyState->resources.paradox - 12);
            executed = true;
        } else if (operation == "mark_bounty") {
            Enemy* target = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius &&
                (target == nullptr || enemy.boss || enemy.hp > target->hp)) target = &enemy;
            if (target != nullptr) {
                target->bountyTicks = std::max(target->bountyTicks, definition.durationTicks);
                target->bountyId = economyState->nextBountyId++;
                economyState->activeBountyId = target->bountyId;
                economyState->activeBountyTargetId = target->id;
                economyState->bountyAgeTicks = 0;
                economyState->bountyIsolationTicks = 0;
                economyState->bountyObjectivesCompleted = 0;
                economyState->bountyTagMask = 0;
                assignBountyObjectives(content, seed, target->id, target->boss, *economyState);
                const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                if (economyState->bountyKillingMomentumReady != 0 && identity.primaryGroup == "bounty_hunter" && identity.primaryCount >= 5) {
                    economyState->bountyMomentumObjective = -1;
                    for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) {
                        if (economyState->bountyObjectiveKinds[objective] >= 0 && economyState->bountyObjectiveTargets[objective] > 1) {
                            economyState->bountyMomentumObjective = static_cast<int>(objective);
                            economyState->bountyObjectiveProgress[objective] = economyState->bountyObjectiveTargets[objective] - 1;
                            break;
                        }
                    }
                    if (economyState->bountyMomentumObjective < 0) economyState->bountyMomentumObjective = 0;
                    economyState->bountyKillingMomentumReady = 0;
                }
                if (skillLoadoutState.doctrineId == "bounty_hunter_collector" && economyState->bountyCollectorReady != 0) {
                    valueA *= 1.15f;
                    economyState->bountyCollectorReady = 0;
                }
                target->vulnerability = std::max(target->vulnerability, valueA);
                target->vulnerabilityTicks = std::max(target->vulnerabilityTicks, definition.durationTicks);
                if (hasEquippedSkillNode("wanted_isolation")) economyState->bountyIsolationTicks = std::min(TickRate * 10, TickRate);
                if (hasEquippedSkillNode("wanted_exploit")) {
                    static constexpr const char* weaknesses[] = {"projectile", "summon", "structure", "elemental", "direct"};
                    const bool retained = hasEquippedSkillNode("exploit_capstone") && economyState->bountyRetainedWeaknessReady != 0 && !economyState->bountyRetainedWeakness.empty();
                    target->weaknessTag = retained ? economyState->bountyRetainedWeakness : weaknesses[static_cast<std::size_t>((static_cast<std::uint32_t>(target->id) + seed) % 5u)];
                    if (retained) {
                        economyState->bountyRetainedWeakness.clear();
                        economyState->bountyRetainedWeaknessReady = 0;
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target->pos, 38.0f, TickRate, "perfect_counter");
                    }
                    target->weaknessRewarded = false;
                    target->vulnerability = std::max(target->vulnerability, valueA * (hasEquippedSkillNode("wanted_exploit_mastery") ? 1.25f : 1.10f));
                    target->vulnerabilityTicks = std::max(target->vulnerabilityTicks, definition.durationTicks);
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "deadeye_shot" || operation == "harpoon" || operation == "exploit_weakness") {
            Enemy* target = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius &&
                (target == nullptr || (enemy.boss && !target->boss) || enemy.hp > target->hp || (enemy.hp == target->hp && enemy.id < target->id))) target = &enemy;
            if (target != nullptr) {
                if (target->bountyId == economyState->activeBountyId) {
                    for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) {
                        if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "distinct_tags")) {
                            const std::uint32_t tag = 1u << (static_cast<std::uint32_t>(request.skill) % 5u);
                            economyState->bountyTagMask |= tag;
                            std::uint32_t tags = economyState->bountyTagMask;
                            int distinctTags = 0;
                            while (tags != 0u) { distinctTags += static_cast<int>(tags & 1u); tags >>= 1u; }
                            economyState->bountyObjectiveProgress[objective] = std::min(economyState->bountyObjectiveTargets[objective], distinctTags);
                        } else if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "interrupt") && target->telegraphTicks > 0) {
                            economyState->bountyObjectiveProgress[objective] = economyState->bountyObjectiveTargets[objective];
                        }
                    }
                }
                if (operation == "deadeye_shot") {
                    const bool isolated = std::count_if(enemyList.begin(), enemyList.end(), [&](const Enemy& other) { return other.alive && other.id != target->id && distanceSquared(other.pos, target->pos) < 120.0f * 120.0f; }) == 0;
                    const bool executionWindow = hasEquippedSkillNode("deadeye_execution") && target->hp <= target->maxHp * 0.5f;
                    const float shotDamage = valueA * (skillLoadoutState.doctrineId == "bounty_hunter_deadeye" ? 1.15f : 1.0f) * (isolated ? 1.5f : 1.0f) * (executionWindow ? 1.35f : 1.0f);
                    const bool perfectCaliber = hasEquippedSkillNode("deadeye_arsenal_capstone") && target->bountyId != 0 && target->bountyId == economyState->activeBountyId && target->weaknessTag == bountyWeaknessForSkill(request.skill);
                    applySkillDamage(*target, shotDamage, request.skill);
                    if (perfectCaliber) {
                        const Vec2 repeatPosition = target->pos;
                        if (target->alive) {
                            const int beforeRepeat = counters.damageDealt;
                            applyDamage(*target, shotDamage * 0.35f);
                            if (counters.damageDealt > beforeRepeat) counters.skillDamage[static_cast<std::size_t>(request.skill)] += counters.damageDealt - beforeRepeat;
                        }
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, repeatPosition, 22.0f, TickRate / 2, "perfect_caliber");
                    }
                    if (target->bountyId == economyState->activeBountyId) {
                        for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "direct_hit")) economyState->bountyObjectiveProgress[objective] = economyState->bountyObjectiveTargets[objective];
                        if (isolated && hasEquippedSkillNode("deadeye_capstone")) economyState->resources.trophies = std::min(100, economyState->resources.trophies + 1);
                        economyState->bountyObjectivesCompleted = 0;
                        for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) ++economyState->bountyObjectivesCompleted;
                    }
                } else if (operation == "harpoon") {
                    target->pos.x = std::max(92.0f, target->pos.x - valueA * (target->boss ? 0.45f : 1.0f));
                    target->slow = std::max(target->slow, target->boss && hasEquippedSkillNode("harpoon_trap") ? 0.9f : 0.65f);
                    if (hasEquippedSkillNode("harpoon_isolate")) for (Enemy& nearby : enemyList) if (nearby.alive && !nearby.boss && nearby.id != target->id && distanceSquared(nearby.pos, target->pos) <= 100.0f * 100.0f) nearby.pos.x = std::min(static_cast<float>(Width - 40), nearby.pos.x + 32.0f);
                    if (target->bountyId == economyState->activeBountyId) economyState->bountyIsolationTicks = hasEquippedSkillNode("harpoon_capstone") || hasEquippedSkillNode("harpoon_trap_capstone") ? TickRate * 10 : std::min(TickRate * 10, economyState->bountyIsolationTicks + TickRate / 2);
                } else {
                    static constexpr const char* weaknesses[] = {"projectile", "summon", "structure", "elemental", "direct"};
                    target->weaknessTag = weaknesses[static_cast<std::size_t>((static_cast<std::uint32_t>(target->id) + seed) % 5u)];
                    target->weaknessRewarded = false;
                    const int weaknessDuration = target->bountyId == economyState->activeBountyId && hasEquippedSkillNode("exploit_contract") ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.5f)) : definition.durationTicks;
                    target->vulnerability = std::max(target->vulnerability, valueA * (hasEquippedSkillNode("exploit_adapt") ? 1.15f : 1.0f));
                    target->vulnerabilityTicks = std::max(target->vulnerabilityTicks, weaknessDuration);
                    if (target->bountyId == economyState->activeBountyId && hasEquippedSkillNode("exploit_contract_capstone")) economyState->resources.trophies = std::min(100, economyState->resources.trophies + 1);
                    if (target->bountyId == economyState->activeBountyId) for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "weakness_revealed")) economyState->bountyObjectiveProgress[objective] = economyState->bountyObjectiveTargets[objective];
                }
                if (target->bountyId == economyState->activeBountyId) {
                    economyState->bountyObjectivesCompleted = 0;
                    for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (economyState->bountyObjectiveKinds[objective] >= 0 && economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) ++economyState->bountyObjectivesCompleted;
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "collector_drone") {
            int speedRank = 0;
            for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) if (skillLoadoutState.skills[slot] == SkillId::CollectorDrone) speedRank = std::max(speedRank, std::min(skillNodeRank(slot, "collector_speed"), 3));
            const float speed = 18.0f * std::pow(1.15f, static_cast<float>(speedRank));
            spawnAlliedUnit(request.target.world, request.skill, "collector_drone", definition.durationTicks, 72.0f, 0.0f, speed);
            ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
            executed = true;
        } else if (operation == "infect") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                const int infectionDuration = definition.durationTicks + (skillLoadoutState.doctrineId == "plaguewright_necrotic" ? TickRate * 3 : 0) + (hasEquippedSkillNode("patient_necrotic_mastery") ? TickRate * 2 : 0);
                enemy.infectionTicks = std::max(enemy.infectionTicks, infectionDuration);
                enemy.infectionStacks = std::min(5, std::max(enemy.infectionStacks, 1));
                enemy.infectionGeneration = 0;
                enemy.pandemicSpreadUsed = false;
                recordPlagueHost(enemy);
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "vector_swarm") {
            int spreadCount = 0;
            bool bridgeUsed = false;
            const bool packEpidemic = hasEquippedSkillNode("vector_rabid_capstone");
            for (Enemy& source : enemyList) {
                if (!source.alive || source.infectionTicks <= 0 || distanceSquared(source.pos, request.target.world) > radius * radius) continue;
                const bool primeSource = economyState->pandemicTicks > 0 && source.infectionStrain == economyState->pandemicPrimeStrain && (source.id == economyState->pandemicPrimeHostId || source.infectionGeneration > 0);
                if (primeSource && source.pandemicSpreadUsed) continue;
                bool sourceSpread = false;
                for (Enemy& target : enemyList) {
                    if (spreadCount >= (packEpidemic ? 7 : 6) || !target.alive || target.infectionTicks > 0 || target.id == source.id) continue;
                    const float targetDistance = distanceSquared(target.pos, source.pos);
                    const bool normalRange = targetDistance <= 110.0f * 110.0f;
                    const bool bridgeRange = packEpidemic && !bridgeUsed && targetDistance <= 165.0f * 165.0f;
                    if (!normalRange && !bridgeRange) continue;
                    target.infectionTicks = definition.durationTicks;
                    const bool retainStack = spreadCount == 0 && hasEquippedSkillNode("vector_necrotic_capstone");
                    target.infectionStacks = std::min(5, std::max(1, source.infectionStacks - 1 + (retainStack ? 1 : 0)));
                    target.infectionGeneration = std::min(2, source.infectionGeneration + 1);
                    target.infectionStrain = source.infectionStrain;
                    target.pandemicSpreadUsed = primeSource;
                    recordPlagueHost(target);
                    if (hasEquippedSkillNode("vector_rabid")) {
                        target.confusionTicks = std::max(target.confusionTicks, TickRate * 2);
                        if (!target.boss) { target.allegiance = 1; target.allegianceTicks = std::max(target.allegianceTicks, TickRate * 2); }
                    }
                    if (bridgeRange && !normalRange) {
                        bridgeUsed = true;
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target.pos, 30.0f, TickRate, "pack_epidemic");
                    }
                    ++spreadCount;
                    sourceSpread = true;
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                }
                if (primeSource && sourceSpread) source.pandemicSpreadUsed = true;
            }
            economyState->resources.biomass = std::min(100, economyState->resources.biomass + spreadCount);
            executed = true;
        } else if (operation == "mutation") {
            const int strain = std::clamp(static_cast<int>(std::round(valueA)), 1, 4);
            int mutated = 0;
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.infectionTicks > 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.infectionStrain = strain;
                enemy.infectionStacks = std::min(5, enemy.infectionStacks + 1);
                enemy.infectionTicks = std::max(enemy.infectionTicks, definition.durationTicks);
                enemy.vulnerability = std::max(enemy.vulnerability, valueB);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                ++mutated;
            }
            if (mutated > 0 && request.skill == SkillId::Mutation && economyState->plagueFreeMutationReady != 0) economyState->plagueFreeMutationReady = 0;
            if (mutated > 0 && hasEquippedSkillNode("mutation_biomass")) economyState->resources.biomass = std::min(100, economyState->resources.biomass + 2);
            executed = true;
        } else if (operation == "rupture_host") {
            Enemy* host = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.infectionTicks > 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius &&
                (host == nullptr || enemy.infectionStacks > host->infectionStacks || (enemy.infectionStacks == host->infectionStacks && enemy.id < host->id))) host = &enemy;
            if (host != nullptr) {
                const int strain = host->infectionStrain;
                const int stacks = host->infectionStacks;
                const Vec2 hostPos = host->pos;
                const bool primeHost = economyState->pandemicTicks > 0 && strain == economyState->pandemicPrimeStrain;
                const PlagueMutationDefinition* mutation = nullptr;
                for (const PlagueMutationDefinition& candidate : content.plagueMutations) if (candidate.strain == strain) { mutation = &candidate; break; }
                const bool sporeBurst = mutation != nullptr && mutation->behavior == "burst" && hasEquippedSkillNode("mutation_spore");
                const bool septicBurst = hasEquippedSkillNode("rupture_septic");
                const float spreadRadius = radius * (sporeBurst ? 1.25f : 1.0f) * (septicBurst ? 1.20f : 1.0f);
                applySkillDamage(*host, valueA + static_cast<float>(stacks) * 8.0f, request.skill);
                host->infectionTicks = 0; host->infectionStacks = 0;
                for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, hostPos) <= spreadRadius * spreadRadius) {
                    enemy.infectionTicks = std::max(enemy.infectionTicks, definition.durationTicks / (septicBurst || primeHost ? 1 : 2));
                    enemy.infectionStacks = std::max(enemy.infectionStacks, std::max(1, stacks - (primeHost ? 0 : 1)));
                    enemy.infectionStrain = strain;
                    enemy.pandemicSpreadUsed = false;
                }
                if (sporeBurst && hasEquippedSkillNode("mutation_spore_capstone")) {
                    Enemy* preserved = nullptr;
                    for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, hostPos) <= spreadRadius * spreadRadius && (preserved == nullptr || enemy.id < preserved->id)) preserved = &enemy;
                    if (preserved != nullptr) {
                        preserved->infectionTicks = std::max(preserved->infectionTicks, definition.durationTicks);
                        preserved->infectionStacks = std::max(preserved->infectionStacks, stacks);
                        preserved->infectionStrain = strain;
                    }
                }
                economyState->resources.biomass = std::min(100, economyState->resources.biomass + stacks * (hasEquippedSkillNode("rupture_living") ? 3 : 2));
                if (septicBurst && hasEquippedSkillNode("rupture_septic_capstone") && zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, hostPos, spreadRadius * 0.72f, TickRate * 3, 0, static_cast<float>(std::max(1, stacks)), static_cast<float>(strain), request.skill, false, true});
                if (hasEquippedSkillNode("rupture_living_capstone")) {
                    Enemy* preserved = nullptr;
                    for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, hostPos) <= spreadRadius * spreadRadius && (preserved == nullptr || enemy.id < preserved->id)) preserved = &enemy;
                    if (preserved != nullptr) {
                        preserved->infectionTicks = std::max(preserved->infectionTicks, definition.durationTicks);
                        preserved->infectionStacks = std::max(preserved->infectionStacks, 1);
                        preserved->infectionStrain = strain;
                    }
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "quarantine") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                executed = true;
            }
        } else if (operation == "mine_layer") {
            const bool freeMine = hasEquippedSkillNode("mine_scrap_capstone") && economyState->mineFoundryWave != wave;
            const float trapDiscount = hasEquippedSkillNode("scrap_trap") ? 0.80f : 1.0f;
            const int mineCost = freeMine ? 0 : static_cast<int>(std::round(static_cast<float>(valueB) * trapDiscount));
            constexpr int buildCost = 12;
            if (canSpendBuildSupply(buildCost) && economyState->resources.scrap >= mineCost) {
                economyState->resources.scrap -= mineCost;
                if (freeMine) economyState->mineFoundryWave = wave;
                DeployableBuilding& trap = buildings.emplace_back();
                trap.id = nextBuildingId++; trap.pos = request.target.world; trap.hp = trap.maxHp = valueA; trap.lifetimeTicks = definition.durationTicks;
                const float controlScale = hasEquippedSkillNode("scrap_trap_mastery") ? 1.18f : 1.0f;
                trap.footprintRadius = radius * controlScale; trap.effectValue = valueA * 0.8f; trap.charges = 3; trap.ownerSkill = request.skill; trap.role = "trap";
                if (skillLoadoutState.doctrineId == "architect_trapfoundry") { ++trap.charges; trap.networkRearmScale = 1.25f; }
                if (request.skill == SkillId::TrapFoundry && hasEquippedSkillNode("trap_cryo_mastery")) {
                    DeployableBuilding* nearest = nullptr;
                    float nearestDistance = std::numeric_limits<float>::max();
                    for (DeployableBuilding& candidate : buildings) {
                        if (!candidate.alive || candidate.id == trap.id || candidate.role != "trap" || candidate.ownerSkill != SkillId::TrapFoundry) continue;
                        const float candidateDistance = distanceSquared(candidate.pos, trap.pos);
                        if (candidateDistance < nearestDistance || (candidateDistance == nearestDistance && candidate.id < (nearest == nullptr ? std::numeric_limits<int>::max() : nearest->id))) {
                            nearestDistance = candidateDistance;
                            nearest = &candidate;
                        }
                    }
                    if (nearest != nullptr) {
                        trap.linkedBuildingId = nearest->id;
                        nearest->linkedBuildingId = trap.id;
                    }
                }
                spendBuildSupply(buildCost);
                recordSalvagerConstruction(1);
                ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "jury_rigged_turret") {
            const bool paired = hasEquippedSkillNode("turret_swarm");
            const int cost = static_cast<int>(std::round(valueB * (paired ? 0.72f : 1.0f)));
            constexpr int buildCost = 20;
            if (canSpendBuildSupply(buildCost) && economyState->resources.scrap >= cost) {
                economyState->resources.scrap -= cost;
                const bool transferredModule = economyState->salvageModuleReady > 0;
                const float moduleScale = transferredModule ? (economyState->salvageModuleReady > 1 ? 1.18f : 1.10f) : 1.0f;
                const std::size_t beforeBuildings = buildings.size();
                const std::string primaryRole = hasEquippedSkillNode("turret_mortar") ? "mortar" : "sentry";
                spawnBuilding(request.target.world, request.skill, primaryRole, definition.durationTicks, valueA * moduleScale);
                if (primaryRole == "mortar" && hasEquippedSkillNode("turret_mortar_capstone") && economyState->salvageBatteryWave != wave && buildings.size() > beforeBuildings) {
                    buildings[beforeBuildings].attackCooldownTicks = 0;
                    economyState->salvageBatteryWave = wave;
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, 42.0f, 8, "salvage_battery");
                }
                if (paired) spawnBuilding({request.target.world.x + 34.0f, request.target.world.y + 24.0f}, request.skill, "swarm", definition.durationTicks, valueA * 0.62f * moduleScale);
                if (transferredModule && buildings.size() > beforeBuildings) economyState->salvageModuleReady = 0;
                if (buildings.size() > beforeBuildings) { spendBuildSupply(buildCost); recordSalvagerConstruction(2); }
                ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "strip_for_parts") {
            DeployableBuilding* target = nullptr;
            for (DeployableBuilding& building : buildings) if (building.alive && distanceSquared(building.pos, request.target.world) <= radius * radius && (target == nullptr || building.id < target->id)) target = &building;
            if (target != nullptr) {
                economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + std::max(1, static_cast<int>(std::round(target->effectValue * valueA))));
                if (hasEquippedSkillNode("strip_emergency_capstone")) {
                    economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + 3);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Expire, target->pos, target->footprintRadius, TickRate, "last_resort");
                }
                if (hasEquippedSkillNode("strip_transfer")) economyState->salvageModuleReady = hasEquippedSkillNode("strip_transfer_capstone") ? 2 : 1;
                target->alive = false;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "improvised_arsenal") {
            const int spent = economyState->resources.scrap;
            economyState->resources.scrap = 0;
            if (spent > 0) {
                const std::size_t beforeBuildings = buildings.size();
                spawnBuilding(request.target.world, request.skill, "mortar", definition.durationTicks, valueA + static_cast<float>(spent) * valueB);
                if (buildings.size() > beforeBuildings) {
                    if (hasEquippedSkillNode("arsenal_barrage_capstone")) {
                        economyState->arsenalAmmoTicks = std::max(economyState->arsenalAmmoTicks, definition.durationTicks);
                        economyState->arsenalAmmoPayouts = 0;
                    }
                    if (hasEquippedSkillNode("arsenal_economy_capstone")) {
                        economyState->arsenalInventoryTicks = std::max(economyState->arsenalInventoryTicks, definition.durationTicks);
                        economyState->arsenalInventoryScrap = spent;
                    }
                    recordSalvagerConstruction(4);
                    if (economyState->salvagerMasterworkReady != 0) {
                        buildings.back().actionSpeedScale = 1.35f;
                        buildings.back().actionSpeedTicks = definition.durationTicks;
                        buildings.back().spawnCooldownTicks = 10;
                        economyState->salvagerMasterworkReady = 0;
                        economyState->salvagerConstructionMask = 0;
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, radius, TickRate * 2, "masterwork");
                    }
                }
                ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "spotter_drone") {
            const float coverageRadius = radius * (hasEquippedSkillNode("spotter_grid") ? 1.35f : 1.0f);
            const float projectionScale = (hasEquippedSkillNode("spotter_observer") ? 1.35f : 1.0f) * (skillLoadoutState.doctrineId == "artillerist_spotter" ? 1.20f : 1.0f);
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= coverageRadius * coverageRadius) {
                enemy.predictedTicks = std::max(enemy.predictedTicks, definition.durationTicks);
                const float projection = enemy.speed * static_cast<float>(definition.durationTicks) * projectionScale / static_cast<float>(TickRate);
                enemy.predictedPosition = {std::min(static_cast<float>(Width - 40), enemy.pos.x + projection), pathY(std::min(static_cast<float>(Width - 40), enemy.pos.x + projection), enemy.id)};
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            economyState->resources.targetingData = std::min(100, economyState->resources.targetingData + (hasEquippedSkillNode("spotter_capstone") ? 8 : 4) + (skillLoadoutState.doctrineId == "artillerist_spotter" ? 2 : 0));
            executed = true;
        } else if (operation == "rail_cannon") {
            const int before = counters.damageDealt;
            const bool chargedShot = hasEquippedSkillNode("rail_charge") && economyState->resources.targetingData >= 8;
            if (chargedShot) economyState->resources.targetingData -= 8;
            for (Enemy& enemy : enemyList) if (enemy.alive && std::abs(enemy.pos.y - request.target.world.y) <= radius) {
                applySkillDamage(enemy, valueA * (enemy.predictedTicks > 0 ? 1.25f : 1.0f) * (chargedShot ? 1.25f : 1.0f), request.skill);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
            economyState->resources.targetingData = std::min(100, economyState->resources.targetingData + (hasEquippedSkillNode("rail_capstone") ? 12 : 8));
            executed = true;
        } else if (operation == "cluster_shell") {
            if (zones.size() < content.maxSkillZones) {
                const int armTicks = hasEquippedSkillNode("cluster_fast") ? definition.durationTicks / 5 : definition.durationTicks / 3;
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, armTicks, valueA, valueB, request.skill, false, true});
                if (hasEquippedSkillNode("cluster_capstone") && zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, {request.target.world.x + 46.0f, request.target.world.y - 28.0f}, radius * 0.72f, definition.durationTicks / 2, armTicks + 6, valueA * 0.55f, valueB, request.skill, false, true});
                executed = true;
            }
        } else if (operation == "walking_barrage") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                executed = true;
            }
        } else if (operation == "spatial_collapse") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                const float dx = request.target.world.x - enemy.pos.x;
                const float dy = request.target.world.y - enemy.pos.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.001f) { enemy.pos.x += dx / distance * std::min(distance, valueA); enemy.pos.y += dy / distance * std::min(distance, valueA); }
                if (hasEquippedSkillNode("collapse_capstone") && distance > 0.001f) {
                    const float remaining = std::max(0.0f, distance - valueA);
                    enemy.pos.x += dx / distance * std::min(remaining, valueA * 0.45f);
                    enemy.pos.y += dy / distance * std::min(remaining, valueA * 0.45f);
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            addVoidInstability(hasEquippedSkillNode("collapse_safe") ? 5 : 10);
            executed = true;
        } else if (operation == "banish") {
            Enemy* targetEnemy = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (targetEnemy == nullptr || enemy.hp > targetEnemy->hp || (enemy.hp == targetEnemy->hp && enemy.id < targetEnemy->id))) targetEnemy = &enemy;
            if (targetEnemy != nullptr) {
                const bool eliteExile = hasEquippedSkillNode("banish_elite");
                const int banishDuration = targetEnemy->boss
                    ? (eliteExile ? std::max(1, static_cast<int>(std::ceil(static_cast<float>(definition.durationTicks) * 0.5f))) : std::max(1, definition.durationTicks / 3))
                    : (eliteExile ? std::max(1, static_cast<int>(std::ceil(static_cast<float>(definition.durationTicks) * 1.25f))) : definition.durationTicks);
                targetEnemy->banishedTicks = banishDuration;
                targetEnemy->temporalDelayTicks = std::max(targetEnemy->temporalDelayTicks, targetEnemy->banishedTicks);
                targetEnemy->banishReturnArmed = hasEquippedSkillNode("banish_return");
                targetEnemy->banishReturnPosition = {std::max(92.0f, targetEnemy->pos.x - 110.0f), pathY(std::max(92.0f, targetEnemy->pos.x - 110.0f), targetEnemy->id)};
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            addVoidInstability(hasEquippedSkillNode("banish_return") ? 10 : 12);
            executed = true;
        } else if (operation == "phase_exchange") {
            Enemy* first = nullptr; Enemy* second = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (first == nullptr || enemy.id < first->id) { second = first; first = &enemy; } else if (second == nullptr || enemy.id < second->id) second = &enemy;
            }
            if (first != nullptr && second != nullptr) {
                std::swap(first->pos, second->pos);
                if (hasEquippedSkillNode("exchange_capstone")) {
                    first->slow = std::max(first->slow, 0.55f);
                    second->slow = std::max(second->slow, 0.55f);
                    first->spatialCooldownTicks = std::max(first->spatialCooldownTicks, TickRate / 2);
                    second->spatialCooldownTicks = std::max(second->spatialCooldownTicks, TickRate / 2);
                }
            }
            addVoidInstability(hasEquippedSkillNode("exchange_safe") ? 4 : 8);
            executed = true;
        } else if (operation == "event_horizon") {
            if (zones.size() < content.maxSkillZones) {
                const float crackScale = hasEquippedSkillNode("horizon_crack") ? (1.0f + std::min(0.20f, static_cast<float>(economyState->resources.instability) * 0.002f)) : 1.0f;
                zones.push_back({nextZoneId++, request.target.world, radius * crackScale, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                addVoidInstability(8);
                if (hasEquippedSkillNode("horizon_capstone")) emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, request.target.world, radius, definition.durationTicks, "event_horizon");
                executed = true;
            }
        } else if (operation == "intercept") {
            const float guardHealth = valueA * (hasEquippedSkillNode("intercept_guard") ? 1.35f : 1.0f);
            const std::size_t beforeAllies = alliedUnitsList.size();
            spawnAlliedUnit(request.target.world, request.skill, "bulwark", definition.durationTicks, guardHealth, valueB, 42.0f);
            if (alliedUnitsList.size() == beforeAllies) return false;
            ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
            startOathVow(1, definition.durationTicks);
            executed = true;
        } else if (operation == "challenge") {
            bool challenged = false;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.challengeTicks = definition.durationTicks;
                enemy.vulnerability = std::max(enemy.vulnerability, valueA);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                if (hasEquippedSkillNode("challenge_elite") && enemy.boss) enemy.vulnerability = std::max(enemy.vulnerability, valueA * 1.25f);
                if (hasEquippedSkillNode("challenge_capstone") && enemy.boss) { enemy.attackCooldownTicks = std::max(enemy.attackCooldownTicks, TickRate); enemy.telegraphTicks = 0; }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                challenged = true;
                break;
            }
            if (!challenged) return false;
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + 6);
            startOathVow(2, definition.durationTicks);
            executed = true;
        } else if (operation == "sanctuary") {
            const float heal = valueA * (hasEquippedSkillNode("sanctuary_heal") ? 1.35f : 1.0f);
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) { unit.hp = std::min(unit.maxHp, unit.hp + heal); unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks); unit.damageScale = std::max(unit.damageScale, 1.1f); if (hasEquippedSkillNode("sanctuary_shield")) unit.damageReduction = std::max(unit.damageReduction, 0.35f); ++counters.skillTargets[static_cast<std::size_t>(request.skill)]; }
            if (zones.size() >= content.maxSkillZones) return false;
            zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, heal, valueB, request.skill, false, true});
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + 4 + (skillLoadoutState.doctrineId == "oathkeeper_sanctuary" ? 4 : 0));
            startOathVow(0, definition.durationTicks);
            executed = true;
        } else if (operation == "judgment") {
            const int spend = std::min(economyState->resources.resolve, std::max(1, static_cast<int>(std::round(valueB))));
            economyState->resources.resolve -= spend;
            const int before = counters.damageDealt;
            const float damage = valueA + static_cast<float>(spend) * (skillLoadoutState.doctrineId == "oathkeeper_judgment" ? 3.6f : 3.0f);
            if (hasEquippedSkillNode("judgment_single")) {
                Enemy* target = nullptr;
                for (Enemy& enemy : enemyList) if (enemy.alive && enemy.challengeTicks > 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (target == nullptr || enemy.hp > target->hp || (enemy.hp == target->hp && enemy.id < target->id))) target = &enemy;
                if (target != nullptr) applySkillDamage(*target, damage * 1.55f, request.skill);
                else damageArea(request.target.world, radius, damage, false);
            } else damageArea(request.target.world, radius, damage, false);
            if (hasEquippedSkillNode("judgment_capstone") && spend >= 8) economyState->guardianWardTicks = std::max(economyState->guardianWardTicks, TickRate);
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
            executed = true;
        } else if (operation == "misfortune") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) { enemy.slow = std::max(enemy.slow, valueA); enemy.vulnerability = std::max(enemy.vulnerability, valueB); enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks); ++counters.skillTargets[static_cast<std::size_t>(request.skill)]; }
            if (drawFateEvent() == 0) ++economyState->fateUnfavorableBank;
            if (hasEquippedSkillNode("misfortune_capstone")) {
                economyState->fateDoomedOutcomeReady = 1;
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, request.target.world, radius, TickRate, "doomed_event");
            }
            economyState->resources.fate = std::min(100, economyState->resources.fate + (hasEquippedSkillNode("misfortune_resource") ? 8 : 5));
            executed = true;
        } else if (operation == "lucky_shot") {
            const int before = counters.damageDealt;
            const int event = drawFateEvent();
            const bool doomed = economyState->fateDoomedOutcomeReady != 0;
            const bool highRoll = hasEquippedSkillNode("lucky_high");
            const bool safeBet = hasEquippedSkillNode("lucky_safe") && !highRoll;
            const float eventScale = (event <= 0 ? (safeBet ? 0.88f : 0.70f) : (event == 1 ? 1.0f : (event == 2 ? (highRoll ? 1.48f : 1.35f) : (safeBet ? 1.55f : (highRoll ? 1.95f : 1.75f))))) * (doomed ? 0.65f : 1.0f);
            damageArea(request.target.world, radius, valueA * eventScale * (economyState->fateBoostTicks > 0 ? 1.5f : 1.0f), false);
            if (doomed) {
                economyState->fateDoomedOutcomeReady = 0;
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, "doomed_outcome");
            }
            if (event >= 2 && hasEquippedSkillNode("lucky_capstone")) {
                skillCooldowns[request.slot] = std::max(1, skillCooldowns[request.slot] / 2);
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, "lucky_break");
            }
            if (event == 0) ++economyState->fateUnfavorableBank;
            else if (event >= 2 && economyState->fateUnfavorableBank > 0) --economyState->fateUnfavorableBank;
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
            economyState->resources.fate = std::min(100, economyState->resources.fate + 4);
            executed = true;
        } else if (operation == "stack_deck") {
            if (economyState->fateRewriteReady != 0 && economyState->fateQueueSize > 1) {
                const int first = economyState->fateQueue[0];
                for (int index = 1; index < economyState->fateQueueSize; ++index) economyState->fateQueue[static_cast<std::size_t>(index - 1)] = economyState->fateQueue[static_cast<std::size_t>(index)];
                economyState->fateQueue[static_cast<std::size_t>(economyState->fateQueueSize - 1)] = first;
                economyState->fateRewriteReady = 0;
            } else {
                for (int index = 1; index < economyState->fateQueueSize; ++index) if (economyState->fateQueue[static_cast<std::size_t>(index)] >= 2) {
                    std::swap(economyState->fateQueue[0], economyState->fateQueue[static_cast<std::size_t>(index)]);
                    break;
                }
            }
            if (hasEquippedSkillNode("stack_bank") && economyState->fateQueueSize > 1) {
                // Stack the Deck normally improves the next event.  Fate Bank
                // reserves one additional favorable event immediately behind
                // it, without consuming or previewing either event.  If the
                // existing deterministic queue has no favorable result in its
                // remaining window, the talent converts that one bounded slot
                // rather than drawing a new outcome or changing the stream.
                if (economyState->fateQueue[1] < 2) {
                    int favorableIndex = -1;
                    for (int index = 2; index < economyState->fateQueueSize; ++index) {
                        if (economyState->fateQueue[static_cast<std::size_t>(index)] >= 2) { favorableIndex = index; break; }
                    }
                    if (favorableIndex >= 0) std::swap(economyState->fateQueue[1], economyState->fateQueue[static_cast<std::size_t>(favorableIndex)]);
                    else economyState->fateQueue[1] = 2;
                }
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, request.target.world, radius, TickRate, "fate_bank");
            }
            economyState->fateBoostTicks = std::max(economyState->fateBoostTicks, definition.durationTicks);
            if (hasEquippedSkillNode("stack_capstone") && economyState->fateQueueSize > 0) {
                economyState->fatePreviewEvent = economyState->fateQueue[0];
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, request.target.world, radius, TickRate * 2, "house_preview");
            }
            economyState->resources.fate = std::min(100, economyState->resources.fate + 8);
            executed = true;
        } else if (operation == "double_nothing") {
            if (economyState->resources.fate >= static_cast<int>(std::round(valueB))) {
                economyState->resources.fate -= static_cast<int>(std::round(valueB));
                const int event = drawFateEvent();
                const bool houseWins = skillLoadoutState.doctrineId == "fatebinder_house" && economyState->fateUnfavorableBank >= 3;
                if (event == 0 && !houseWins) ++economyState->fateUnfavorableBank;
                if (event == 0 && !houseWins && hasEquippedSkillNode("double_capstone") && economyState->fateQueueSize > 1) {
                    economyState->fateQueue[1] = std::max(2, economyState->fateQueue[1]);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, 24.0f, TickRate, "house_bank");
                }
                if (event >= 2 || houseWins) {
                    if (houseWins) economyState->fateUnfavorableBank = 0;
                    std::array<std::size_t, SkillSlotCount> candidates{};
                    std::size_t candidateCount = 0;
                    for (std::size_t index = 0; index < SkillSlotCount; ++index) if (index != request.slot && skillCooldowns[index] > 0) candidates[candidateCount++] = index;
                    std::stable_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(candidateCount), [this](std::size_t left, std::size_t right) {
                        if (hasEquippedSkillNode("double_refresh") && skillCooldowns[left] != skillCooldowns[right]) return skillCooldowns[left] > skillCooldowns[right];
                        return left < right;
                    });
                    const std::size_t refreshCount = hasEquippedSkillNode("double_risk") ? std::min<std::size_t>(2, candidateCount) : std::min<std::size_t>(1, candidateCount);
                    for (std::size_t index = 0; index < refreshCount; ++index) skillCooldowns[candidates[index]] = 0;
                    if (refreshCount > 0 && hasEquippedSkillNode("double_refresh")) emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, "credit_wager");
                    if (refreshCount > 1 && hasEquippedSkillNode("double_risk")) emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, "all_in");
                }
                executed = true;
            }
        } else if (operation == "pounce") {
            AlliedUnit* beast = nullptr;
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast" && (beast == nullptr || unit.id < beast->id)) beast = &unit;
            if (beast != nullptr) {
                beast->pos = request.target.world;
                beast->attackCooldownTicks = 0;
                const int before = counters.damageDealt;
                const bool empowered = economyState->beastPounceEmpoweredTicks > 0;
                damageArea(request.target.world, radius, beast->damage * beast->damageScale * valueA * (empowered ? 1.60f : 1.0f), false);
                if (empowered) economyState->beastPounceEmpoweredTicks = 0;
                counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, request.target.world, radius, definition.durationTicks, empowered ? "alpha_feast_pounce" : "pounce");
                executed = true;
            }
        } else if (operation == "feed_beast") {
            AlliedUnit* beast = nullptr;
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast" && (beast == nullptr || unit.id < beast->id)) beast = &unit;
            BattlefieldRemain* meal = nullptr;
            // Salvager drones reserve a remain before travelling to it.  A
            // Beastmaster command must not steal that reservation between
            // simulation ticks; the drone either delivers it or releases it
            // through the normal expiry/consumption path.
            for (BattlefieldRemain& remain : economyState->remains) if (!remain.consumed && remain.claimedByDrone == 0 && distanceSquared(remain.pos, request.target.world) <= radius * radius && (meal == nullptr || remain.id < meal->id)) meal = &remain;
            if (beast != nullptr && meal != nullptr) {
                meal->consumed = true;
                beast->hp = std::min(beast->maxHp, beast->hp + valueA);
                economyState->resources.bond = std::min(100, economyState->resources.bond + static_cast<int>(std::round(valueB)));
                // Trophy Feast treats remains from tougher enemy types as a
                // valuable meal.  The reward is deliberately a temporary
                // basic-attack growth effect; it must not multiply Pounce's
                // command payload or create a permanent max-health exploit.
                const bool trophyMeal = meal->source != EnemyType::Grunt && meal->source != EnemyType::Runner && meal->source != EnemyType::Swarmling;
                if (hasEquippedSkillNode("feed_trophy") && trophyMeal) {
                    beast->damageScale = std::max(beast->damageScale, 1.15f);
                    beast->buffTicks = std::max(beast->buffTicks, TickRate * 8);
                    economyState->resources.bond = std::min(100, economyState->resources.bond + 4);
                }
                if (hasEquippedSkillNode("feed_capstone") && meal->value > 0) economyState->beastPounceEmpoweredTicks = std::max(economyState->beastPounceEmpoweredTicks, TickRate * 8);
                if (hasEquippedSkillNode("feed_cleanse")) beast->injuryTicks = 0;
                ++counters.skillHealing[static_cast<std::size_t>(request.skill)];
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
            if (beast != nullptr && !executed) {
                // A Feed command is still a valid, cooldown-consuming command
                // when no corpse is in range; automatic corpse processing can
                // make the next cast meaningful without locking the skill bar.
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "adapt_beast") {
            AlliedUnit* beast = nullptr;
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast" && (beast == nullptr || unit.id < beast->id)) beast = &unit;
            if (beast != nullptr) {
                int adaptation = 1;
                if (hasEquippedSkillNode("adapt_armor")) adaptation = 1;
                else if (hasEquippedSkillNode("adapt_lightning")) adaptation = 2;
                else if (hasEquippedSkillNode("adapt_regen")) adaptation = 3;
                else if (hasEquippedSkillNode("adapt_burrow")) adaptation = 4;
                else if (hasEquippedSkillNode("adapt_spiked")) adaptation = 5;
                else if (skillLoadoutState.doctrineId == "beastmaster_adaptation") {
                    int armored = 0; int fast = 0; int ranged = 0;
                    for (const Enemy& enemy : enemyList) if (enemy.alive) {
                        if (enemy.type == EnemyType::Tank || enemy.type == EnemyType::Shielded) ++armored;
                        else if (enemy.type == EnemyType::Runner || enemy.type == EnemyType::Swarmling) ++fast;
                        else if (enemy.type == EnemyType::Teleporter || enemy.type == EnemyType::Boss) ++ranged;
                    }
                    adaptation = armored >= fast && armored >= ranged ? 1 : (fast >= ranged ? 4 : 2);
                } else adaptation = 1 + ((wave + beast->id) % 5);
                const bool repeatedTrait = hasEquippedSkillNode("adapt_capstone") && economyState->beastAdaptation == adaptation && (economyState->beastAdaptationTicks > 0 || economyState->beastAdaptationPersistent);
                economyState->beastAdaptationStreak = repeatedTrait ? economyState->beastAdaptationStreak + 1 : 1;
                economyState->beastAdaptationPersistent = repeatedTrait && economyState->beastAdaptationStreak >= 2;
                economyState->beastAdaptation = adaptation;
                economyState->beastTraitMask |= (1u << static_cast<unsigned int>(std::clamp(adaptation - 1, 0, 4)));
                economyState->beastAdaptationTicks = economyState->beastAdaptationPersistent ? 0 : (hasEquippedSkillNode("adapt_capstone") ? std::max(definition.durationTicks, TickRate * 12) : definition.durationTicks);
                beast->buffTicks = std::max(beast->buffTicks, definition.durationTicks);
                if (hasEquippedSkillNode("adapt_armor")) beast->damageReduction = std::max(beast->damageReduction, 0.28f);
                if (adaptation == 2 || adaptation == 5) beast->damageScale = std::max(beast->damageScale, 1.25f);
                if (adaptation == 4) beast->speedScale = std::max(beast->speedScale, 1.35f);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "pack_call") {
            AlliedUnit* beast = nullptr;
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast" && (beast == nullptr || unit.id < beast->id)) beast = &unit;
            if (beast == nullptr && hasEquippedSkillNode("pack_capstone")) {
                for (AlliedUnit& unit : alliedUnitsList) if (!unit.alive && unit.role == "beast" && unit.downedTicks > 0 && (beast == nullptr || unit.id < beast->id)) beast = &unit;
                if (beast != nullptr) {
                    beast->alive = true;
                    beast->downedTicks = 0;
                    beast->hp = std::max(1.0f, beast->maxHp * 0.35f);
                    beast->injuryTicks = 0;
                    beast->pos = request.target.world;
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, beast->pos, beast->radius, TickRate, "pack_rescue");
                }
            }
            if (beast != nullptr) {
                beast->pos = request.target.world;
                for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) {
                    unit.damageScale = std::max(unit.damageScale, valueA);
                    unit.speedScale = std::max(unit.speedScale, valueB);
                    unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks);
                    if (hasEquippedSkillNode("pack_defensive")) unit.damageReduction = std::max(unit.damageReduction, 0.18f);
                    if (hasEquippedSkillNode("pack_hunting") && distanceSquared(unit.pos, beast->pos) <= 110.0f * 110.0f) unit.damageScale = std::max(unit.damageScale, valueA * 1.12f);
                }
                if (hasEquippedSkillNode("pack_capstone")) economyState->beastPackTakedownReady = 1;
                economyState->resources.bond = std::min(100, economyState->resources.bond + 8);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "hunt_command") {
            Enemy* targetEnemy = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (targetEnemy == nullptr || enemy.boss || enemy.hp > targetEnemy->hp || (enemy.hp == targetEnemy->hp && enemy.id < targetEnemy->id))) targetEnemy = &enemy;
            if (targetEnemy != nullptr) {
                economyState->beastCommandTargetId = targetEnemy->id;
                economyState->beastCommandTicks = definition.durationTicks;
                if (hasEquippedSkillNode("hunt_isolate")) {
                    bool isolated = true;
                    for (const Enemy& nearby : enemyList) if (nearby.alive && nearby.id != targetEnemy->id && distanceSquared(nearby.pos, targetEnemy->pos) <= 110.0f * 110.0f) { isolated = false; break; }
                    if (isolated) {
                        targetEnemy->vulnerability = std::max(targetEnemy->vulnerability, 0.18f);
                        targetEnemy->vulnerabilityTicks = std::max(targetEnemy->vulnerabilityTicks, definition.durationTicks);
                        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, targetEnemy->pos, targetEnemy->radius + 10.0f, TickRate, "lone_prey");
                    }
                }
                for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast") {
                    unit.damageScale = std::max(unit.damageScale, valueA);
                    unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks);
                    if (hasEquippedSkillNode("hunt_guard")) unit.damageReduction = std::max(unit.damageReduction, 0.22f);
                }
                if (hasEquippedSkillNode("hunt_capstone")) economyState->beastHuntPinReady = 1;
                economyState->resources.bond = std::min(100, economyState->resources.bond + 6);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
            if (targetEnemy == nullptr) executed = true;
        } else if (operation == "delayed_damage") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, definition.durationTicks, valueA, valueB, request.skill, false, true});
                SkillZone& barrage = zones.back();
                Enemy* predictedTarget = nullptr;
                float nearest = radius * radius;
                for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= nearest && (predictedTarget == nullptr || enemy.id < predictedTarget->id)) predictedTarget = &enemy;
                if (predictedTarget != nullptr) {
                    barrage.predictedEnemyId = predictedTarget->id;
                    if (predictedTarget->predictedTicks > 0) barrage.predictedPosition = predictedTarget->predictedPosition;
                    else {
                        const float projectedX = std::min(static_cast<float>(Width - 40), predictedTarget->pos.x + predictedTarget->speed * static_cast<float>(definition.durationTicks) / static_cast<float>(TickRate));
                        barrage.predictedPosition = {projectedX, pathY(projectedX, predictedTarget->id)};
                    }
                } else barrage.predictedPosition = request.target.world;
                if (economyState->artilleristFireSolutionReady != 0 && predictedTarget != nullptr) {
                    barrage.predictedPosition = predictedTarget->pos;
                    economyState->artilleristFireSolutionReady = 0;
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, barrage.predictedPosition, radius * 0.72f, TickRate * 2, "fire_solution");
                }
            }
            economyState->resources.targetingData = std::min(100, economyState->resources.targetingData + 8);
            executed = true;
        } else if (operation == "displace") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                SkillZone& gate = zones.back();
                const float exitX = std::clamp(request.target.world.x + valueA, 92.0f, static_cast<float>(Width - 40));
                gate.secondaryCenter = {exitX, pathY(exitX, gate.id)};
            }
            addVoidInstability(10);
            executed = true;
        } else if (operation == "ward") {
            if (zones.size() >= content.maxSkillZones) return false;
            zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
            economyState->guardianWardTicks = std::max(economyState->guardianWardTicks, definition.durationTicks);
            if (hasEquippedSkillNode("ward_bulwark")) {
                AlliedUnit* protectedTarget = nullptr;
                for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius &&
                    (protectedTarget == nullptr || distanceSquared(unit.pos, request.target.world) < distanceSquared(protectedTarget->pos, request.target.world) ||
                     (distanceSquared(unit.pos, request.target.world) == distanceSquared(protectedTarget->pos, request.target.world) && unit.id < protectedTarget->id))) protectedTarget = &unit;
                if (protectedTarget != nullptr) {
                    protectedTarget->hp = std::min(protectedTarget->maxHp, protectedTarget->hp + valueA * 1.5f);
                    protectedTarget->damageReduction = std::max(protectedTarget->damageReduction, 0.50f);
                    protectedTarget->buffTicks = std::max(protectedTarget->buffTicks, definition.durationTicks);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, protectedTarget->pos, protectedTarget->radius + 12.0f, definition.durationTicks, "single_bulwark");
                }
            }
            startOathVow(0, definition.durationTicks);
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + (hasEquippedSkillNode("ward_capstone") ? 15 : 10));
            executed = true;
        } else if (operation == "fate_boost") {
            for (int index = 1; index < economyState->fateQueueSize; ++index) if (economyState->fateQueue[static_cast<std::size_t>(index)] >= 2) {
                std::swap(economyState->fateQueue[0], economyState->fateQueue[static_cast<std::size_t>(index)]);
                break;
            }
            economyState->resources.fate = std::min(100, economyState->resources.fate + (hasEquippedSkillNode("fate_capstone") ? 26 : 18));
            economyState->fateBoostTicks = std::max(economyState->fateBoostTicks, definition.durationTicks);
            executed = true;
        } else if (operation == "treason_mark") {
            Enemy* targetEnemy = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && !enemy.boss && enemy.allegiance == 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (targetEnemy == nullptr || enemy.hp > targetEnemy->hp || (enemy.hp == targetEnemy->hp && enemy.id < targetEnemy->id))) targetEnemy = &enemy;
            if (targetEnemy != nullptr) {
                targetEnemy->allegiance = 1;
                targetEnemy->allegianceTicks = definition.durationTicks;
                targetEnemy->usurperInheritedMark = false;
                targetEnemy->usurperTreasonMark = true;
                targetEnemy->vulnerability = std::max(targetEnemy->vulnerability, valueA);
                targetEnemy->vulnerabilityTicks = std::max(targetEnemy->vulnerabilityTicks, definition.durationTicks);
                if (hasEquippedSkillNode("treason_scapegoat")) {
                    const std::size_t typeIndex = static_cast<std::size_t>(targetEnemy->type);
                    targetEnemy->damageResistance = std::min(0.75f, content.enemyDamageResistance[typeIndex] + 0.15f + (hasEquippedSkillNode("treason_scapegoat_mastery") ? 0.06f : 0.0f));
                }
                if (skillLoadoutState.doctrineId == "usurper_collapse") {
                    int nearbyMarked = 0;
                    for (const Enemy& nearby : enemyList) if (nearby.alive && nearby.allegiance == 1 && nearby.id != targetEnemy->id && distanceSquared(nearby.pos, targetEnemy->pos) <= 120.0f * 120.0f) ++nearbyMarked;
                    if (nearbyMarked > 0) targetEnemy->vulnerability = std::max(targetEnemy->vulnerability, valueA + 0.08f * static_cast<float>(std::min(3, nearbyMarked)));
                }
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "riot_whisper") {
            int affected = 0;
            const int controlDuration = (skillLoadoutState.doctrineId == "usurper_puppeteer" ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.25f)) : definition.durationTicks) +
                (hasEquippedSkillNode("riot_link_mastery") ? TickRate * 2 : 0);
            const int riotTargetLimit = hasEquippedSkillNode("riot_link") ? 6 : 2;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                if (affected >= riotTargetLimit) break;
                enemy.confusionTicks = std::max(enemy.confusionTicks, controlDuration);
                if (!enemy.boss) enemy.allegiance = (affected % 2 == 0) ? 1 : 0;
                enemy.allegianceTicks = std::max(enemy.allegianceTicks, controlDuration);
                ++affected;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            if (affected > 0) economyState->resources.discord = std::min(100, economyState->resources.discord + affected * 2);
            if (affected > 0 && hasEquippedSkillNode("riot_capstone")) economyState->usurperCivilWarReady = 1;
            executed = true;
        } else if (operation == "blood_field") {
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
                economyState->bloodDebt = std::min(100, economyState->bloodDebt + 2);
                executed = true;
            }
        } else if (operation == "blood_golem") {
            const std::size_t before = alliedUnitsList.size();
            spawnAlliedUnit(request.target.world, request.skill, "blood_golem", definition.durationTicks, valueA, valueB, 40.0f);
            if (alliedUnitsList.size() > before) {
                AlliedUnit& golem = alliedUnitsList.back();
                if (hasEquippedSkillNode("golem_guardian")) {
                    golem.maxHp *= 1.20f;
                    golem.hp = golem.maxHp;
                    golem.damageReduction = 0.28f;
                }
                if (hasEquippedSkillNode("golem_reaper")) golem.damage *= 1.20f;
                economyState->bloodDebt = std::min(100, economyState->bloodDebt + 3);
                ++counters.skillSummons[static_cast<std::size_t>(request.skill)];
                executed = true;
            }
        } else if (operation == "last_pulse") {
            const int before = counters.damageDealt;
            const int debtBefore = economyState->bloodDebt;
            const bool debtCascade = hasEquippedSkillNode("pulse_debt");
            const float debtScale = 1.0f + static_cast<float>(debtBefore) * (debtCascade ? 0.035f : 0.025f);
            damageArea(request.target.world, radius, valueA * debtScale, false);
            const int dealt = std::max(0, counters.damageDealt - before);
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += dealt;
            if (dealt > 0 && economyState->bloodEclipseTicks > 0) economyState->bloodEclipseHealth = std::min(50, economyState->bloodEclipseHealth + std::max(1, dealt / 20));
            if (debtBefore >= 12 && hasEquippedSkillNode("pulse_capstone")) {
                economyState->bloodPulseEmpowerTicks = std::max(economyState->bloodPulseEmpowerTicks, TickRate * 4);
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate * 4, "red_eclipse");
            }
            economyState->bloodDebt = std::max(0, economyState->bloodDebt - (debtCascade ? 12 : 8));
            executed = true;
        } else if (operation == "puppet_thread") {
            Enemy* targetEnemy = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && !enemy.boss && enemy.allegiance == 0 && distanceSquared(enemy.pos, request.target.world) <= radius * radius && (targetEnemy == nullptr || enemy.hp > targetEnemy->hp || (enemy.hp == targetEnemy->hp && enemy.id < targetEnemy->id))) targetEnemy = &enemy;
            if (targetEnemy != nullptr) {
                const int controlDuration = skillLoadoutState.doctrineId == "usurper_puppeteer" ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.25f)) : definition.durationTicks;
                targetEnemy->allegiance = 1;
                targetEnemy->allegianceTicks = controlDuration;
                targetEnemy->vulnerability = std::max(targetEnemy->vulnerability, valueA);
                targetEnemy->vulnerabilityTicks = std::max(targetEnemy->vulnerabilityTicks, controlDuration);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
        } else if (operation == "false_orders") {
            int affected = 0;
            const int controlDuration = skillLoadoutState.doctrineId == "usurper_puppeteer" ? static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.25f)) : definition.durationTicks;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.confusionTicks = std::max(enemy.confusionTicks, controlDuration);
                if (enemy.boss) {
                    const bool strongerFallback = hasEquippedSkillNode("orders_exposure");
                    enemy.vulnerability = std::max(enemy.vulnerability, valueA * (strongerFallback ? 1.28f : 1.0f));
                    enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, strongerFallback ? static_cast<int>(std::round(static_cast<float>(controlDuration) * 0.38f)) : controlDuration / 2);
                } else {
                    enemy.allegiance = 1;
                    enemy.allegianceTicks = std::max(enemy.allegianceTicks, controlDuration);
                }
                ++affected;
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            economyState->resources.discord = std::min(100, economyState->resources.discord + affected * 2);
            if (affected >= 2 && hasEquippedSkillNode("orders_capstone")) {
                economyState->resources.discord = std::min(100, economyState->resources.discord + 6);
                economyState->usurperRiotReady = 0;
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Expire, request.target.world, radius, TickRate, "coup_reward");
            }
            executed = true;
        } else if (operation == "shared_agony") {
            if (zones.size() < content.maxSkillZones) {
                const bool chainBranch = hasEquippedSkillNode("agony_chain");
                const bool focusBranch = hasEquippedSkillNode("agony_focus");
                const float linkRadius = radius * (chainBranch ? 1.25f : (focusBranch ? 0.72f : 1.0f));
                const float echoScale = valueA * (chainBranch ? 0.80f : (focusBranch ? 1.35f : 1.0f));
                zones.push_back({nextZoneId++, request.target.world, linkRadius, definition.durationTicks, 0, echoScale, valueB, request.skill, false, true});
                // A new cast replaces the previous link rather than creating
                // an unbounded web of overlapping echo groups.
                for (Enemy& enemy : enemyList) enemy.sharedAgonyTicks = 0;
                for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= linkRadius * linkRadius) enemy.sharedAgonyTicks = std::max(enemy.sharedAgonyTicks, definition.durationTicks);
                economyState->resources.discord = std::min(100, economyState->resources.discord + 3);
                executed = true;
            }
        } else if (operation == "blood_strike") {
            Enemy* markedElite = nullptr;
            float markedEliteHealth = 0.0f;
            if (hasEquippedSkillNode("lance_capstone")) {
                for (Enemy& enemy : enemyList) if (enemy.alive && enemy.vulnerabilityTicks > 0 && (enemy.boss || enemy.maxHp >= 180.0f) && distanceSquared(enemy.pos, request.target.world) <= radius * radius &&
                    (markedElite == nullptr || enemy.maxHp > markedElite->maxHp || (enemy.maxHp == markedElite->maxHp && enemy.id < markedElite->id))) markedElite = &enemy;
                if (markedElite != nullptr) markedEliteHealth = markedElite->hp;
            }
            const int before = counters.damageDealt;
            damageArea(request.target.world, radius, valueA * (hasSkillGroup("bloodbinder") ? 1.0f + static_cast<float>(economyState->bloodDebt) * 0.01f : 1.0f), false);
            const int dealt = std::max(0, counters.damageDealt - before);
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += dealt;
            if (hasEquippedSkillNode("lance_control")) {
                for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) enemy.slow = std::max(enemy.slow, 0.55f);
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, definition.durationTicks, "crimson_zone");
            }
            if (dealt > 0 && economyState->bloodEclipseTicks > 0) economyState->bloodEclipseHealth = std::min(50, economyState->bloodEclipseHealth + std::max(1, dealt / 20));
            if (markedElite != nullptr && markedElite->hp < markedEliteHealth) {
                lives = std::min(maxLives, lives + 1);
                emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, markedElite->pos, radius * 0.55f, TickRate, "heartsplitter");
            }
            executed = true;
        } else if (operation == "life_siphon") {
            const bool harvest = hasEquippedSkillNode("siphon_harvest");
            const bool tithe = hasEquippedSkillNode("siphon_tithe");
            std::vector<int> eligible;
            for (std::size_t index = 0; index < alliedUnitsList.size(); ++index) {
                const AlliedUnit& unit = alliedUnitsList[index];
                if (unit.alive && distanceSquared(unit.pos, request.target.world) <= radius * radius) eligible.push_back(static_cast<int>(index));
            }
            std::stable_sort(eligible.begin(), eligible.end(), [&](int left, int right) {
                const AlliedUnit& a = alliedUnitsList[static_cast<std::size_t>(left)];
                const AlliedUnit& b = alliedUnitsList[static_cast<std::size_t>(right)];
                return a.id < b.id;
            });
            if (harvest || tithe) {
                const int conversionCap = harvest && hasEquippedSkillNode("siphon_harvest_mastery") ? 10 : (tithe && hasEquippedSkillNode("siphon_tithe_mastery") ? 8 : 6);
                int convertedHealth = 0;
                int affected = 0;
                for (const int index : eligible) {
                    AlliedUnit& unit = alliedUnitsList[static_cast<std::size_t>(index)];
                    if (!unit.alive) continue;
                    const float beforeHealth = unit.hp;
                    if (harvest) {
                        unit.alive = false;
                        unit.hp = 0.0f;
                    } else {
                        const float drainRatio = hasEquippedSkillNode("siphon_tithe_mastery") ? 0.45f : 0.35f;
                        unit.hp = std::max(1.0f, unit.hp * (1.0f - drainRatio));
                        if (hasEquippedSkillNode("siphon_tithe_capstone")) {
                            unit.damageScale = std::max(unit.damageScale, 1.30f);
                            unit.buffTicks = std::max(unit.buffTicks, TickRate * 5);
                        }
                    }
                    convertedHealth += std::max(1, static_cast<int>(std::round((harvest ? beforeHealth : beforeHealth - unit.hp) / 40.0f * valueA / 3.0f)));
                    ++affected;
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                }
                if (affected > 0) {
                    const int healing = std::min(conversionCap, convertedHealth);
                    const int actualHealing = std::min(std::max(0, maxLives - lives), healing);
                    lives += actualHealing;
                    counters.skillHealing[static_cast<std::size_t>(request.skill)] += actualHealing;
                    if (harvest && hasEquippedSkillNode("siphon_harvest_capstone")) economyState->bloodHarvestShield = std::min(6, economyState->bloodHarvestShield + std::max(0, healing - actualHealing));
                    const int debtReduction = static_cast<int>(std::round(valueB)) + (tithe && hasEquippedSkillNode("siphon_tithe_mastery") ? 1 : 0);
                    economyState->bloodDebt = std::max(0, economyState->bloodDebt - debtReduction);
                    const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                    if (identity.primaryGroup == "bloodbinder" && identity.primaryCount >= 5) economyState->bloodHeartFragments = std::min(3, economyState->bloodHeartFragments + 1);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, request.target.world, radius, TickRate, harvest ? "harvest" : "tithe");
                    executed = true;
                }
            } else {
                AlliedUnit* sacrifice = nullptr;
                for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && (sacrifice == nullptr || unit.hp < sacrifice->hp || (unit.hp == sacrifice->hp && unit.id < sacrifice->id))) sacrifice = &unit;
                if (sacrifice != nullptr) {
                    sacrifice->alive = false;
                    sacrifice->hp = 0.0f;
                    lives = std::min(maxLives, lives + static_cast<int>(std::round(valueA)));
                    const bool eliteSummon = sacrifice->role == "blood_golem" || sacrifice->maxHp >= 100.0f;
                    const int debtReduction = static_cast<int>(std::round(valueB)) + (eliteSummon && hasEquippedSkillNode("siphon_debt") ? (hasEquippedSkillNode("siphon_debt_mastery") ? 4 : 2) : 0);
                    economyState->bloodDebt = std::max(0, economyState->bloodDebt - debtReduction);
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                    counters.skillHealing[static_cast<std::size_t>(request.skill)] += static_cast<int>(std::round(valueA));
                    const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                    if (identity.primaryGroup == "bloodbinder" && identity.primaryCount >= 5) economyState->bloodHeartFragments = std::min(3, economyState->bloodHeartFragments + 1);
                    if (hasEquippedSkillNode("siphon_capstone")) economyState->bloodReservoirReady = 1;
                    if (eliteSummon && hasEquippedSkillNode("siphon_debt_mastery")) emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, sacrifice->pos, sacrifice->radius + 8.0f, TickRate, "absolution_mastery");
                    executed = true;
                }
            }
            // The legacy single-sacrifice contract keeps its historical no-op
            // behavior for compatibility. New area branches must have a real
            // eligible summon so an empty target cannot consume cooldown or
            // fabricate a recovery transaction.
            if (!executed && !harvest && !tithe) executed = true;
        }
    }
    return executed;
}

bool GameSim::activateSkill(std::size_t slot, const TargetSpec& target, std::string* error) {
    skillError.clear();
    if (gameOver || victory || upgradeChoicePending) { skillError = "SKILL UNAVAILABLE"; ++counters.failedSkillCasts; if (error) *error = skillError; return false; }
    if (!skillLoadoutSatisfiesRules(&skillError)) { ++counters.failedSkillCasts; if (error) *error = skillError; return false; }
    if (!validateSkillTarget(slot, target, &skillError)) { ++counters.failedSkillCasts; if (error) *error = skillError; return false; }
    TargetSpec quantized = target;
    quantized.world.x = std::round(quantized.world.x * 10.0f) / 10.0f;
    quantized.world.y = std::round(quantized.world.y * 10.0f) / 10.0f;
    quantized.direction.x = std::round(quantized.direction.x * 1000.0f) / 1000.0f;
    quantized.direction.y = std::round(quantized.direction.y * 1000.0f) / 1000.0f;
    SkillCastRequest request;
    request.sequence = nextSkillCastSequence++;
    request.tick = static_cast<std::uint32_t>(tickCount);
    request.slot = static_cast<std::uint8_t>(slot);
    request.skill = skillLoadoutState.skills[slot];
    request.target = quantized;
    if (!castSkill(request, &skillError)) { ++counters.failedSkillCasts; if (error) *error = skillError; return false; }
    ++counters.skillCasts;
    if (error) error->clear();
    return true;
}

void GameSim::emitSkillVisualEvent(SkillId skill, SkillVisualPhase phase, Vec2 position, float radius, int duration, const std::string& branch) {
    if (skillVisualEventsList.size() >= 64u) skillVisualEventsList.erase(skillVisualEventsList.begin());
    skillVisualEventsList.push_back({nextSkillVisualEventId++, skill, phase, position, radius, std::max(1, duration), branch});
}

void GameSim::updateSkillVisualEvents() {
    std::vector<SkillVisualEvent> expired;
    for (SkillVisualEvent& event : skillVisualEventsList) {
        if (event.remainingTicks == 1 && event.phase == SkillVisualPhase::Cast) expired.push_back({0, event.skill, SkillVisualPhase::Expire, event.position, event.radius, 6, event.branchId});
        --event.remainingTicks;
    }
    skillVisualEventsList.erase(std::remove_if(skillVisualEventsList.begin(), skillVisualEventsList.end(), [](const SkillVisualEvent& event) { return event.remainingTicks <= 0; }), skillVisualEventsList.end());
    for (const SkillVisualEvent& event : expired) emitSkillVisualEvent(event.skill, event.phase, event.position, event.radius, event.remainingTicks, event.branchId);
}

bool GameSim::castSkill(const SkillCastRequest& request, std::string* error) {
    if (error) error->clear();
    const std::size_t slot = request.slot;
    SkillDefinition definition = skillDefinition(request.skill);
    const bool pulseSafety = request.skill == SkillId::LastPulse && hasEquippedSkillNode("pulse_safety");
    if (pulseSafety && definition.healthCost > 0) definition.healthCost = std::max(1, (definition.healthCost + 1) / 2);
    const SkillLoadoutIdentity plagueIdentity = skillLoadoutIdentity();
    const bool freeMutation = request.skill == SkillId::Mutation && definition.resourceCost > 0 && plagueIdentity.primaryGroup == "plaguewright" && plagueIdentity.primaryCount >= 5 && economyState->plagueFreeMutationReady != 0;
    int* resource = resourcePointer(economyState->resources, definition.resourceId);
    if (definition.resourceCost > 0) {
        if (resource == nullptr) {
            if (error) *error = "UNKNOWN SKILL RESOURCE";
            return false;
        }
        if (*resource < (freeMutation ? 0 : definition.resourceCost)) {
            if (error) *error = "INSUFFICIENT " + definition.resourceId;
            return false;
        }
    }
    const SkillLoadoutIdentity bloodIdentity = skillLoadoutIdentity();
    const bool heartDiscount = definition.healthCost > 0 && bloodIdentity.primaryGroup == "bloodbinder" && bloodIdentity.primaryCount >= 5 && economyState->bloodHeartFragments >= 3;
    const int doctrineHealthCost = definition.healthCost > 0 && skillLoadoutState.doctrineId == "bloodbinder_sacrifice" ? std::max(1, static_cast<int>(std::ceil(static_cast<float>(definition.healthCost) * 1.15f))) : definition.healthCost;
    const int effectiveHealthCost = doctrineHealthCost > 0 && heartDiscount ? std::max(1, (doctrineHealthCost + 1) / 2) : doctrineHealthCost;
    const int eclipsePaid = definition.healthCost > 0 ? std::min(effectiveHealthCost, economyState->bloodEclipseHealth) : 0;
    const int reservePaid = definition.healthCost > 0 ? std::min(std::max(0, effectiveHealthCost - eclipsePaid), economyState->bloodGolemReserve) : 0;
    const int shieldPaid = definition.healthCost > 0 ? std::min(std::max(0, effectiveHealthCost - eclipsePaid - reservePaid), economyState->bloodHarvestShield) : 0;
    const int realHealthCost = std::max(0, effectiveHealthCost - eclipsePaid - reservePaid - shieldPaid);
    const int safetyFloor = economyState->bloodReservoirReady != 0 && definition.healthCost > 0 ? 2 : 1;
    const int payableHealthCost = definition.healthCost > 0 ? std::min(realHealthCost, std::max(0, lives - safetyFloor)) : 0;
    if (definition.healthCost > 0 && lives <= payableHealthCost) {
        if (error) *error = "INSUFFICIENT TOWER HEALTH";
        return false;
    }
    float radiusScale = 1.0f;
    float valueScale = 1.0f;
    float cooldownScale = 1.0f;
    float durationScale = 1.0f;
    int chargesDelta = 0;
    std::string branch;
    for (const SkillNodeDefinition& node : content.skillNodes) {
        if (node.skillId != definition.id || skillNodeRank(slot, node.id) <= 0) continue;
        const int rank = std::min(skillNodeRank(slot, node.id), node.maxRank);
        radiusScale *= std::pow(node.radiusScale, static_cast<float>(rank));
        valueScale *= std::pow(node.valueScale, static_cast<float>(rank));
        cooldownScale *= std::pow(node.cooldownScale, static_cast<float>(rank));
        durationScale *= std::pow(node.durationScale, static_cast<float>(rank));
        chargesDelta += node.chargesDelta * rank;
        if (node.tier >= 2) branch = node.branchId;
    }
    if (skillLoadoutState.doctrineId == "fatebinder_loaded" && hasSkillGroup("fatebinder")) durationScale *= 1.15f;
    definition.durationTicks = std::max(1, static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * durationScale)));
    const int maximumCharges = std::max(1, definition.charges + chargesDelta);
    const int previousCharges = skillCharges[slot];
    const int previousCooldown = skillCooldowns[slot];
    skillCharges[slot] = std::min(skillCharges[slot], maximumCharges);
    skillCharges[slot] = std::max(0, skillCharges[slot] - 1);
    if (hasSkillGroup("chronomancer")) cooldownScale *= 1.0f + static_cast<float>(economyState->resources.paradox) * 0.003f;
    if (economyState->chronomancerDebtBurstTicks > 0) cooldownScale *= 0.70f;
    skillCooldowns[slot] = std::max(1, static_cast<int>(static_cast<float>(definition.cooldownTicks) * cooldownScale));
    if (resource != nullptr && definition.resourceCost > 0 && !freeMutation) *resource -= definition.resourceCost;
    const int previousLives = lives;
    const int previousBloodDebt = economyState->bloodDebt;
    const int previousEclipseHealth = economyState->bloodEclipseHealth;
    const int previousHeartFragments = economyState->bloodHeartFragments;
    const int previousReservoirReady = economyState->bloodReservoirReady;
    const int previousHarvestShield = economyState->bloodHarvestShield;
    const int previousGolemReserve = economyState->bloodGolemReserve;
    const int previousParadox = economyState->resources.paradox;
    const std::size_t previousSkillVisualEventCount = skillVisualEventsList.size();
    if (definition.healthCost > 0) {
        economyState->bloodEclipseHealth -= eclipsePaid;
        economyState->bloodGolemReserve -= reservePaid;
        economyState->bloodHarvestShield -= shieldPaid;
        lives -= payableHealthCost;
        economyState->bloodDebt = std::min(100, economyState->bloodDebt + effectiveHealthCost);
        if (heartDiscount) economyState->bloodHeartFragments = 0;
        if (economyState->bloodReservoirReady != 0) economyState->bloodReservoirReady = 0;
    }
    const Vec2 target = request.target.world;
    const float radius = definition.radius * radiusScale;
    float valueA = definition.valueA * valueScale;
    float valueB = definition.valueB * valueScale;
    if (skillLoadoutState.doctrineId == "bloodbinder_sacrifice" && definition.healthCost > 0) {
        valueA *= 1.15f;
        valueB *= 1.10f;
    }
    if (request.skill == SkillId::LastPulse && pulseSafety) valueA *= 0.85f;
    if (request.skill == SkillId::Mutation) {
        valueA = static_cast<float>(mutationStrainSelection);
        if (branch == "spore" || branch == "symbiotic") valueB *= 1.15f;
    }
    if (skillLoadoutState.doctrineId == "void_shepherd_geometry" && hasSkillGroup("void_shepherd") &&
        (request.skill == SkillId::SpatialCollapse || request.skill == SkillId::PhaseExchange || request.skill == SkillId::EventHorizon || request.skill == SkillId::RiftGate)) valueA *= 1.20f;
    if (skillLoadoutState.doctrineId == "void_shepherd_stability" && hasSkillGroup("void_shepherd") && definition.durationTicks > 0) definition.durationTicks = static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * 1.20f));
    if (request.skill == SkillId::ArcBolt && hasSkillGroup("arcanist")) {
        const bool doctrinePulse = skillLoadoutState.doctrineId == "arcanist_focus" && economyState->arcanistCadence > 0 && economyState->arcanistCadence % 3 == 0;
        economyState->arcanistCadence = std::min(10, economyState->arcanistCadence + (doctrinePulse ? 2 : 1));
        if (economyState->arcanistCadence == 10 && hasEquippedSkillNode("bolt_capstone")) economyState->arcanistArcanumReady = 1;
    } else if (request.skill == SkillId::ChainLightning && hasSkillGroup("arcanist")) {
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        const bool fullSequence = economyState->arcanistCadence >= 10;
        const bool arcanum = economyState->arcanistArcanumReady != 0 && hasEquippedSkillNode("bolt_capstone");
        valueA *= 1.0f + static_cast<float>(economyState->arcanistCadence) * 0.10f;
        if (arcanum) {
            valueA *= 1.18f;
            economyState->arcanistArcanumReady = 0;
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius * 0.82f, TickRate, "arcanum");
        }
        economyState->arcanistCadence = 0;
        if (fullSequence && identity.primaryGroup == "arcanist" && identity.primaryCount >= 5) economyState->arcanistAfterimageReady = 1;
    }
    if (request.skill == SkillId::TemporalAnchor || (hasSkillGroup("chronomancer") && request.skill != SkillId::BorrowedTime)) economyState->resources.paradox = std::min(100, economyState->resources.paradox + 12);
    if ((request.skill == SkillId::GravityWell || request.skill == SkillId::PhaseMine) && hasSkillGroup("void_shepherd")) economyState->resources.instability = std::min(100, economyState->resources.instability + 8);
    const bool authoredPersistentVisual = std::any_of(definition.operations.begin(), definition.operations.end(), [](const std::string& operation) {
        return operation == "create_zone" || operation == "ward" || operation == "displace" || operation == "infect" || operation == "vector_swarm" || operation == "mutation" || operation == "quarantine" || operation == "exploit_weakness" || operation == "adapt_beast" || operation == "lucky_shot" || operation == "hunt_command";
    });
    const int persistentVisualDuration = request.skill == SkillId::GravityWell || request.skill == SkillId::PhaseMine || request.skill == SkillId::RallyBeacon || request.skill == SkillId::CryoField || request.skill == SkillId::ResonancePulse || request.skill == SkillId::DoubleNothing || authoredPersistentVisual
        ? definition.durationTicks : 10;
    emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, target, radius, persistentVisualDuration, branch);
    if (!definition.operations.empty() && executeAuthoredSkill(request, definition, radius, valueA, valueB)) {
        if (hasSkillGroup("void_shepherd")) {
            int operationBit = 0;
            if (request.skill == SkillId::RiftGate) operationBit = 1;
            else if (request.skill == SkillId::SpatialCollapse) operationBit = 2;
            else if (request.skill == SkillId::Banish) operationBit = 4;
            else if (request.skill == SkillId::PhaseExchange) operationBit = 8;
            else if (request.skill == SkillId::EventHorizon) operationBit = 16;
            if (operationBit != 0 && (economyState->voidSpatialOperationMask & operationBit) == 0) {
                economyState->voidSpatialOperationMask |= operationBit;
                int distinctOperations = 0;
                for (int mask = economyState->voidSpatialOperationMask; mask != 0; mask >>= 1) distinctOperations += mask & 1;
                const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                if (distinctOperations >= 3 && identity.primaryGroup == "void_shepherd" && identity.primaryCount >= 5) {
                    economyState->voidSpatialOperationMask = 0;
                    economyState->voidFixedPointReady = 1;
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, TickRate * 2, "fixed_point_ready");
                }
            }
        }
        if (hasSkillGroup("chronomancer")) {
            int operationBit = 0;
            if (request.skill == SkillId::TemporalAnchor) operationBit = 1;
            else if (request.skill == SkillId::Accelerate) operationBit = 2;
            else if (request.skill == SkillId::Delay) operationBit = 4;
            else if (request.skill == SkillId::Rewind) operationBit = 8;
            else if (request.skill == SkillId::BorrowedTime) operationBit = 16;
            if (operationBit != 0 && (economyState->chronomancerOperationMask & operationBit) == 0) {
                economyState->chronomancerOperationMask |= operationBit;
                int distinctOperations = 0;
                for (int mask = economyState->chronomancerOperationMask; mask != 0; mask >>= 1) distinctOperations += mask & 1;
                if (distinctOperations >= 3) {
                    economyState->chronomancerOperationMask = 0;
                    economyState->chronomancerStableMomentReady = 1;
                    economyState->resources.paradox = std::max(0, economyState->resources.paradox - 8);
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, TickRate * 2, "stable_moment");
                }
            }
        }
        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
        if (resource != nullptr && definition.resourceRefund > 0) *resource = std::min(100, *resource + definition.resourceRefund);
        return true;
    }
    if (!definition.operations.empty()) {
        skillCharges[slot] = previousCharges;
        skillCooldowns[slot] = previousCooldown;
        if (resource != nullptr && definition.resourceCost > 0 && !freeMutation) *resource += definition.resourceCost;
        lives = previousLives;
        economyState->resources.paradox = previousParadox;
        economyState->bloodDebt = previousBloodDebt;
        economyState->bloodEclipseHealth = previousEclipseHealth;
        economyState->bloodHeartFragments = previousHeartFragments;
        economyState->bloodReservoirReady = previousReservoirReady;
        economyState->bloodHarvestShield = previousHarvestShield;
        economyState->bloodGolemReserve = previousGolemReserve;
        if (skillVisualEventsList.size() > previousSkillVisualEventCount) skillVisualEventsList.resize(previousSkillVisualEventCount);
        if (error) *error = "SKILL EFFECT COULD NOT EXECUTE";
        return false;
    }
    const auto issueLegionMinorOrder = [&](int unitType) {
        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
        if (identity.primaryGroup != "legion" || identity.primaryCount < 5) return;
        ++economyState->legionSummonCasts;
        if (economyState->legionSummonCasts % 3 != 0) return;
        economyState->legionLastOrderType = std::clamp(unitType, 1, 3);
        ++economyState->legionMinorOrders;
        int affected = 0;
        for (AlliedUnit& unit : alliedUnitsList) {
            if (!unit.alive) continue;
            const bool melee = unit.role == "soldier" || unit.role == "bulwark";
            const bool ranged = unit.role == "drone" || unit.role == "hunter" || unit.role == "disruptor" || unit.role == "striker";
            if (economyState->legionLastOrderType == 1 && melee) {
                unit.damageReduction = std::max(unit.damageReduction, 0.12f);
                unit.buffTicks = std::max(unit.buffTicks, TickRate * 3);
                ++affected;
            } else if (economyState->legionLastOrderType == 2 && ranged) {
                unit.damageScale = std::max(unit.damageScale, 1.15f);
                unit.speedScale = std::max(unit.speedScale, 1.25f);
                unit.buffTicks = std::max(unit.buffTicks, TickRate * 3);
                ++affected;
            } else if (economyState->legionLastOrderType == 3) {
                unit.hp = std::min(unit.maxHp, unit.hp + 8.0f);
                unit.damageReduction = std::max(unit.damageReduction, 0.08f);
                unit.buffTicks = std::max(unit.buffTicks, TickRate * 2);
                ++affected;
            }
        }
        static constexpr const char* orderNames[] = {"", "minor_melee_order", "minor_ranged_order", "minor_support_order"};
        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, TickRate * 2, orderNames[economyState->legionLastOrderType]);
        if (affected == 0) economyState->legionLastOrderType = 0;
    };
    switch (request.skill) {
        case SkillId::GravityWell:
            if (zones.size() < content.maxSkillZones) {
                zones.push_back({nextZoneId++, target, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true, branch == "edge_horizon" || hasEquippedSkillNode("gravity_edge_horizon")});
                SkillZone& gravity = zones.back();
                gravity.gravitySingularityLocked = hasEquippedSkillNode("gravity_singularity_capstone");
                if (gravity.gravitySingularityLocked) {
                    Enemy* strongest = nullptr;
                    for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, target) <= radius * radius && (strongest == nullptr || enemy.hp > strongest->hp || (enemy.hp == strongest->hp && enemy.id < strongest->id))) strongest = &enemy;
                    gravity.predictedEnemyId = strongest == nullptr ? 0 : strongest->id;
                }
            }
            break;
        case SkillId::PhaseMine:
            if (zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, target, radius, definition.durationTicks, TickRate / 2, valueA, valueB, request.skill, false, true});
            break;
        case SkillId::VanguardDrop: {
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            const int count = std::max(2, static_cast<int>(valueA) + chargesDelta);
            for (int index = 0; index < count; ++index) spawnAlliedUnit({target.x + static_cast<float>(index * 18), target.y + static_cast<float>((index % 2) * 20 - 10)}, request.skill, branch == "bulwark" ? "bulwark" : (branch == "strike_team" ? "striker" : "soldier"), definition.durationTicks, valueB * (branch == "bulwark" ? 1.35f : 1.0f), 10.0f * (branch == "strike_team" ? 1.55f : 1.0f), branch == "strike_team" ? 72.0f : 52.0f);
            issueLegionMinorOrder(branch == "strike_team" ? 2 : 1);
            break;
        }
        case SkillId::ForwardBarracks:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 12, branch);
            spawnBuilding(target, request.skill, branch == "field_armory" ? "armory" : "barracks", definition.durationTicks, valueA * (branch == "field_armory" ? 1.3f : 1.0f));
            issueLegionMinorOrder(3);
            break;
        case SkillId::RuinHex:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, target) <= radius * radius) {
                enemy.vulnerability = std::max(enemy.vulnerability, valueA);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                enemy.ruinBrittleTriggered = false;
                if (branch == "withering") enemy.slow = std::max(enemy.slow, hasEquippedSkillNode("ruin_withering_mastery") ? 4.0f : 3.0f);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            break;
        case SkillId::RallyBeacon: {
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
            const bool warCry = hasEquippedSkillNode("rally_warcry");
            const float rallyHeal = valueA * (warCry ? 0.65f : 1.0f);
            const float rallyDamage = valueB * (warCry ? 1.25f : 1.0f) * (hasEquippedSkillNode("rally_warcry_mastery") ? 1.12f : 1.0f);
            const float rallySpeed = (warCry ? 1.45f : 1.25f) * (hasEquippedSkillNode("rally_warcry_mastery") ? 1.10f : 1.0f);
            bool revived = false;
            for (AlliedUnit& unit : alliedUnitsList) {
                if (!revived && hasEquippedSkillNode("rally_medic_capstone") && !unit.alive && unit.downedTicks > 0 && distanceSquared(unit.pos, target) <= radius * radius) {
                    unit.alive = true;
                    unit.downedTicks = 0;
                    unit.hp = std::max(1.0f, unit.maxHp * 0.35f);
                    revived = true;
                    emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, unit.pos, unit.radius, 12, "field_revive");
                    ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
                }
                if (unit.alive && distanceSquared(unit.pos, target) <= radius * radius) { const float before = unit.hp; unit.hp = std::min(unit.maxHp, unit.hp + rallyHeal); unit.damageScale = std::max(unit.damageScale, rallyDamage); unit.speedScale = std::max(unit.speedScale, rallySpeed); unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks); if (hasEquippedSkillNode("rally_medic")) unit.injuryTicks = 0; if (hasEquippedSkillNode("rally_warcry_capstone")) unit.nextAttackCooldownReduced = true; ++counters.skillTargets[static_cast<std::size_t>(request.skill)]; counters.skillHealing[static_cast<std::size_t>(request.skill)] += static_cast<int>(std::round(unit.hp - before)); }
            }
            break;
        }
        case SkillId::SentryFabricator:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            spawnBuilding(target, request.skill, branch == "mortar" ? "mortar" : (branch == "gatling" ? "gatling" : "sentry"), definition.durationTicks, valueA);
            break;
        case SkillId::CryoField:
            if (zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, target, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
            break;
        case SkillId::DroneSwarm: {
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            if (skillLoadoutIdentity().primaryGroup == "salvager" && !economyState->drones.empty()) {
                // Salvager's drone skill is an active timing tool. It snapshots
                // the deployed network; drones rebuilt later do not inherit it.
                for (RecoveryDrone& drone : economyState->drones) drone.boostTicks = std::max(drone.boostTicks, definition.durationTicks);
                break;
            }
            const int count = std::max(2, static_cast<int>(valueA) + chargesDelta);
            for (int index = 0; index < count; ++index) spawnAlliedUnit({target.x + static_cast<float>(index * 12), target.y + static_cast<float>((index % 3) * 14 - 14)}, request.skill, branch == "disruptor" ? "disruptor" : (branch == "hunter" ? "hunter" : "drone"), definition.durationTicks, 22.0f, valueB * (branch == "hunter" ? 1.45f : 1.0f), 85.0f);
            issueLegionMinorOrder(2);
            break;
        }
        default:
            break;
        case SkillId::Count: break;
    }
    if (resource != nullptr && definition.resourceRefund > 0) *resource = std::min(100, *resource + definition.resourceRefund);
    return true;
}

SkillSnapshot GameSim::skillSnapshot(std::size_t slot) const {
    SkillSnapshot snapshot;
    if (slot >= SkillSlotCount) return snapshot;
    const SkillDefinition& definition = skillDefinition(skillLoadoutState.skills[slot]);
    snapshot.skill = skillLoadoutState.skills[slot];
    snapshot.targetMode = targetModeFromString(definition.targetMode);
    snapshot.cooldownRemaining = skillCooldowns[slot];
    float cooldownScale = 1.0f;
    float durationScale = 1.0f;
    float radiusScale = 1.0f;
    for (const SkillNodeDefinition& node : content.skillNodes) {
        const int rank = std::min(skillNodeRank(slot, node.id), node.maxRank);
        if (node.skillId != definition.id || rank <= 0) continue;
        cooldownScale *= std::pow(node.cooldownScale, static_cast<float>(rank));
        durationScale *= std::pow(node.durationScale, static_cast<float>(rank));
        radiusScale *= std::pow(node.radiusScale, static_cast<float>(rank));
    }
    snapshot.cooldownMaximum = std::max(1, static_cast<int>(std::round(static_cast<float>(definition.cooldownTicks) * cooldownScale)));
    snapshot.resolvedDurationTicks = std::max(1, static_cast<int>(std::round(static_cast<float>(definition.durationTicks) * durationScale)));
    snapshot.resolvedRadius = definition.radius * radiusScale;
    snapshot.resolvedRange = definition.range;
    float valueScale = 1.0f;
    for (const SkillNodeDefinition& node : content.skillNodes) {
        const int rank = std::min(skillNodeRank(slot, node.id), node.maxRank);
        if (node.skillId == definition.id && rank > 0) valueScale *= std::pow(node.valueScale, static_cast<float>(rank));
    }
    snapshot.resolvedValueA = definition.valueA * valueScale;
    snapshot.resolvedValueB = definition.valueB * valueScale;
    if (snapshot.skill == SkillId::ChainLightning && hasSkillGroup("arcanist")) {
        snapshot.resolvedValueA *= 1.0f + static_cast<float>(economyState->arcanistCadence) * 0.10f;
        if (economyState->arcanistArcanumReady != 0 && hasEquippedSkillNode("bolt_capstone")) snapshot.resolvedValueA *= 1.18f;
    }
    snapshot.charges = skillCharges[slot];
    snapshot.resourceId = definition.resourceId;
    snapshot.resourceCost = definition.resourceCost;
    snapshot.healthCost = definition.healthCost;
    if (const int* resource = resourcePointer(economyState->resources, definition.resourceId)) snapshot.resourceAvailable = *resource;
    int chargesDelta = 0;
    for (const SkillNodeDefinition& node : content.skillNodes) if (node.skillId == definition.id && skillNodeRank(slot, node.id) > 0) chargesDelta += node.chargesDelta * std::min(skillNodeRank(slot, node.id), node.maxRank);
    snapshot.maximumCharges = std::max(1, definition.charges + chargesDelta);
    snapshot.iconId = definition.iconId;
    for (const SkillNodeDefinition& node : content.skillNodes) if (node.skillId == definition.id && skillNodeRank(slot, node.id) > 0 && node.tier >= 2) {
        snapshot.branchId = node.branchId;
        snapshot.iconId = definition.iconId + "." + node.iconLayer;
    }
    return snapshot;
}

float GameSim::pathY(float x, int enemyId) const {
    const std::size_t index = static_cast<std::size_t>(selectedArena);
    const float idOffset = index == 0 ? 37.0f : (index == 1 ? 23.0f : 51.0f);
    return 360.0f + std::sin((x + enemyId * idOffset) * content.arenaPathFrequency[index]) * content.arenaPathAmplitude[index];
}

void GameSim::spawnWaveIfNeeded() {
    if (spawnedThisWave != 0 || !enemyList.empty() || (!endlessMode && wave > 10)) return;
    if (hasSkillGroup("architect")) economyState->resources.buildSupply = std::min(economyState->resources.buildSupplyCap, economyState->resources.buildSupply + 25);
    if (hasSkillGroup("salvager") && economyState->allowanceWave != wave) {
        economyState->resources.scrap = std::max(economyState->resources.scrap, 8);
        economyState->allowanceWave = wave;
    }
    const int authoredWave = std::clamp(wave, 1, 10);
    waveSpawnTarget = content.waveEnemyBudget[static_cast<std::size_t>(authoredWave - 1)];
    if (endlessMode && wave > 10) {
        const float endlessScale = 1.0f + static_cast<float>(wave - 10) * 0.12f;
        waveSpawnTarget = std::max(1, static_cast<int>(std::ceil(static_cast<float>(waveSpawnTarget) * endlessScale)));
    }
    // Swarm increases regular-wave pressure, but the final wave is authored as
    // a single boss encounter and must stay that way under every modifier.
    if (hasSkull(Skull::Swarm) && (endlessMode || wave < 10)) {
        waveSpawnTarget = static_cast<int>(std::ceil(static_cast<float>(waveSpawnTarget) * content.skullSpawnScale[static_cast<std::size_t>(Skull::Swarm)]));
    }
    spawnedThisWave = 0;
}

void GameSim::spawnEnemy(bool boss) {
    Enemy enemy;
    enemy.id = nextEnemyId++;
    enemy.pos = {92.0f, pathY(92.0f, enemy.id)};
    enemy.pathHistory[0] = enemy.pos;
    enemy.pathHistoryCount = 1;
    enemy.pathHistoryHead = 0;
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
    if (boss) enemy.attackCooldownTicks = content.bossAttackCooldownTicks;
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
    const bool timeFracture = economyState->timeFractureTicks > 0;
    const float hostileTimeScale = timeFracture ? 0.4f : 1.0f;
    const auto mutationFor = [&](int strain) -> const PlagueMutationDefinition* {
        for (const PlagueMutationDefinition& mutation : content.plagueMutations) if (mutation.strain == strain) return &mutation;
        return nullptr;
    };
    for (Enemy& enemy : enemyList) {
        if (!enemy.alive) continue;
        if (enemy.predictedTicks > 0) --enemy.predictedTicks;
        if (enemy.spatialCooldownTicks > 0) --enemy.spatialCooldownTicks;
        if (enemy.challengeTicks > 0) --enemy.challengeTicks;
        if (enemy.banishedTicks > 0) {
            --enemy.banishedTicks;
            if (enemy.banishedTicks == 0 && enemy.banishReturnArmed) {
                enemy.pos = enemy.banishReturnPosition;
                enemy.vulnerability = std::max(enemy.vulnerability, hasEquippedSkillNode("banish_capstone") ? 0.30f : 0.12f);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, TickRate * (hasEquippedSkillNode("banish_capstone") ? 2 : 1));
                enemy.banishReturnArmed = false;
                emitSkillVisualEvent(SkillId::Banish, SkillVisualPhase::Hit, enemy.pos, 34.0f, 10, hasEquippedSkillNode("banish_capstone") ? "null_return" : "return_vector");
            }
            continue;
        }
        if (enemy.stun > 0.0f) enemy.stun -= 1.0f / TickRate;
        if (enemy.slow > 0.0f) enemy.slow -= 1.0f / TickRate;
        if (enemy.cryoWhiteoutTicks > 0) {
            --enemy.cryoWhiteoutTicks;
            const float fade = static_cast<float>(enemy.cryoWhiteoutTicks) / static_cast<float>(TickRate * 2);
            enemy.slow = std::max(enemy.slow, 0.30f + std::clamp(fade, 0.0f, 1.0f) * 0.65f);
        }
        if (enemy.vulnerabilityTicks > 0) --enemy.vulnerabilityTicks;
        if (enemy.vulnerabilityTicks <= 0) enemy.vulnerability = 0.0f;
        if (enemy.alive && enemy.vulnerabilityTicks > 0 && hasEquippedSkillNode("ruin_withering_capstone")) {
            const float healthMissing = enemy.maxHp > 0.0f ? std::clamp(1.0f - enemy.hp / enemy.maxHp, 0.0f, 1.0f) : 0.0f;
            enemy.slow = std::max(enemy.slow, 0.25f + healthMissing * 0.75f);
        }
        if (enemy.shockTicks > 0) --enemy.shockTicks;
        if (enemy.soakTicks > 0) --enemy.soakTicks;
        if (enemy.freezeTicks > 0) --enemy.freezeTicks;
        if (enemy.galeTicks > 0) --enemy.galeTicks;
        if (enemy.stormReactionCooldownTicks > 0) --enemy.stormReactionCooldownTicks;
        if (enemy.bountyTicks > 0) --enemy.bountyTicks;
        if (enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId && enemy.alive) {
            ++economyState->bountyAgeTicks;
            bool isolated = true;
            for (const Enemy& nearby : enemyList) if (nearby.alive && nearby.id != enemy.id && distanceSquared(enemy.pos, nearby.pos) <= 110.0f * 110.0f) { isolated = false; break; }
            if (isolated) {
                ++economyState->bountyIsolationTicks;
                for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "isolation_ticks")) economyState->bountyObjectiveProgress[objective] = std::min(economyState->bountyObjectiveTargets[objective], economyState->bountyIsolationTicks);
            }
            economyState->bountyObjectivesCompleted = 0;
            for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (economyState->bountyObjectiveKinds[objective] >= 0 && economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) ++economyState->bountyObjectivesCompleted;
        }
        if (enemy.infectionTicks > 0) {
            const float infectionDamage = (skillLoadoutState.doctrineId == "plaguewright_necrotic" ? 1.5f : 1.0f) * (hasEquippedSkillNode("patient_necrotic") ? 1.12f : 1.0f);
            const PlagueMutationDefinition* mutation = mutationFor(enemy.infectionStrain);
            if (tickCount % 15 == 0) {
                const float strainScale = mutation == nullptr ? 1.0f : mutation->damageScale;
                applyDamage(enemy, (2.0f + static_cast<float>(enemy.infectionStacks) * 1.5f) * infectionDamage * strainScale);
                if (mutation != nullptr && mutation->hostileDamage > 0.0f && enemy.alive) for (Enemy& other : enemyList) if (other.alive && other.id != enemy.id && other.allegiance == 0 && distanceSquared(other.pos, enemy.pos) < mutation->spreadRadius * mutation->spreadRadius) applyDamage(other, mutation->hostileDamage);
            }
            const bool primeSource = economyState->pandemicTicks > 0 && enemy.infectionStrain == economyState->pandemicPrimeStrain && (enemy.id == economyState->pandemicPrimeHostId || enemy.infectionGeneration > 0);
            if (enemy.alive && enemy.infectionGeneration < 2 && (!primeSource || !enemy.pandemicSpreadUsed) && tickCount % TickRate == enemy.id % TickRate) {
                Enemy* spreadTarget = nullptr;
                const float spreadRadius = mutation == nullptr ? 92.0f : mutation->spreadRadius;
                const float authoredSpreadRadius = spreadRadius * (hasEquippedSkillNode("patient_spore") ? 1.20f : 1.0f);
                float nearest = authoredSpreadRadius * authoredSpreadRadius;
                for (Enemy& candidate : enemyList) {
                    if (!candidate.alive || candidate.id == enemy.id || candidate.infectionTicks > 0) continue;
                    const float distance = distanceSquared(enemy.pos, candidate.pos);
                    if (distance <= nearest && (spreadTarget == nullptr || distance < nearest || candidate.id < spreadTarget->id)) { nearest = distance; spreadTarget = &candidate; }
                }
                if (spreadTarget != nullptr) {
                    spreadTarget->infectionTicks = std::max(spreadTarget->infectionTicks, TickRate * 5);
                    spreadTarget->infectionStacks = std::max(1, enemy.infectionStacks - (primeSource ? 0 : 1));
                    spreadTarget->infectionGeneration = enemy.infectionGeneration + 1;
                    spreadTarget->infectionStrain = enemy.infectionStrain;
                    spreadTarget->pandemicSpreadUsed = primeSource;
                    if (primeSource) enemy.pandemicSpreadUsed = true;
                    ++counters.statusApplications;
                    if (hasEquippedSkillNode("patient_spore_mastery")) {
                        Enemy* secondarySpreadTarget = nullptr;
                        const float masterySpreadRadius = authoredSpreadRadius * 5.0f;
                        float secondaryDistance = masterySpreadRadius * masterySpreadRadius;
                        for (Enemy& candidate : enemyList) {
                            if (!candidate.alive || candidate.id == enemy.id || candidate.id == spreadTarget->id || candidate.infectionTicks > 0) continue;
                            const float distance = distanceSquared(enemy.pos, candidate.pos);
                            if (distance <= secondaryDistance && (secondarySpreadTarget == nullptr || distance < secondaryDistance || candidate.id < secondarySpreadTarget->id)) {
                                secondaryDistance = distance;
                                secondarySpreadTarget = &candidate;
                            }
                        }
                        if (secondarySpreadTarget != nullptr) {
                            secondarySpreadTarget->infectionTicks = std::max(secondarySpreadTarget->infectionTicks, TickRate * 4);
                            secondarySpreadTarget->infectionStacks = std::max(1, enemy.infectionStacks - (primeSource ? 0 : 2));
                            secondarySpreadTarget->infectionGeneration = enemy.infectionGeneration + 1;
                            secondarySpreadTarget->infectionStrain = enemy.infectionStrain;
                            secondarySpreadTarget->pandemicSpreadUsed = primeSource;
                            ++counters.statusApplications;
                            emitSkillVisualEvent(SkillId::PatientZero, SkillVisualPhase::Hit, secondarySpreadTarget->pos, 18.0f, TickRate / 2, "spore_mastery");
                        }
                    }
                }
            }
            --enemy.infectionTicks;
            if (enemy.infectionTicks <= 0) enemy.infectionStacks = 0;
        }
        if (enemy.temporalDelayTicks > 0) --enemy.temporalDelayTicks;
        if (enemy.temporalEchoTicks > 0 && enemy.alive) {
            if (enemy.pathHistoryCount > 1) {
                const int historySize = static_cast<int>(enemy.pathHistory.size());
                const int historyIndex = (enemy.pathHistoryHead - 1 + historySize) % historySize;
                enemy.pos = enemy.pathHistory[static_cast<std::size_t>(historyIndex)];
            } else {
                enemy.pos.x = std::max(92.0f, enemy.pos.x - 24.0f);
                enemy.pos.y = pathY(enemy.pos.x, enemy.id);
            }
            enemy.temporalEchoTicks = 0;
            enemy.temporalDelayTicks = std::max(enemy.temporalDelayTicks, TickRate / 3);
        }
        if (enemy.allegianceTicks > 0) {
            --enemy.allegianceTicks;
            if (enemy.allegianceTicks == 0) {
                const bool controlled = enemy.allegiance == 1;
                const bool controlledElite = controlled && (enemy.boss || enemy.maxHp >= 180.0f);
                enemy.allegiance = 0;
                enemy.usurperInheritedMark = false;
                if (enemy.usurperTreasonMark) {
                    enemy.usurperTreasonMark = false;
                    enemy.damageResistance = content.enemyDamageResistance[static_cast<std::size_t>(enemy.type)];
                }
                enemy.confusionTicks = 0;
                if (controlledElite && hasEquippedSkillNode("puppet_capstone")) {
                    economyState->resources.discord = std::min(100, economyState->resources.discord + 4);
                    emitSkillVisualEvent(SkillId::PuppetThread, SkillVisualPhase::Expire, enemy.pos, enemy.radius, TickRate, "masters_hand");
                }
                if (controlled && economyState->usurperRiotReady != 0 && hasEquippedSkillNode("orders_capstone")) {
                    economyState->resources.discord = std::min(100, economyState->resources.discord + 6);
                    economyState->usurperRiotReady = 0;
                    emitSkillVisualEvent(SkillId::FalseOrders, SkillVisualPhase::Expire, enemy.pos, enemy.radius, TickRate, "coup_reward");
                }
            }
        }
        if (enemy.confusionTicks > 0) --enemy.confusionTicks;
        if (enemy.sharedAgonyTicks > 0) --enemy.sharedAgonyTicks;
        if (enemy.temporalAnchorTicks > 0) {
            --enemy.temporalAnchorTicks;
            if (enemy.temporalAnchorTicks == 0 && enemy.temporalAnchorValid && enemy.alive) {
                enemy.pos = enemy.temporalAnchorPosition;
                enemy.hp = std::min(enemy.maxHp, std::max(1.0f, enemy.temporalAnchorHealth));
                if (hasEquippedSkillNode("anchor_delay")) {
                    enemy.vulnerability = std::max(enemy.vulnerability, 0.20f);
                    enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, TickRate * 2);
                }
                if (hasEquippedSkillNode("anchor_capstone")) {
                    enemy.temporalDelayTicks = std::max(enemy.temporalDelayTicks, TickRate / 2);
                    enemy.slow = std::max(enemy.slow, 3.0f);
                }
                enemy.temporalAnchorValid = false;
                enemy.temporalAnchorProtected = false;
            }
        }
        if (enemy.teleportCooldown > 0.0f) enemy.teleportCooldown -= 1.0f / TickRate;
        if (enemy.signalJamTicks > 0) --enemy.signalJamTicks;
        if (enemy.attackCooldownTicks > 0 && (!timeFracture || tickCount % 5u < 2u)) --enemy.attackCooldownTicks;
        if (enemy.burn > 0.0f) {
            enemy.burn -= 1.0f / TickRate;
            if (enemy.burnTicks-- % 5 == 0) applyDamage(enemy, enemy.burnDps / TickRate * 5.0f);
        }
        if (enemy.poison > 0.0f) {
            enemy.poison -= 1.0f / TickRate;
            if (enemy.poisonTicks-- % 7 == 0) applyDamage(enemy, enemy.poisonDps / TickRate * 7.0f);
        }
        if (!enemy.alive || enemy.stun > 0.0f || enemy.temporalDelayTicks > 0) continue;
        if (enemy.boss && enemy.phase == 1 && enemy.hp <= enemy.maxHp * 0.55f) {
            enemy.phase = 2;
            const float arenaSpeed = content.arenaSpeedScale[static_cast<std::size_t>(selectedArena)];
            enemy.speed = 66.666664f * content.enemySpeedScale[static_cast<std::size_t>(EnemyType::Boss)] * arenaSpeed * (hasSkull(Skull::Haste) ? content.skullSpeedScale[static_cast<std::size_t>(Skull::Haste)] : 1.0f);
            enemy.damageResistance = 0.20f;
        }
        if (enemy.type == EnemyType::Teleporter && enemy.teleportCooldown <= 0.0f && enemy.signalJamTicks <= 0) {
            enemy.pos.x = std::max(92.0f, enemy.pos.x - 115.0f);
            enemy.teleportCooldown = content.enemyTeleportCooldown[static_cast<std::size_t>(EnemyType::Teleporter)] + random01() * 1.5f;
        }
        if (enemy.boss) {
            if (enemy.telegraphTicks > 0) {
                if (!timeFracture || tickCount % 5u < 2u) --enemy.telegraphTicks;
                if (enemy.telegraphTicks == 0) {
                    lives = std::max(0, lives - (economyState->guardianWardTicks > 0 ? std::max(1, content.bossAttackLives / 2) : content.bossAttackLives));
                    ++counters.bossAttacks;
                    enemy.attackCooldownTicks = content.bossAttackCooldownTicks;
                    if (lives <= 0) gameOver = true;
                }
            } else if (enemy.attackCooldownTicks <= 0) {
                // Fifteen ticks is a 500 ms warning at the fixed 30 Hz tick.
                enemy.telegraphTicks = content.bossTelegraphTicks;
            }
        }
        const std::uint8_t supportLevel = workshopSupportLevels[static_cast<std::size_t>(selectedSupport)];
        if (selectedSupport == SupportModule::StasisField && enemy.stun <= 0.0f) enemy.slow = std::max(enemy.slow, 0.35f + static_cast<float>(supportLevel) * 0.01f);
        if (!enemy.boss && enemy.temporalCancelTicks > 0) {
            --enemy.temporalCancelTicks;
            enemy.attackCooldownTicks = 24;
            continue;
        }
        if (!enemy.boss && enemy.attackCooldownTicks <= 0) {
            Enemy* rivalTarget = nullptr;
            const bool riotLogic = enemy.confusionTicks > 0 && hasEquippedSkillNode("orders_riot");
            const float interceptionRadius = hasEquippedSkillNode("puppet_shield") ? 160.0f : (riotLogic ? 150.0f : 110.0f);
            float rivalDistance = interceptionRadius * interceptionRadius;
            for (Enemy& rival : enemyList) {
                const float distance = distanceSquared(enemy.pos, rival.pos);
                const bool opposing = (enemy.allegiance == 1 && rival.allegiance == 0) || (enemy.allegiance == 0 && rival.allegiance == 1);
                if (rival.alive && rival.id != enemy.id && opposing && distance <= rivalDistance && (rivalTarget == nullptr || distance < rivalDistance || rival.id < rivalTarget->id)) { rivalDistance = distance; rivalTarget = &rival; }
            }
            if (rivalTarget != nullptr) {
                const float friendlyFireScale = enemy.allegiance == 1 && hasEquippedSkillNode("puppet_court") ? 1.25f : 1.0f;
                const float turncoatScale = enemy.usurperTreasonMark && hasEquippedSkillNode("treason_turncoat")
                    ? (1.20f * (hasEquippedSkillNode("treason_turncoat_mastery") ? 1.12f : 1.0f))
                    : 1.0f;
                applyDamage(*rivalTarget, (enemy.allegiance == 1 ? 12.0f : 6.0f) * friendlyFireScale * turncoatScale);
                economyState->resources.discord = std::min(100, economyState->resources.discord + 1);
                if (!rivalTarget->alive) {
                    ++economyState->usurperInfightingKills;
                    const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                    if (identity.primaryGroup == "usurper" && identity.primaryCount >= 5 && economyState->usurperInfightingKills % 3 == 0 && alliedUnitsList.size() < content.maxAlliedUnits) {
                        const std::size_t before = alliedUnitsList.size();
                        spawnAlliedUnit(rivalTarget->pos, SkillId::RiotWhisper, "rebel_echo", TickRate * 6, 30.0f, 10.0f, 28.0f);
                        if (alliedUnitsList.size() > before) {
                            ++economyState->usurperRebelEchoes;
                            emitSkillVisualEvent(SkillId::RiotWhisper, SkillVisualPhase::Hit, rivalTarget->pos, 32.0f, TickRate * 2, "rebel_echo");
                        }
                    }
                }
                enemy.attackCooldownTicks = 24;
                continue;
            }
            AlliedUnit* allyTarget = nullptr;
            float allyDistance = 42.0f * 42.0f;
            for (AlliedUnit& ally : alliedUnitsList) {
                const float distance = distanceSquared(enemy.pos, ally.pos);
                const bool safeCollector = ally.role == "collector_drone" && hasEquippedSkillNode("collector_safe");
                if (ally.alive && !safeCollector && distance <= allyDistance) { allyDistance = distance; allyTarget = &ally; }
            }
            DeployableBuilding* buildingTarget = nullptr;
            float buildingDistance = 48.0f * 48.0f;
            for (DeployableBuilding& building : buildings) {
                const float distance = distanceSquared(enemy.pos, building.pos);
                if (building.alive && distance <= buildingDistance) { buildingDistance = distance; buildingTarget = &building; }
            }
            if (allyTarget != nullptr && (buildingTarget == nullptr || allyDistance <= buildingDistance)) {
                const float controlledDamageScale = enemy.allegiance == 1 && skillLoadoutState.doctrineId == "usurper_puppeteer" ? 0.65f : 1.0f;
                const float preventedDamage = (economyState->guardianWardTicks > 0 ? 4.0f : 8.0f) * controlledDamageScale * (1.0f - std::clamp(allyTarget->damageReduction, 0.0f, 0.8f));
                allyTarget->hp -= preventedDamage;
                if (allyTarget->role == "beast") {
                    allyTarget->injuryTicks = std::max(allyTarget->injuryTicks, TickRate * 2);
                    if (economyState->beastAdaptation == 5 && (economyState->beastAdaptationTicks > 0 || economyState->beastAdaptationPersistent)) {
                        // Spiked Carapace is a contact retaliation trait, not a
                        // second pet skill. Keep the response capped per enemy
                        // attack so a surrounded pet cannot create an unbounded
                        // damage loop.
                        applyDamage(enemy, std::min(8.0f, preventedDamage * 0.75f));
                        emitSkillVisualEvent(SkillId::Adaptation, SkillVisualPhase::Hit, enemy.pos, enemy.radius + 8.0f, TickRate / 2, "spiked_carapace");
                    }
                    if (economyState->beastAdaptation == 4 && (economyState->beastAdaptationTicks > 0 || economyState->beastAdaptationPersistent)) {
                        // Burrowing is an escape response, not a teleport
                        // attack: move the pet a fixed distance away from the
                        // advancing lane and keep it inside the arena.
                        allyTarget->pos.x = std::min(static_cast<float>(Width - 50), allyTarget->pos.x + 34.0f);
                        allyTarget->attackCooldownTicks = 0;
                        emitSkillVisualEvent(SkillId::Adaptation, SkillVisualPhase::Hit, allyTarget->pos, allyTarget->radius + 10.0f, TickRate / 2, "burrowing_escape");
                    }
                }
                if (allyTarget->role == "bulwark" && hasEquippedSkillNode("intercept_resolve")) economyState->resources.resolve = std::min(100, economyState->resources.resolve + (hasEquippedSkillNode("intercept_capstone") ? 3 : 2));
                enemy.attackCooldownTicks = 24;
            } else if (buildingTarget != nullptr) {
                buildingTarget->hp -= economyState->guardianWardTicks > 0 ? 6.0f : 12.0f;
                enemy.attackCooldownTicks = 30;
            }
        }
        bool blockedByWall = false;
        const float hasteSuppression = enemy.signalJamTicks > 0 && hasSkull(Skull::Haste) ? 1.0f / content.skullSpeedScale[static_cast<std::size_t>(Skull::Haste)] : 1.0f;
        const float nextX = enemy.pos.x + enemy.speed * hasteSuppression * (enemy.slow > 0.0f ? 0.45f : 1.0f) * hostileTimeScale / TickRate;
        for (DeployableBuilding& wall : buildings) if (wall.alive && wall.role == "wall") {
            const bool atWall = enemy.pos.x <= wall.pos.x && nextX >= wall.pos.x && std::abs(pathY(wall.pos.x, enemy.id) - wall.pos.y) <= wall.footprintRadius;
            if (atWall || distanceSquared(enemy.pos, wall.pos) <= wall.footprintRadius * wall.footprintRadius) {
                wall.hp -= enemy.boss ? 8.0f : 4.0f;
                enemy.attackCooldownTicks = 24;
                blockedByWall = true;
                if (wall.effectValue > 0.0f) applyDamage(enemy, wall.effectValue / TickRate);
                break;
            }
        }
        if (blockedByWall) continue;
        for (DeployableBuilding& trap : buildings) if (trap.alive && trap.role == "trap") {
            const bool insideTrap = distanceSquared(enemy.pos, trap.pos) <= trap.footprintRadius * trap.footprintRadius;
            const auto contact = std::find(enemy.trapContactIds.begin(), enemy.trapContactIds.end(), trap.id);
            if (!insideTrap) {
                if (contact != enemy.trapContactIds.end()) enemy.trapContactIds.erase(contact);
                continue;
            }
            if (trap.charges <= 0 || trap.attackCooldownTicks > 0 || contact != enemy.trapContactIds.end()) continue;
            enemy.trapContactIds.push_back(trap.id);
            --trap.charges;
            int rearmRank = 0;
            for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
                if (skillLoadoutState.skills[slot] != trap.ownerSkill) continue;
                rearmRank = std::max(rearmRank, std::min(skillNodeRank(slot, "trap_rearm"), 3));
            }
            const float authoredRearmScale = std::pow(0.90f, static_cast<float>(rearmRank));
            trap.attackCooldownTicks = std::max(1, static_cast<int>(std::round((trap.effectValue > 0.0f ? 24.0f : 18.0f) * authoredRearmScale / std::max(1.0f, trap.networkRearmScale))));
            const int before = counters.damageDealt;
            damageArea(trap.pos, trap.footprintRadius * 1.35f, std::max(1.0f, trap.effectValue), false);
            counters.skillDamage[static_cast<std::size_t>(trap.ownerSkill)] += std::max(0, counters.damageDealt - before);
            ++counters.skillTargets[static_cast<std::size_t>(trap.ownerSkill)];
            ++counters.reactionTriggers;
            if (trap.ownerSkill == SkillId::TrapFoundry) {
                if (hasEquippedSkillNode("trap_incendiary")) {
                    for (Enemy& affected : enemyList) if (affected.alive && distanceSquared(affected.pos, trap.pos) <= trap.footprintRadius * 1.35f * trap.footprintRadius * 1.35f) {
                        affected.burn = std::max(affected.burn, 2.0f);
                        affected.burnDps = std::max(affected.burnDps, trap.effectValue * 0.35f);
                        affected.burnTicks = std::max(affected.burnTicks, TickRate * 2);
                        if (hasEquippedSkillNode("trap_incendiary_mastery") && zones.size() < content.maxSkillZones) {
                            zones.push_back({nextZoneId++, affected.pos, trap.footprintRadius * 0.72f, TickRate, 0, trap.effectValue * 0.18f, 0.0f, SkillId::TrapFoundry, false, true});
                            emitSkillVisualEvent(SkillId::TrapFoundry, SkillVisualPhase::Hit, affected.pos, trap.footprintRadius * 0.72f, TickRate, "firebreak");
                        }
                    }
                }
                if (hasEquippedSkillNode("trap_cryo")) {
                    for (Enemy& affected : enemyList) if (affected.alive && distanceSquared(affected.pos, trap.pos) <= trap.footprintRadius * 1.35f * trap.footprintRadius * 1.35f) affected.slow = std::max(affected.slow, 2.0f);
                }
                if (hasEquippedSkillNode("trap_cryo_mastery") && trap.linkedBuildingId != 0) {
                    for (DeployableBuilding& linked : buildings) if (linked.alive && linked.id == trap.linkedBuildingId && linked.role == "trap") {
                        linked.linkedPrimeTicks = std::max(linked.linkedPrimeTicks, TickRate);
                        linked.attackCooldownTicks = 0;
                        emitSkillVisualEvent(SkillId::TrapFoundry, SkillVisualPhase::Hit, linked.pos, linked.footprintRadius * 1.35f, TickRate, "linked_prime");
                        break;
                    }
                }
                if (hasEquippedSkillNode("trap_capstone") && economyState->trapNetworkWave != wave) {
                    economyState->trapNetworkWave = wave;
                    for (DeployableBuilding& linked : buildings) if (linked.alive && linked.role == "trap" && linked.id != trap.id) { ++linked.charges; break; }
                }
            } else if (trap.ownerSkill == SkillId::MineLayer && hasEquippedSkillNode("mine_slow")) {
                for (Enemy& affected : enemyList) if (affected.alive && distanceSquared(affected.pos, trap.pos) <= trap.footprintRadius * 1.35f * trap.footprintRadius * 1.35f) {
                    affected.slow = std::max(affected.slow, 1.8f * (hasEquippedSkillNode("scrap_trap_mastery") ? 1.18f : 1.0f));
                    if (!affected.boss && hasEquippedSkillNode("mine_slow_capstone")) affected.freezeTicks = std::max(affected.freezeTicks, TickRate / 2);
                }
            }
            emitSkillVisualEvent(trap.ownerSkill, SkillVisualPhase::Trigger, trap.pos, trap.footprintRadius * 1.35f, trap.attackCooldownTicks, "trigger");
        }
        const float slowFactor = enemy.slow > 0.0f ? std::max(0.25f, 0.45f - static_cast<float>(supportLevel) * 0.005f) : 1.0f;
        enemy.pos.x += enemy.speed * hasteSuppression * slowFactor * hostileTimeScale / TickRate;
        enemy.pos.y = pathY(enemy.pos.x, enemy.id);
        enemy.pathHistoryHead = (enemy.pathHistoryHead + 1) % static_cast<int>(enemy.pathHistory.size());
        enemy.pathHistory[static_cast<std::size_t>(enemy.pathHistoryHead)] = enemy.pos;
        enemy.pathHistoryCount = std::min(static_cast<int>(enemy.pathHistory.size()), enemy.pathHistoryCount + 1);
        if (enemy.pos.x >= ExitX) {
            enemy.alive = false;
            --lives;
            ++counters.leaks;
            if (lives <= 0) gameOver = true;
        }
    }
    enemyList.erase(std::remove_if(enemyList.begin(), enemyList.end(), [](const Enemy& e) { return !e.alive; }), enemyList.end());
}

void GameSim::createBattlefieldRemain(const Enemy& enemy, int value, int biomassValue) {
    const bool salvageEnabled = hasSkillGroup("salvager");
    const bool plagueEnabled = hasSkillGroup("plaguewright") && biomassValue > 0;
    const bool beastEnabled = hasSkillGroup("beastmaster");
    if ((!salvageEnabled && !plagueEnabled && !beastEnabled) || (value <= 0 && biomassValue <= 0)) return;
    if (economyState->remains.size() >= 64u) {
        const auto oldest = std::min_element(economyState->remains.begin(), economyState->remains.end(), [](const BattlefieldRemain& left, const BattlefieldRemain& right) { return left.expiryTick < right.expiryTick; });
        if (oldest != economyState->remains.end() && oldest->claimedByDrone == 0) economyState->remains.erase(oldest);
    }
    const int remainLifetime = skillLoadoutState.doctrineId == "salvager_logistics" ? TickRate * 18 : TickRate * 12;
    const int doctrineValue = salvageEnabled && skillLoadoutState.doctrineId == "salvager_scrapyard" ? value + 1 : value;
    const int capstoneValue = salvageEnabled && hasEquippedSkillNode("scrap_capstone") ? doctrineValue + 1 : doctrineValue;
    const int biomassLifetime = skillLoadoutState.doctrineId == "plaguewright_necrotic" ? TickRate * 18 : TickRate * 12;
    economyState->remains.push_back({economyState->nextRemainId++, enemy.pos, capstoneValue, plagueEnabled ? biomassValue : 0, enemy.type, tickCount, tickCount + (plagueEnabled ? biomassLifetime : remainLifetime), 0, false});
}

void GameSim::updateEconomyEntities() {
    const bool salvageEnabled = hasSkillGroup("salvager");
    const bool plagueEnabled = hasSkillGroup("plaguewright");
    const bool beastEnabled = hasSkillGroup("beastmaster");
    if (economyState->arsenalAmmoTicks > 0) --economyState->arsenalAmmoTicks;
    if (economyState->arsenalInventoryTicks > 0) --economyState->arsenalInventoryTicks;
    if (economyState->arsenalInventoryTicks == 0) economyState->arsenalInventoryScrap = 0;
    if (economyState->bloodPulseEmpowerTicks > 0) --economyState->bloodPulseEmpowerTicks;
    if (!salvageEnabled && !plagueEnabled && !beastEnabled) return;
    for (BattlefieldRemain& remain : economyState->remains) {
        const bool carriedByDrone = std::any_of(economyState->drones.begin(), economyState->drones.end(), [&](const RecoveryDrone& drone) {
            return drone.active && drone.carrying > 0 && drone.targetRemainId == remain.id;
        });
        if (remain.expiryTick <= tickCount && !carriedByDrone) { remain.consumed = true; remain.claimedByDrone = 0; }
    }
    economyState->remains.erase(std::remove_if(economyState->remains.begin(), economyState->remains.end(), [](const BattlefieldRemain& remain) { return remain.consumed; }), economyState->remains.end());
    if (plagueEnabled) {
        for (BattlefieldRemain& remain : economyState->remains) {
            if (remain.biomassValue <= 0 || remain.consumed || remain.claimedByDrone != 0 || tickCount - remain.createdTick < TickRate) continue;
            economyState->resources.biomass = std::min(100, economyState->resources.biomass + remain.biomassValue);
            remain.consumed = true;
        }
        economyState->remains.erase(std::remove_if(economyState->remains.begin(), economyState->remains.end(), [](const BattlefieldRemain& remain) { return remain.consumed; }), economyState->remains.end());
    }
    if (!salvageEnabled) return;
    for (RecoveryDrone& drone : economyState->drones) {
        if (!drone.active) continue;
        if (drone.boostTicks > 0) --drone.boostTicks;
        const float doctrineSpeed = skillLoadoutState.doctrineId == "salvager_logistics" ? 1.25f : 1.0f;
        const float currentSpeed = drone.speed * doctrineSpeed * (drone.boostTicks > 0 ? 2.0f : 1.0f);
        if (drone.carrying > 0) {
            const float dx = 180.0f - drone.pos.x;
            const float dy = 360.0f - drone.pos.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= std::max(8.0f, currentSpeed / static_cast<float>(TickRate))) {
                drone.pos = {180.0f, 360.0f};
                economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + drone.carrying);
                for (BattlefieldRemain& remain : economyState->remains) if (remain.id == drone.targetRemainId) {
                    remain.consumed = true;
                    remain.claimedByDrone = 0;
                    break;
                }
                drone.carrying = 0;
                drone.targetRemainId = 0;
            } else if (distance > 0.0f) {
                const float step = currentSpeed / static_cast<float>(TickRate);
                drone.pos.x += dx / distance * std::min(distance, step);
                drone.pos.y += dy / distance * std::min(distance, step);
            }
            continue;
        }
        BattlefieldRemain* target = nullptr;
        if (drone.targetRemainId != 0) {
            const auto existing = std::find_if(economyState->remains.begin(), economyState->remains.end(), [&](const BattlefieldRemain& remain) { return remain.id == drone.targetRemainId && !remain.consumed; });
            if (existing != economyState->remains.end()) target = &*existing;
            else drone.targetRemainId = 0;
        }
        if (target == nullptr) {
            for (BattlefieldRemain& remain : economyState->remains) {
                if (remain.consumed || remain.value <= 0 || (remain.claimedByDrone != 0 && remain.claimedByDrone != drone.id)) continue;
                if (target == nullptr || remain.expiryTick < target->expiryTick || (remain.expiryTick == target->expiryTick && (remain.value > target->value || (remain.value == target->value && remain.id < target->id)))) target = &remain;
            }
            if (target != nullptr) { target->claimedByDrone = drone.id; drone.targetRemainId = target->id; }
        }
        if (target == nullptr) {
            drone.pos.x += (180.0f - drone.pos.x) * 0.08f;
            drone.pos.y += (360.0f - drone.pos.y) * 0.08f;
            continue;
        }
        const float dx = target->pos.x - drone.pos.x;
        const float dy = target->pos.y - drone.pos.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= std::max(8.0f, currentSpeed / static_cast<float>(TickRate))) {
            drone.pos = target->pos;
            drone.carrying = target->value;
        } else if (distance > 0.0f) {
            const float step = currentSpeed / static_cast<float>(TickRate);
            drone.pos.x += dx / distance * std::min(distance, step);
            drone.pos.y += dy / distance * std::min(distance, step);
        }
    }
}

void GameSim::updateSkills() {
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) {
    const SkillDefinition& definition = skillDefinition(skillLoadoutState.skills[slot]);
    int chargesDelta = 0;
    float cooldownScale = 1.0f;
    if (!skillLoadoutState.nodeBuilds[slot].empty()) for (const SkillNodeDefinition& node : content.skillNodes) if (node.skillId == definition.id) {
        const int rank = std::min(skillNodeRank(slot, node.id), node.maxRank);
        if (rank <= 0) continue;
        chargesDelta += node.chargesDelta * rank;
        cooldownScale *= std::pow(node.cooldownScale, static_cast<float>(rank));
    }
        const int maximumCharges = std::max(1, definition.charges + chargesDelta);
        skillCharges[slot] = std::min(skillCharges[slot], maximumCharges);
        if (skillCooldowns[slot] > 0) --skillCooldowns[slot];
        if (skillCooldowns[slot] == 0 && skillCharges[slot] < maximumCharges) {
            ++skillCharges[slot];
            if (skillCharges[slot] < maximumCharges) skillCooldowns[slot] = std::max(1, static_cast<int>(static_cast<float>(definition.cooldownTicks) * cooldownScale));
        }
    }
}

void GameSim::applySkillDamage(Enemy& enemy, float damage, SkillId owner) {
    if (!enemy.alive) return;
    if (!enemy.weaknessTag.empty() && enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId && enemy.weaknessTag == bountyWeaknessForSkill(owner)) {
        damage *= 1.15f;
        if (hasEquippedSkillNode("exploit_contract_capstone")) {
            if (!enemy.weaknessRewarded) economyState->resources.trophies = std::min(100, economyState->resources.trophies + 2);
            enemy.weaknessRewarded = true;
        }
    }
    const int before = counters.damageDealt;
    applyDamage(enemy, damage);
    const int dealt = std::max(0, counters.damageDealt - before);
    if (owner != SkillId::Count) counters.skillDamage[static_cast<std::size_t>(owner)] += dealt;
    if (dealt > 0 && economyState->bloodEclipseTicks > 0 && hasSkillGroup("bloodbinder")) economyState->bloodEclipseHealth = std::min(50, economyState->bloodEclipseHealth + std::max(1, dealt / 20));
}

void GameSim::updateSkillZones() {
    for (SkillZone& zone : zones) {
        if (!zone.alive) continue;
        if (zone.armTicks > 0) { --zone.armTicks; continue; }
        if (zone.ownerSkill == SkillId::Thunderhead && hasEquippedSkillNode("thunderhead_pursuit")) {
            Enemy* strongest = nullptr;
            for (Enemy& candidate : enemyList) {
                if (!candidate.alive || distanceSquared(candidate.pos, zone.center) > 520.0f * 520.0f) continue;
                if (strongest == nullptr || candidate.hp > strongest->hp || (candidate.hp == strongest->hp && candidate.id < strongest->id)) strongest = &candidate;
            }
            if (strongest != nullptr) {
                const float dx = strongest->pos.x - zone.center.x;
                const float dy = strongest->pos.y - zone.center.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.001f) {
                    const float step = 6.0f * (hasEquippedSkillNode("thunderhead_pursuit_mastery") ? 1.35f : 1.0f);
                    zone.center.x += dx / distance * std::min(distance, step);
                    zone.center.y += dy / distance * std::min(distance, step);
                }
            }
        }
        const float radiusSq = zone.radius * zone.radius;
        if (zone.ownerSkill == SkillId::EyeOfTheStorm && (hasEquippedSkillNode("eye_storm_convergence") || hasEquippedSkillNode("eye_storm_roaming"))) {
            const bool roaming = hasEquippedSkillNode("eye_storm_roaming");
            Enemy* clusterAnchor = nullptr;
            int clusterCount = 0;
            for (Enemy& candidate : enemyList) {
                if (!candidate.alive) continue;
                int count = 0;
                for (const Enemy& nearby : enemyList) if (nearby.alive && distanceSquared(candidate.pos, nearby.pos) <= (zone.radius * 1.8f) * (zone.radius * 1.8f)) ++count;
                if (clusterAnchor == nullptr || count > clusterCount || (count == clusterCount && candidate.id < clusterAnchor->id)) {
                    clusterAnchor = &candidate;
                    clusterCount = count;
                }
            }
            if (clusterAnchor != nullptr) {
                const float dx = clusterAnchor->pos.x - zone.center.x;
                const float dy = clusterAnchor->pos.y - zone.center.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.001f) {
                    const float step = (roaming ? 5.0f : 3.0f) * (hasEquippedSkillNode("eye_storm_roaming_mastery") ? 1.25f : 1.0f);
                    zone.center.x = std::clamp(zone.center.x + dx / distance * std::min(distance, step), 92.0f, static_cast<float>(Width - 40));
                    zone.center.y = std::clamp(zone.center.y + dy / distance * std::min(distance, step), 80.0f, 640.0f);
                }
            }
        }
        for (Enemy& enemy : enemyList) {
            const Vec2 effectCenter = zone.ownerSkill == SkillId::MortarBarrage ? zone.predictedPosition : zone.center;
            if (!enemy.alive || distanceSquared(enemy.pos, effectCenter) > radiusSq) continue;
            if (zone.ownerSkill == SkillId::GravityWell) {
                if (zone.gravitySingularityLocked && zone.predictedEnemyId != 0 && enemy.id != zone.predictedEnemyId) continue;
                const float pull = std::max(0.0f, zone.valueA) / static_cast<float>(TickRate);
                const float edge = enemy.pos.x < zone.center.x ? 92.0f : static_cast<float>(Width - 40);
                const float destination = zone.pullsToEdge ? edge : zone.center.x;
                const float distanceToDestination = destination - enemy.pos.x;
                enemy.pos.x += std::copysign(std::min(std::abs(distanceToDestination), pull), distanceToDestination);
                enemy.slow = std::max(enemy.slow, 0.35f);
                if (zone.valueB > 0.0f) applySkillDamage(enemy, zone.valueB / static_cast<float>(TickRate), zone.ownerSkill);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::PhaseMine && !zone.triggered) {
                const int rewoundId = enemy.id;
                const bool phaseSnare = hasEquippedSkillNode("phase_mine_snare");
                const int snareDuration = phaseSnare ? TickRate * 4 : TickRate * 3;
                enemy.pos.x = std::max(92.0f, enemy.pos.x - zone.valueA);
                enemy.vulnerability = std::max(enemy.vulnerability, phaseSnare ? 0.24f : 0.18f);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, snareDuration);
                enemy.slow = std::max(enemy.slow, phaseSnare ? 2.75f : 2.0f);
                if (hasEquippedSkillNode("phase_snare_capstone")) {
                    enemy.signalJamTicks = std::max(enemy.signalJamTicks, TickRate * 2);
                    emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, enemy.radius + 10.0f, TickRate * 2, "phase_lock");
                }
                const bool phaseArray = hasEquippedSkillNode("phase_mine_array");
                const bool phaseArrayMastery = hasEquippedSkillNode("phase_array_mastery");
                const bool phaseArrayCapstone = hasEquippedSkillNode("phase_array_capstone");
                int extraTargets = phaseArray ? (phaseArrayMastery ? 2 : 1) : 0;
                if (phaseArrayCapstone) extraTargets = std::max(1, extraTargets);
                std::vector<int> rewoundTargets{rewoundId};
                for (int extra = 0; extra < extraTargets; ++extra) {
                    Enemy* chained = nullptr;
                    float chainedDistance = zone.radius * zone.radius;
                    for (Enemy& candidate : enemyList) {
                        if (!candidate.alive || std::find(rewoundTargets.begin(), rewoundTargets.end(), candidate.id) != rewoundTargets.end()) continue;
                        const float distance = distanceSquared(candidate.pos, zone.center);
                        if (distance <= chainedDistance && (chained == nullptr || distance < chainedDistance || (distance == chainedDistance && candidate.id < chained->id))) {
                            chained = &candidate;
                            chainedDistance = distance;
                        }
                    }
                    if (chained != nullptr) {
                        rewoundTargets.push_back(chained->id);
                        const float displacementScale = std::max(0.55f, 0.75f - static_cast<float>(extra) * 0.10f);
                        chained->pos.x = std::max(92.0f, chained->pos.x - zone.valueA * displacementScale);
                        chained->vulnerability = std::max(chained->vulnerability, phaseSnare ? 0.20f : 0.14f);
                        chained->vulnerabilityTicks = std::max(chained->vulnerabilityTicks, phaseSnare ? TickRate * 3 : TickRate * 2);
                        chained->slow = std::max(chained->slow, phaseSnare ? 2.25f : 1.5f);
                        emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, chained->pos, chained->radius + 8.0f, TickRate, phaseArrayCapstone ? "rewind_chain" : "rewind_array");
                    }
                }
                zone.triggered = true;
                ++counters.reactionTriggers;
            } else if (zone.ownerSkill == SkillId::CryoField) {
                enemy.slow = std::max(enemy.slow, zone.valueA);
                if (zone.valueB > 0.0f) enemy.stun = std::max(enemy.stun, zone.valueB / 10.0f);
                if (hasEquippedSkillNode("cryo_permafrost_capstone")) enemy.cryoWhiteoutTicks = std::max(enemy.cryoWhiteoutTicks, TickRate * 2);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::MortarBarrage) {
                if (!zone.triggered) {
                    applySkillDamage(enemy, zone.valueA, zone.ownerSkill);
                    zone.triggered = true;
                    ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                    if (zone.predictedEnemyId == enemy.id) {
                        ++economyState->resources.targetingData;
                        ++economyState->artilleristAccurateImpacts;
                        const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                        if (identity.primaryGroup == "artillerist" && identity.primaryCount >= 5 && economyState->artilleristAccurateImpacts >= 3) {
                            economyState->artilleristAccurateImpacts = 0;
                            economyState->artilleristFireSolutionReady = 1;
                            emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, zone.radius * 0.72f, TickRate * 2, "fire_solution_ready");
                        }
                    } else economyState->artilleristAccurateImpacts = 0;
                    if (hasEquippedSkillNode("artillery_capstone")) {
                        const float sideOffset = std::min(150.0f, zone.radius * 0.45f);
                        for (const float offset : {-sideOffset, sideOffset}) {
                            const Vec2 sidePoint{zone.predictedPosition.x + offset, pathY(zone.predictedPosition.x + offset, enemy.id)};
                            damageArea(sidePoint, zone.radius * 0.36f, zone.valueA * 0.35f, false);
                            emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, sidePoint, zone.radius * 0.36f, TickRate, "orbital_fire");
                        }
                    }
                }
            } else if (zone.ownerSkill == SkillId::RiftGate) {
                if (enemy.spatialCooldownTicks <= 0) {
                    enemy.pos.x = zone.secondaryCenter.x;
                    enemy.pos.y = pathY(enemy.pos.x, enemy.id);
                    enemy.spatialCooldownTicks = TickRate;
                    emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, zone.radius, 8, "exit");
                    if (hasEquippedSkillNode("rift_collapse")) {
                        for (Enemy& nearby : enemyList) if (nearby.alive && nearby.id != enemy.id && distanceSquared(nearby.pos, enemy.pos) <= zone.radius * zone.radius) {
                            const float dx = enemy.pos.x - nearby.pos.x;
                            const float dy = enemy.pos.y - nearby.pos.y;
                            const float distance = std::sqrt(dx * dx + dy * dy);
                            if (distance > 0.001f) {
                                const float pull = std::min(distance, hasEquippedSkillNode("rift_collapse_mastery") ? 36.0f : 24.0f);
                                nearby.pos.x += dx / distance * pull;
                                nearby.pos.y += dy / distance * pull;
                            }
                        }
                        emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, zone.radius * 0.75f, TickRate, "rift_collapse");
                    }
                    if (hasEquippedSkillNode("rift_capstone")) {
                        const float networkX = std::clamp(enemy.pos.x + zone.valueA * 0.45f, 92.0f, static_cast<float>(Width - 40));
                        enemy.pos.x = networkX;
                        enemy.pos.y = pathY(networkX, enemy.id);
                        emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, zone.radius * 0.72f, TickRate, "impossible_geometry");
                    }
                    ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                    ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
                }
            } else if (zone.ownerSkill == SkillId::TemporalAnchor) {
                enemy.temporalDelayTicks = std::max(enemy.temporalDelayTicks, TickRate / 2);
                enemy.slow = std::max(enemy.slow, 0.45f);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::GuardianWard) {
                enemy.slow = std::max(enemy.slow, 0.25f);
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::Quarantine) {
                if (enemy.infectionTicks > 0) {
                    enemy.infectionTicks = std::max(enemy.infectionTicks, TickRate / 2);
                    enemy.infectionStacks = std::min(5, enemy.infectionStacks + (hasEquippedSkillNode("quarantine_hot_capstone") ? 2 : 1));
                    const float quarantineDamage = zone.valueA / static_cast<float>(TickRate) * (hasEquippedSkillNode("quarantine_hot") ? 1.35f : 1.0f);
                    applySkillDamage(enemy, quarantineDamage, zone.ownerSkill);
                    if (hasEquippedSkillNode("quarantine_clean")) {
                        // Clean Room does not create Biomass every frame. It
                        // records contained processing and pays only at the
                        // authored end-of-zone transaction.
                        zone.processedTicks = std::min(150, zone.processedTicks + 1);
                        enemy.infectionGeneration = std::min(enemy.infectionGeneration, 1);
                    }
                    ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                }
                enemy.slow = std::max(enemy.slow, zone.valueB);
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::RuptureHost) {
                enemy.infectionTicks = std::max(enemy.infectionTicks, TickRate);
                enemy.infectionStacks = std::max(enemy.infectionStacks, std::max(1, static_cast<int>(zone.valueA) - 1));
                enemy.infectionStrain = std::clamp(static_cast<int>(zone.valueB), 1, 4);
                ++counters.statusApplications;
            } else if (zone.ownerSkill == SkillId::HemorrhageField) {
                applySkillDamage(enemy, zone.valueA / static_cast<float>(TickRate), zone.ownerSkill);
                enemy.vulnerability = std::max(enemy.vulnerability, zone.valueB);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, TickRate / 2);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::SharedAgony) {
                const int before = counters.damageDealt;
                applySkillDamage(enemy, zone.valueA / static_cast<float>(TickRate), zone.ownerSkill);
                for (Enemy& linked : enemyList) if (linked.alive && linked.id != enemy.id && distanceSquared(linked.pos, enemy.pos) <= 88.0f * 88.0f) applySkillDamage(linked, zone.valueA * 0.35f / static_cast<float>(TickRate), zone.ownerSkill);
                counters.skillDamage[static_cast<std::size_t>(zone.ownerSkill)] += std::max(0, counters.damageDealt - before);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::ClusterShell) {
                const int before = counters.damageDealt;
                applySkillDamage(enemy, zone.valueA, zone.ownerSkill);
                counters.skillDamage[static_cast<std::size_t>(zone.ownerSkill)] += std::max(0, counters.damageDealt - before);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::WalkingBarrage) {
                const bool longMarch = hasEquippedSkillNode("walking_long") || skillLoadoutState.doctrineId == "artillerist_walking";
                const bool denseSalvo = hasEquippedSkillNode("walking_dense");
                const int routeSteps = longMarch ? 7 : 5;
                const int stepTicks = denseSalvo ? 8 : (longMarch ? (skillLoadoutState.doctrineId == "artillerist_walking" ? 9 : 10) : 12);
                const int rawStep = std::min(routeSteps - 1, zone.processedTicks / stepTicks);
                const bool returning = hasEquippedSkillNode("walking_capstone") && rawStep >= routeSteps - 1;
                const int step = returning ? routeSteps - 1 - ((zone.processedTicks / stepTicks) % routeSteps) : rawStep;
                const float stepWidth = denseSalvo ? 52.0f : 70.0f;
                const float march = static_cast<float>(step) * stepWidth;
                const float impactWidth = denseSalvo ? 54.0f : 45.0f;
                const float impactDamage = zone.valueA * (denseSalvo ? 1.10f : 1.0f);
                if (std::abs(enemy.pos.x - (zone.center.x + march - (longMarch ? 210.0f : 140.0f))) <= impactWidth) applySkillDamage(enemy, impactDamage, zone.ownerSkill);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
            } else if (zone.ownerSkill == SkillId::EventHorizon) {
                if (enemy.spatialCooldownTicks <= 0) {
                    const bool eliteEventHorizon = hasEquippedSkillNode("horizon_capstone") && enemy.maxHp >= 180.0f;
                    const float displacement = zone.valueA * (hasEquippedSkillNode("horizon_pull") ? 1.25f : 1.0f) * (eliteEventHorizon ? 1.35f : 1.0f) / static_cast<float>(TickRate);
                    enemy.pos.x = std::max(92.0f, enemy.pos.x - displacement);
                    enemy.slow = std::max(enemy.slow, zone.valueB);
                    enemy.spatialCooldownTicks = eliteEventHorizon ? TickRate / 2 : TickRate;
                    if (hasEquippedSkillNode("horizon_capstone")) emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, eliteEventHorizon ? 30.0f : 22.0f, TickRate / 2, "event_horizon");
                    ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                    ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
                }
            } else if (zone.ownerSkill == SkillId::Sanctuary) {
                for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, zone.center) <= radiusSq) { unit.hp = std::min(unit.maxHp, unit.hp + zone.valueA / static_cast<float>(TickRate)); if (hasEquippedSkillNode("sanctuary_shield")) { unit.damageReduction = std::max(unit.damageReduction, 0.35f); unit.buffTicks = std::max(unit.buffTicks, 2); } ++counters.skillHealing[static_cast<std::size_t>(zone.ownerSkill)]; }
            } else if (zone.ownerSkill == SkillId::TrapFoundry) {
                for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, zone.center) <= radiusSq) {
                    applySkillDamage(enemy, zone.valueA, zone.ownerSkill);
                    enemy.burn = std::max(enemy.burn, 0.35f);
                    enemy.burnDps = std::max(enemy.burnDps, zone.valueA);
                    enemy.burnTicks = std::max(enemy.burnTicks, TickRate / 2);
                    ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                }
            } else if (zone.ownerSkill == SkillId::Thunderhead) {
                const int before = counters.damageDealt;
                applySkillDamage(enemy, zone.valueA, zone.ownerSkill);
                enemy.shockTicks = std::max(enemy.shockTicks, TickRate * 3);
                if (enemy.soakTicks > 0) {
                    if (economyState->stormPerfectTicks <= 0) enemy.soakTicks = 0;
                    ++counters.reactionTriggers;
                    ++economyState->stormReactions;
                    economyState->resources.charge = std::min(6, economyState->resources.charge + (hasEquippedSkillNode("thunderhead_static_mastery") ? 2 : 1));
                }
                counters.skillDamage[static_cast<std::size_t>(zone.ownerSkill)] += std::max(0, counters.damageDealt - before);
                ++counters.statusApplications;
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
                emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, enemy.pos, 24.0f, zone.remainingTicks, "shock");
                zone.armTicks = std::max(1, TickRate / 2);
                if (hasEquippedSkillNode("thunderhead_capstone") && zone.remainingTicks % (TickRate / 2) == 0) {
                    const Vec2 echoCenter{zone.center.x + 34.0f, zone.center.y - 24.0f};
                    if (distanceSquared(enemy.pos, echoCenter) <= radiusSq) {
                        applySkillDamage(enemy, zone.valueA * 0.55f, zone.ownerSkill);
                        enemy.shockTicks = std::max(enemy.shockTicks, TickRate);
                        emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, echoCenter, 18.0f, zone.remainingTicks, "echo");
                    }
                }
            } else if (zone.ownerSkill == SkillId::EyeOfTheStorm) {
                const float pull = std::max(0.0f, zone.valueA) / static_cast<float>(TickRate);
                const float dx = zone.center.x - enemy.pos.x;
                const float dy = zone.center.y - enemy.pos.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.001f) {
                    enemy.pos.x += dx / distance * std::min(distance, pull);
                    enemy.pos.y += dy / distance * std::min(distance, pull);
                }
                enemy.slow = std::max(enemy.slow, 0.35f);
                if (hasEquippedSkillNode("eye_storm_roaming_mastery")) enemy.galeTicks = std::max(enemy.galeTicks, TickRate / 2);
                const bool secondReaction = hasEquippedSkillNode("eye_storm_convergence_mastery");
                if (zone.processedTicks < (secondReaction ? 2 : 1) && enemy.shockTicks > 0 && enemy.soakTicks > 0) {
                    if (economyState->stormPerfectTicks <= 0) enemy.soakTicks = 0;
                    applySkillDamage(enemy, zone.valueB * (zone.processedTicks == 0 ? 1.0f : 0.55f), zone.ownerSkill);
                    ++counters.reactionTriggers;
                    ++economyState->stormReactions;
                    economyState->resources.charge = std::min(6, economyState->resources.charge + (hasEquippedSkillNode("eye_storm_capstone") ? 4 : 2));
                    ++zone.processedTicks;
                    zone.triggered = zone.processedTicks >= (secondReaction ? 2 : 1);
                }
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            }
        }
        if (zone.ownerSkill == SkillId::WalkingBarrage) ++zone.processedTicks;
        if (zone.ownerSkill == SkillId::WalkingBarrage && hasEquippedSkillNode("walking_capstone") && !zone.triggered && zone.processedTicks >= (hasEquippedSkillNode("walking_long") ? 60 : 48)) {
            const float orbitalX = zone.center.x + (hasEquippedSkillNode("walking_long") ? 210.0f : 140.0f);
            damageArea({orbitalX, zone.center.y}, hasEquippedSkillNode("walking_dense") ? 74.0f : 64.0f, zone.valueA * 1.35f, false);
            zone.triggered = true;
            emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Hit, {orbitalX, zone.center.y}, 70.0f, 18, "orbital");
        }
        if (zone.ownerSkill == SkillId::GravityWell && zone.remainingTicks == 1 && hasEquippedSkillNode("gravity_event_capstone") && !zone.gravityAftermathTriggered) {
            zone.gravityAftermathTriggered = true;
            const float aftermathRadius = zone.radius * 0.72f;
            const float aftermathPull = std::max(18.0f, zone.valueA * 0.55f);
            for (Enemy& enemy : enemyList) {
                if (!enemy.alive || distanceSquared(enemy.pos, zone.center) > aftermathRadius * aftermathRadius) continue;
                const float dx = zone.center.x - enemy.pos.x;
                const float dy = zone.center.y - enemy.pos.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 0.001f) {
                    enemy.pos.x += dx / distance * std::min(distance, aftermathPull);
                    enemy.pos.y += dy / distance * std::min(distance, aftermathPull);
                }
                enemy.slow = std::max(enemy.slow, 0.55f);
            }
            emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Expire, zone.center, aftermathRadius, TickRate, "collapse_aftermath");
        }
        if (zone.ownerSkill == SkillId::Quarantine && zone.remainingTicks == 1 && hasEquippedSkillNode("quarantine_clean_capstone")) {
            const int payout = zone.processedTicks > 0 ? std::min(5, 1 + zone.processedTicks / TickRate) : 0;
            economyState->resources.biomass = std::min(100, economyState->resources.biomass + payout);
            if (payout > 0) emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Expire, zone.center, zone.radius, 12, "biomass_payout");
        }
        if (zone.ownerSkill == SkillId::Sanctuary && zone.remainingTicks == 1 && hasEquippedSkillNode("sanctuary_capstone")) {
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + 8);
            emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Expire, zone.center, zone.radius, 12, "hallowed_ground");
        }
        if (zone.ownerSkill == SkillId::HemorrhageField && zone.remainingTicks == 1 && hasEquippedSkillNode("hemorrhage_capstone")) {
            const int recovered = std::min(2, maxLives - lives);
            if (recovered > 0) {
                lives += recovered;
                emitSkillVisualEvent(zone.ownerSkill, SkillVisualPhase::Expire, zone.center, zone.radius, 12, "red_horizon");
            }
        }
        if (zone.remainingTicks > 0) --zone.remainingTicks;
        if (zone.remainingTicks == 0 || (zone.ownerSkill == SkillId::PhaseMine && zone.triggered)) zone.alive = false;
    }
    zones.erase(std::remove_if(zones.begin(), zones.end(), [](const SkillZone& zone) { return !zone.alive; }), zones.end());
}

void GameSim::updateAlliedUnits() {
    int survivingAllies = 0;
    int strikerMasteryRank = 0;
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) if (skillLoadoutState.skills[slot] == SkillId::VanguardDrop) strikerMasteryRank = std::max(strikerMasteryRank, std::min(skillNodeRank(slot, "vanguard_strike_mastery"), 2));
    if (skillLoadoutState.doctrineId == "beastmaster_pack") {
        for (const AlliedUnit& ally : alliedUnitsList) if (ally.alive && ally.role != "beast" && ally.role != "collector_drone") ++survivingAllies;
    }
    for (AlliedUnit& unit : alliedUnitsList) {
        if (!unit.alive) {
            if (unit.downedTicks > 0) --unit.downedTicks;
            continue;
        }
        if (unit.lifetimeTicks > 0) --unit.lifetimeTicks;
        if (unit.buffTicks > 0) --unit.buffTicks;
        else if (unit.accelerationTailTicks > 0) { --unit.accelerationTailTicks; unit.damageScale = 1.0f; unit.speedScale = 1.15f; unit.damageReduction = 0.0f; }
        else { unit.damageScale = 1.0f; unit.speedScale = 1.0f; unit.damageReduction = 0.0f; }
        if (unit.injuryTicks > 0) --unit.injuryTicks;
        if (unit.role == "beast" && economyState->beastSignatureTrait > 0) {
            switch (economyState->beastSignatureTrait) {
                case 1: unit.damageReduction = std::max(unit.damageReduction, 0.20f); break;
                case 2: unit.damageScale = std::max(unit.damageScale, 1.20f); break;
                case 3: unit.hp = std::min(unit.maxHp, unit.hp + 0.65f); break;
                case 4: unit.speedScale = std::max(unit.speedScale, 1.20f); break;
                case 5: unit.damageScale = std::max(unit.damageScale, 1.12f); break;
                default: break;
            }
        }
        const bool activeBeastAdaptation = unit.role == "beast" && (economyState->beastAdaptationTicks > 0 || economyState->beastAdaptationPersistent);
        if (activeBeastAdaptation) {
            if (economyState->beastAdaptation == 1) unit.damageReduction = std::max(unit.damageReduction, 0.28f);
            if (economyState->beastAdaptation == 2 || economyState->beastAdaptation == 5) unit.damageScale = std::max(unit.damageScale, 1.25f);
            if (economyState->beastAdaptation == 3) unit.hp = std::min(unit.maxHp, unit.hp + 0.45f);
            if (economyState->beastAdaptation == 4) unit.speedScale = std::max(unit.speedScale, 1.35f);
        }
        if (unit.role == "beast" && skillLoadoutState.doctrineId == "beastmaster_pack") {
            const int cappedAllies = std::min(6, survivingAllies);
            unit.damageScale = std::max(unit.damageScale, 1.0f + static_cast<float>(cappedAllies) * 0.05f);
            unit.speedScale = std::max(unit.speedScale, 1.0f + static_cast<float>(cappedAllies) * 0.03f);
        }
        if (unit.role == "beast" && unit.injuryTicks > 0) {
            unit.damageScale = std::min(unit.damageScale, 0.80f);
            unit.speedScale = std::min(unit.speedScale, 0.85f);
        }
        if (unit.role == "blood_golem" && economyState->bloodPulseEmpowerTicks > 0) unit.damageScale = std::max(unit.damageScale, 1.35f);
        if (unit.attackCooldownTicks > 0) --unit.attackCooldownTicks;
        if (unit.role == "collector_drone") {
            Enemy* target = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.bountyId == economyState->activeBountyId) { target = &enemy; break; }
            if (target != nullptr) {
                const float dx = target->pos.x - unit.pos.x;
                const float dy = target->pos.y - unit.pos.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance <= 42.0f) {
                    int capacityRank = 0;
                    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) if (skillLoadoutState.skills[slot] == SkillId::CollectorDrone) capacityRank = std::max(capacityRank, std::min(skillNodeRank(slot, "collector_capacity"), 3));
                    economyState->resources.trophies = std::min(100, economyState->resources.trophies + 1 + capacityRank);
                    const bool activeBountySecured = target->bountyId != 0 && target->bountyId == economyState->activeBountyId;
                    if (activeBountySecured && hasEquippedSkillNode("collector_hunter_capstone")) {
                        for (std::size_t objective = 0; objective < economyState->bountyObjectiveProgress.size(); ++objective) {
                            if (economyState->bountyObjectiveKinds[objective] < 0 || economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) continue;
                            ++economyState->bountyObjectiveProgress[objective];
                            emitSkillVisualEvent(SkillId::CollectorDrone, SkillVisualPhase::Hit, unit.pos, 28.0f, TickRate, "clean_sweep");
                            break;
                        }
                        economyState->bountyObjectivesCompleted = 0;
                        for (std::size_t objective = 0; objective < economyState->bountyObjectiveProgress.size(); ++objective) if (economyState->bountyObjectiveKinds[objective] >= 0 && economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) ++economyState->bountyObjectivesCompleted;
                    }
                    target->bountyId = 0;
                    if (hasEquippedSkillNode("collector_capstone")) {
                        for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) if (skillLoadoutState.skills[slot] == SkillId::CollectorDrone) {
                            skillCooldowns[slot] = std::max(0, skillCooldowns[slot] - TickRate * 2);
                            emitSkillVisualEvent(SkillId::CollectorDrone, SkillVisualPhase::Hit, unit.pos, 24.0f, TickRate, "master_collector");
                            break;
                        }
                    }
                    unit.alive = false;
                } else if (distance > 0.001f) {
                    unit.pos.x += dx / distance * unit.speed * unit.speedScale / static_cast<float>(TickRate);
                    unit.pos.y += dy / distance * unit.speed * unit.speedScale / static_cast<float>(TickRate);
                }
            }
            if (unit.hp <= 0.0f || unit.lifetimeTicks == 0) unit.alive = false;
            continue;
        }
        Enemy* target = nullptr;
        float bestDistance = 100000000.0f;
        if (unit.role == "beast" && economyState->beastCommandTargetId != 0 && economyState->beastCommandTicks > 0) {
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.id == economyState->beastCommandTargetId) { target = &enemy; bestDistance = distanceSquared(unit.pos, enemy.pos); break; }
        }
        if (unit.role == "striker" && strikerMasteryRank > 0) {
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.vulnerabilityTicks > 0 &&
                (target == nullptr || enemy.maxHp > target->maxHp || (enemy.maxHp == target->maxHp && enemy.id < target->id))) {
                target = &enemy;
                bestDistance = distanceSquared(unit.pos, enemy.pos);
            }
        }
        if (unit.role == "hunter") {
            for (Enemy& enemy : enemyList) if (enemy.alive && (target == nullptr || enemy.maxHp > target->maxHp || (enemy.maxHp == target->maxHp && enemy.id < target->id))) {
                target = &enemy;
                bestDistance = distanceSquared(unit.pos, enemy.pos);
            }
        }
        for (Enemy& enemy : enemyList) {
            if (target != nullptr) break;
            if (!enemy.alive) continue;
            const float distance = distanceSquared(unit.pos, enemy.pos);
            if (distance < bestDistance || (distance == bestDistance && (target == nullptr || enemy.id < target->id))) { bestDistance = distance; target = &enemy; }
        }
        if (target == nullptr) { unit.pos.x += unit.speed * unit.speedScale / static_cast<float>(TickRate); }
        else if (bestDistance <= 78.0f * 78.0f && unit.attackCooldownTicks <= 0) {
            const bool executionHit = unit.role == "striker" && hasEquippedSkillNode("vanguard_strike_capstone") && target->vulnerabilityTicks > 0;
            const bool reaperHit = unit.role == "blood_golem" && hasEquippedSkillNode("golem_reaper") && target->hp < target->maxHp * 0.65f;
            const bool legionUnit = unit.role == "soldier" || unit.role == "striker" || unit.role == "bulwark" || unit.role == "drone" || unit.role == "disruptor" || unit.role == "hunter";
            int nearbyArmy = 0;
            if (skillLoadoutState.doctrineId == "legion_swarm" && legionUnit) {
                nearbyArmy = static_cast<int>(std::count_if(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& ally) {
                    return ally.alive && (ally.role == "soldier" || ally.role == "striker" || ally.role == "bulwark" || ally.role == "drone" || ally.role == "disruptor" || ally.role == "hunter");
                }));
            }
            const float swarmScale = 1.0f + static_cast<float>(std::min(10, std::max(0, nearbyArmy - 1))) * 0.015f;
            applySkillDamage(*target, unit.damage * unit.damageScale * swarmScale * (executionHit ? 1.45f : 1.0f) * (reaperHit ? 1.35f : 1.0f), unit.ownerSkill);
            if (unit.role == "beast") economyState->beastParticipationTicks = std::min(TickRate * 600, economyState->beastParticipationTicks + 1);
            if (unit.role == "beast" && economyState->beastAdaptation == 2 && (economyState->beastAdaptationTicks > 0 || economyState->beastAdaptationPersistent)) {
                // Lightning Glands is a basic-attack trait, not a second
                // skill payload. Shock is intentionally capped to one second
                // per hit so a pet cannot create permanent status uptime.
                target->shockTicks = std::max(target->shockTicks, TickRate);
                emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, 16.0f, TickRate / 2, "lightning_glands");
            }
            if (unit.role == "hunter" && hasEquippedSkillNode("drone_hunter_capstone")) {
                target->vulnerability = std::max(target->vulnerability, 0.20f);
                target->vulnerabilityTicks = std::max(target->vulnerabilityTicks, TickRate * 3);
                emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, 24.0f, 10, "predator_lock");
            } else if (executionHit) {
                emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, 18.0f, 8, "execution_squad");
            } else {
                emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, unit.role == "disruptor" ? 18.0f : 12.0f, 4, unit.role);
            }
            const int normalAttackCooldown = unit.role == "striker" ? std::max(4, 12 - strikerMasteryRank * 2) : (unit.role == "drone" || unit.role == "disruptor" ? 12 : 20);
            unit.attackCooldownTicks = unit.nextAttackCooldownReduced ? std::max(4, normalAttackCooldown / 2) : normalAttackCooldown;
            if (unit.nextAttackCooldownReduced) {
                unit.nextAttackCooldownReduced = false;
                emitSkillVisualEvent(SkillId::RallyBeacon, SkillVisualPhase::Hit, target->pos, 16.0f, 8, "battle_hymn");
            }
            if (unit.role == "disruptor") {
                target->slow = std::max(target->slow, 2.0f);
                if (hasEquippedSkillNode("drone_disruptor_capstone")) {
                    target->signalJamTicks = std::max(target->signalJamTicks, TickRate * 2);
                    emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, 22.0f, 10, "signal_jam");
                }
            }
            if (unit.role == "bulwark") target->stun = std::max(target->stun, 0.15f);
            if (unit.role == "beast" && target->maxHp >= 180.0f && ((economyState->beastPackTakedownReady != 0) || (economyState->beastHuntPinReady != 0 && target->id == economyState->beastCommandTargetId))) {
                target->stun = std::max(target->stun, 0.60f);
                if (economyState->beastPackTakedownReady != 0) economyState->beastPackTakedownReady = 0;
                if (economyState->beastHuntPinReady != 0 && target->id == economyState->beastCommandTargetId) economyState->beastHuntPinReady = 0;
                emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, 26.0f, 12, "takedown_pin");
            }
        } else {
            const float direction = target->pos.x >= unit.pos.x ? 1.0f : -1.0f;
            unit.pos.x += direction * unit.speed * unit.speedScale / static_cast<float>(TickRate);
            unit.pos.y += (target->pos.y - unit.pos.y) * 0.08f;
        }
        const bool expires = unit.hp <= 0.0f || unit.lifetimeTicks == 0 || unit.pos.x > static_cast<float>(Width + 100) || unit.pos.x < 0.0f;
        if (expires && economyState->arsenalAmmoTicks > 0 && economyState->arsenalAmmoPayouts < 8) {
            economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + 2);
            ++economyState->arsenalAmmoPayouts;
            emitSkillVisualEvent(SkillId::ImprovisedArsenal, SkillVisualPhase::Expire, unit.pos, unit.radius, 8, "arsenal_ammunition");
        }
        if (expires && unit.role == "bulwark" && unit.hp > 0.0f && hasEquippedSkillNode("intercept_capstone")) {
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + 8);
            emitSkillVisualEvent(SkillId::Intercept, SkillVisualPhase::Expire, unit.pos, unit.radius, 12, "surviving_defender");
        }
        if (expires && unit.role == "bulwark" && hasEquippedSkillNode("vanguard_bulwark_capstone")) {
            for (AlliedUnit& ally : alliedUnitsList) if (ally.alive && ally.id != unit.id && distanceSquared(ally.pos, unit.pos) <= 110.0f * 110.0f) {
                ally.damageReduction = std::max(ally.damageReduction, 0.30f);
                ally.buffTicks = std::max(ally.buffTicks, TickRate);
            }
            emitSkillVisualEvent(SkillId::VanguardDrop, SkillVisualPhase::Expire, unit.pos, 110.0f, 14, "last_stand");
        }
        if (expires && unit.role == "blood_golem" && hasEquippedSkillNode("golem_capstone")) {
            economyState->bloodGolemReserve = std::min(8, economyState->bloodGolemReserve + 2);
            emitSkillVisualEvent(SkillId::BloodGolem, SkillVisualPhase::Expire, unit.pos, unit.radius, TickRate * 2, "hemogenesis");
        }
        const bool canEnterDownedState = (hasEquippedSkillNode("rally_medic_capstone") && unit.role != "beast") || (hasEquippedSkillNode("pack_capstone") && unit.role == "beast");
        if (expires && unit.hp <= 0.0f && canEnterDownedState) {
            unit.alive = false;
            unit.downedTicks = unit.role == "beast" ? TickRate * 5 : TickRate;
            emitSkillVisualEvent(unit.role == "beast" ? SkillId::PackCall : SkillId::RallyBeacon, SkillVisualPhase::Expire, unit.pos, unit.radius, unit.downedTicks, unit.role == "beast" ? "downed_beast" : "downed_summon");
        } else if (expires) unit.alive = false;
    }
    if (skillLoadoutState.doctrineId == "bloodbinder_symbiosis" && tickCount > 0 && tickCount % (TickRate * 2) == 0 && lives < maxLives && std::any_of(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& unit) { return unit.alive && unit.role != "collector_drone"; })) {
        ++lives;
        emitSkillVisualEvent(SkillId::LifeSiphon, SkillVisualPhase::Hit, {260.0f, 360.0f}, 32.0f, TickRate, "living_circuit");
    }
    alliedUnitsList.erase(std::remove_if(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& unit) { return !unit.alive && unit.downedTicks <= 0; }), alliedUnitsList.end());
}

void GameSim::updateBuildings() {
    for (DeployableBuilding& building : buildings) {
        if (!building.alive) continue;
        if (building.lifetimeTicks > 0) --building.lifetimeTicks;
        if (building.linkedPrimeTicks > 0) --building.linkedPrimeTicks;
        const bool accelerated = building.actionSpeedTicks > 0;
        const float actionScale = std::max(building.networkActionScale, accelerated ? std::max(1.0f, building.actionSpeedScale) : 1.0f);
        if (building.actionSpeedTicks > 0) --building.actionSpeedTicks;
        if (building.actionSpeedTicks == 0) building.actionSpeedScale = 1.0f;
        const int actionStep = accelerated ? std::max(1, static_cast<int>(std::ceil(actionScale))) : 1;
        if (building.spawnCooldownTicks > 0) building.spawnCooldownTicks = std::max(0, building.spawnCooldownTicks - actionStep);
        if (building.attackCooldownTicks > 0) building.attackCooldownTicks = std::max(0, building.attackCooldownTicks - actionStep);
        if (building.role == "wall" || building.role == "trap") {
            // Walls are damaged by contact in updateEnemies; traps only
            // resolve on entry and use attackCooldownTicks as their rearm timer.
        } else if (building.role == "barracks" || building.role == "armory") {
            if (building.spawnCooldownTicks <= 0) {
                const std::size_t before = alliedUnitsList.size();
                const bool armoryMastery = building.role == "armory" && hasEquippedSkillNode("barracks_armory_mastery");
                const float eliteScale = armoryMastery ? 1.12f : 1.0f;
                spawnAlliedUnit({building.pos.x + 18.0f, building.pos.y}, SkillId::ForwardBarracks, building.role == "armory" ? "striker" : "soldier", 240, (building.role == "armory" ? 72.0f : 48.0f) * eliteScale, (building.role == "armory" ? 18.0f : 8.0f) * eliteScale, building.role == "armory" ? 44.0f : 52.0f);
                if (alliedUnitsList.size() > before) {
                    AlliedUnit& recruit = alliedUnitsList.back();
                    if (hasEquippedSkillNode("barracks_recruit_capstone") && recruit.role == "soldier") {
                        recruit.damageScale = 1.18f;
                        recruit.speedScale = 1.20f;
                        recruit.buffTicks = TickRate;
                        emitSkillVisualEvent(SkillId::ForwardBarracks, SkillVisualPhase::Hit, recruit.pos, 24.0f, 8, "mobilisation");
                    }
                    if (hasEquippedSkillNode("barracks_armory_capstone") && recruit.role == "striker") recruit.damageScale = 1.28f;
                }
                const int baseCooldown = building.role == "armory" ? 70 : 45;
                const float recruitRate = hasEquippedSkillNode("barracks_recruit_mastery") ? 1.15f : 1.0f;
                building.spawnCooldownTicks = std::max(1, static_cast<int>(std::ceil(static_cast<float>(baseCooldown) / (actionScale * recruitRate))));
            }
        } else if (building.attackCooldownTicks <= 0) {
            Enemy* target = nullptr;
            const float gatlingRangeScale = hasEquippedSkillNode("sentry_gatling_mastery") ? 1.15f : 1.0f;
            float bestDistance = 260.0f * 260.0f * gatlingRangeScale * gatlingRangeScale * building.networkRangeScale * building.networkRangeScale;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(building.pos, enemy.pos) < bestDistance) { bestDistance = distanceSquared(building.pos, enemy.pos); target = &enemy; }
            if (target != nullptr) {
                if (building.role == "gatling") {
                    if (building.rampTargetId == target->id) {
                        building.rampTicks = std::min(6, building.rampTicks + (hasEquippedSkillNode("sentry_gatling_mastery") ? 2 : 1));
                    } else {
                        building.rampTicks = hasEquippedSkillNode("sentry_gatling_capstone") ? building.rampTicks / 2 : 0;
                        building.rampTargetId = target->id;
                    }
                }
                if (building.role == "mortar") {
                    const float mortarRadius = 78.0f * (hasEquippedSkillNode("sentry_mortar_mastery") ? 1.15f : 1.0f);
                    damageArea(target->pos, mortarRadius, 22.0f, false);
                    if (hasEquippedSkillNode("sentry_mortar_capstone")) {
                        for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, target->pos) <= mortarRadius * mortarRadius) {
                            enemy.slow = std::max(enemy.slow, hasEquippedSkillNode("sentry_mortar_mastery") ? 0.65f : 0.55f);
                            enemy.pos.y += (360.0f - enemy.pos.y) * 0.18f;
                        }
                    }
                }
                else applySkillDamage(*target, building.role == "swarm" ? 10.0f : (building.role == "gatling" ? 18.0f + static_cast<float>(building.rampTicks) * 1.5f : 18.0f), building.ownerSkill);
                emitSkillVisualEvent(building.ownerSkill, SkillVisualPhase::Hit, target->pos, building.role == "mortar" ? 78.0f * (hasEquippedSkillNode("sentry_mortar_mastery") ? 1.15f : 1.0f) : (building.role == "swarm" ? 10.0f : 18.0f), 4, building.role);
                const int baseCooldown = building.role == "mortar" ? 32 : (building.role == "swarm" ? 18 : (building.role == "gatling" ? std::max(5, 14 - building.rampTicks) : 14));
                building.attackCooldownTicks = std::max(1, static_cast<int>(std::ceil(static_cast<float>(baseCooldown) / actionScale)));
            }
        }
        if (building.lifetimeTicks == 0 && building.role == "trap" && building.ownerSkill == SkillId::MineLayer && building.charges > 0 && hasEquippedSkillNode("mine_scrap")) {
            const int recovered = std::max(1, static_cast<int>(std::round(building.effectValue * 0.20f)));
            economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + recovered);
        }
        if (building.hp <= 0.0f || building.lifetimeTicks == 0) {
            if (building.ownerSkill == SkillId::ImprovisedArsenal && economyState->arsenalInventoryTicks > 0 && economyState->arsenalInventoryScrap > 0) {
                const int recovered = std::clamp(economyState->arsenalInventoryScrap / 4, 1, 8);
                economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + recovered);
                economyState->arsenalInventoryTicks = 0;
                economyState->arsenalInventoryScrap = 0;
                emitSkillVisualEvent(SkillId::ImprovisedArsenal, SkillVisualPhase::Expire, building.pos, building.footprintRadius, 8, "final_inventory");
            }
            if (economyState->arsenalAmmoTicks > 0 && economyState->arsenalAmmoPayouts < 8) {
                economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + 2);
                ++economyState->arsenalAmmoPayouts;
                emitSkillVisualEvent(SkillId::ImprovisedArsenal, SkillVisualPhase::Expire, building.pos, building.footprintRadius, 8, "arsenal_ammunition");
            }
            if (building.hp <= 0.0f && building.role == "wall" && hasEquippedSkillNode("wall_capstone")) {
                for (Enemy& enemy : enemyList) {
                    if (!enemy.alive || distanceSquared(enemy.pos, building.pos) > 120.0f * 120.0f) continue;
                    applySkillDamage(enemy, 18.0f, SkillId::BulwarkWall);
                    enemy.stun = std::max(enemy.stun, 0.35f);
                }
                emitSkillVisualEvent(SkillId::BulwarkWall, SkillVisualPhase::Expire, building.pos, 120.0f, TickRate, "last_rampart");
            }
            if (building.role == "swarm" && hasEquippedSkillNode("turret_swarm_capstone") && economyState->turretBatteryWave != wave) {
                if (economyState->remains.size() < 64u) economyState->remains.push_back({economyState->nextRemainId++, building.pos, 3, 0, EnemyType::Swarmling, tickCount, tickCount + TickRate * 12, 0, false});
                economyState->turretBatteryWave = wave;
            }
            building.alive = false;
        }
    }
    buildings.erase(std::remove_if(buildings.begin(), buildings.end(), [](const DeployableBuilding& building) { return !building.alive; }), buildings.end());
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

void GameSim::applyDamage(Enemy& enemy, float damage, bool allowSharedAgonyEcho) {
    if (!enemy.alive) return;
    if (selectedSupport == SupportModule::CorrosionAmp && (enemy.burn > 0.0f || enemy.poison > 0.0f)) damage *= 1.20f + static_cast<float>(workshopSupportLevels[static_cast<std::size_t>(selectedSupport)]) * 0.01f;
    damage *= 1.0f + std::max(0.0f, enemy.vulnerability);
    if (enemy.vulnerabilityTicks > 0 && hasEquippedSkillNode("ruin_brittle_mastery")) damage *= 1.12f;
    const float dealt = std::max(0.0f, damage * (1.0f - enemy.damageResistance));
    enemy.hp -= dealt;
    counters.damageDealt += static_cast<int>(std::round(dealt));
    if (allowSharedAgonyEcho && dealt > 0.0f && enemy.sharedAgonyTicks > 0) {
        // Echoes are one-hop only: recursive propagation is explicitly
        // disabled on the reflected hit. The per-hit cap prevents a large
        // projectile or boss event from turning a link into an instant
        // full-group wipe.
        const float echoDamage = std::min(48.0f, dealt * (hasEquippedSkillNode("agony_chain") ? 0.80f : (hasEquippedSkillNode("agony_focus") ? 1.35f : 1.0f)) * 0.24f);
        if (echoDamage > 0.0f) {
            int echoes = 0;
            for (Enemy& linked : enemyList) {
                if (!linked.alive || linked.id == enemy.id || linked.sharedAgonyTicks <= 0) continue;
                applyDamage(linked, echoDamage, false);
                emitSkillVisualEvent(SkillId::SharedAgony, SkillVisualPhase::Hit, linked.pos, linked.radius, TickRate / 3, "agony_echo");
                if (++echoes >= 8) break;
            }
        }
    }
    if (dealt >= 24.0f && enemy.vulnerabilityTicks > 0 && hasEquippedSkillNode("ruin_brittle_capstone") && !enemy.ruinBrittleTriggered) {
        enemy.ruinBrittleTriggered = true;
        Enemy* splashTarget = nullptr;
        float splashDistance = 180.0f * 180.0f;
        for (Enemy& candidate : enemyList) {
            if (!candidate.alive || candidate.id == enemy.id) continue;
            const float distance = distanceSquared(candidate.pos, enemy.pos);
            if (distance > splashDistance) continue;
            if (splashTarget == nullptr || distance < splashDistance || (distance == splashDistance && candidate.id < splashTarget->id)) {
                splashTarget = &candidate;
                splashDistance = distance;
            }
        }
        if (splashTarget != nullptr) {
            splashTarget->vulnerability = std::max(splashTarget->vulnerability, 0.12f);
            splashTarget->vulnerabilityTicks = std::max(splashTarget->vulnerabilityTicks, TickRate * 2);
            emitSkillVisualEvent(SkillId::RuinHex, SkillVisualPhase::Hit, splashTarget->pos, 22.0f, TickRate, "armor_shatter");
        }
    }
    if (enemy.hp <= 0.0f) resolveDeath(enemy);
}

void GameSim::resolveDeath(Enemy& enemy) {
    if (!enemy.alive) return;
    enemy.alive = false;
    ++counters.kills;
    currency += enemy.boss ? 100 : 5;
    if (enemy.sharedAgonyTicks > 0 && hasEquippedSkillNode("agony_capstone")) {
        economyState->resources.discord = std::min(100, economyState->resources.discord + 3);
        emitSkillVisualEvent(SkillId::SharedAgony, SkillVisualPhase::Expire, enemy.pos, enemy.radius, TickRate, "shared_fate");
        enemy.sharedAgonyTicks = 0;
    }
    if (enemy.allegiance == 1 && economyState->usurperRiotReady != 0 && hasEquippedSkillNode("orders_capstone")) {
        economyState->resources.discord = std::min(100, economyState->resources.discord + 6);
        economyState->usurperRiotReady = 0;
        emitSkillVisualEvent(SkillId::FalseOrders, SkillVisualPhase::Expire, enemy.pos, enemy.radius, TickRate, "coup_reward");
    }
    if (enemy.allegiance == 1 && economyState->usurperCivilWarReady != 0 && hasEquippedSkillNode("riot_capstone")) {
        economyState->usurperCivilWarReady = 0;
        for (Enemy& remaining : enemyList) if (remaining.alive && remaining.confusionTicks > 0 && !remaining.boss && distanceSquared(remaining.pos, enemy.pos) <= 260.0f * 260.0f) {
            remaining.vulnerability = std::max(remaining.vulnerability, 0.16f);
            remaining.vulnerabilityTicks = std::max(remaining.vulnerabilityTicks, TickRate * 3);
        }
        emitSkillVisualEvent(SkillId::RiotWhisper, SkillVisualPhase::Expire, enemy.pos, 42.0f, TickRate, "civil_war");
    }
    if (hasSkillGroup("usurper") && hasEquippedSkillNode("treason_capstone") && enemy.allegiance == 1 && !enemy.usurperInheritedMark) {
        Enemy* successor = nullptr;
        float successorDistance = 150.0f * 150.0f;
        for (Enemy& candidate : enemyList) {
            if (!candidate.alive || candidate.boss || candidate.allegiance != 0) continue;
            const float distance = distanceSquared(enemy.pos, candidate.pos);
            if (distance > successorDistance) continue;
            if (successor == nullptr || distance < successorDistance || (distance == successorDistance && candidate.id < successor->id)) {
                successor = &candidate;
                successorDistance = distance;
            }
        }
        if (successor != nullptr) {
            successor->allegiance = 1;
            successor->allegianceTicks = TickRate * 3;
            successor->vulnerability = std::max(successor->vulnerability, 0.08f);
            successor->vulnerabilityTicks = std::max(successor->vulnerabilityTicks, TickRate * 3);
            successor->usurperInheritedMark = true;
            emitSkillVisualEvent(SkillId::TreasonMark, SkillVisualPhase::Hit, successor->pos, 24.0f, TickRate, "coup_spread");
        }
    }
    if (enemy.bountyTicks > 0) {
        if (enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId) {
            for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) {
                if (objectiveUses(content, economyState->bountyObjectiveKinds[objective], "deadline") && economyState->bountyAgeTicks <= economyState->bountyObjectiveTargets[objective]) economyState->bountyObjectiveProgress[objective] = economyState->bountyObjectiveTargets[objective];
            }
            economyState->bountyObjectivesCompleted = 0;
            for (std::size_t objective = 0; objective < economyState->bountyObjectiveKinds.size(); ++objective) if (economyState->bountyObjectiveProgress[objective] >= economyState->bountyObjectiveTargets[objective]) ++economyState->bountyObjectivesCompleted;
        }
        const bool maximumContract = enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId && economyState->bountyObjectivesCompleted >= 3;
        if (maximumContract) {
            const SkillLoadoutIdentity identity = skillLoadoutIdentity();
            if (identity.primaryGroup == "bounty_hunter" && identity.primaryCount >= 5) economyState->bountyKillingMomentumReady = 1;
        }
        int payout = enemy.boss ? 12 : 3;
        if (enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId) payout += economyState->bountyObjectivesCompleted * 3;
        if (maximumContract && hasEquippedSkillNode("wanted_capstone")) payout += 4;
        if (enemy.bountyId != 0 && enemy.bountyId == economyState->activeBountyId && hasEquippedSkillNode("wanted_isolation_mastery")) {
            bool isolated = true;
            for (const Enemy& nearby : enemyList) if (nearby.alive && nearby.id != enemy.id && distanceSquared(nearby.pos, enemy.pos) <= 110.0f * 110.0f) { isolated = false; break; }
            if (isolated) {
                payout += enemy.boss ? 4 : 2;
                emitSkillVisualEvent(SkillId::Wanted, SkillVisualPhase::Hit, enemy.pos, 30.0f, TickRate, "isolation_mastery");
            }
        }
        economyState->resources.trophies = std::min(100, economyState->resources.trophies + payout);
        if (enemy.bountyId == economyState->activeBountyId) {
            if (skillLoadoutState.doctrineId == "bounty_hunter_collector") economyState->bountyCollectorReady = 1;
            if (hasEquippedSkillNode("exploit_capstone") && !enemy.weaknessTag.empty()) {
                economyState->bountyRetainedWeakness = enemy.weaknessTag;
                economyState->bountyRetainedWeaknessReady = 1;
                emitSkillVisualEvent(SkillId::ExploitWeakness, SkillVisualPhase::Hit, enemy.pos, 28.0f, TickRate, "perfect_counter_armed");
            }
            economyState->activeBountyId = 0;
            economyState->activeBountyTargetId = 0;
            economyState->bountyAgeTicks = 0;
            economyState->bountyIsolationTicks = 0;
            economyState->bountyObjectivesCompleted = 0;
            economyState->bountyTagMask = 0;
            economyState->bountyObjectiveKinds = {{-1, -1, -1}};
            economyState->bountyObjectiveProgress = {{0, 0, 0}};
            economyState->bountyObjectiveTargets = {{1, 1, 1}};
            economyState->bountyMomentumObjective = -1;
        }
    }
    const std::uint8_t supportLevel = workshopSupportLevels[static_cast<std::size_t>(selectedSupport)];
    if (selectedSupport == SupportModule::CreditRelay) currency += enemy.boss ? 30 + supportLevel * 2 : 2 + supportLevel;
    if (selectedSupport == SupportModule::RepairDrones && counters.kills % std::max(3, 8 - static_cast<int>(supportLevel) / 4) == 0) lives = std::min(maxLives, lives + 1);
    if (hasSkull(Skull::Greed)) currency += enemy.boss ? content.skullBossCurrencyBonus[static_cast<std::size_t>(Skull::Greed)] : content.skullCurrencyBonus[static_cast<std::size_t>(Skull::Greed)];
    if (hasSkillGroup("salvager") && !enemy.boss) {
        const int remainValue = economyState->starterBundleWave != wave ? 6 : 2;
        if (economyState->starterBundleWave != wave) economyState->starterBundleWave = wave;
        createBattlefieldRemain(enemy, remainValue);
    }
    if (hasSkillGroup("plaguewright") && !enemy.boss && enemy.infectionTicks > 0) {
        int biomassValue = 1;
        for (const PlagueMutationDefinition& mutation : content.plagueMutations) if (mutation.strain == enemy.infectionStrain) { biomassValue = mutation.biomassValue; break; }
        if (hasEquippedSkillNode("patient_capstone")) biomassValue = std::max(biomassValue, 2);
        createBattlefieldRemain(enemy, 0, biomassValue);
        const PlagueMutationDefinition* mutation = nullptr;
        for (const PlagueMutationDefinition& candidate : content.plagueMutations) if (candidate.strain == enemy.infectionStrain) { mutation = &candidate; break; }
        const bool symbioticCarrier = skillLoadoutState.doctrineId == "plaguewright_symbiotic" && economyState->plagueSymbioticWave != wave;
        if ((symbioticCarrier || (mutation != nullptr && mutation->behavior == "husk")) && enemy.infectionGeneration <= 2 && alliedUnitsList.size() < content.maxAlliedUnits) {
            // Symbiotic hosts produce one bounded, temporary husk. Husks are
            // deliberately ordinary allies: they cannot create remains,
            // spread infection, or create further husks.
            const float doctrineScale = symbioticCarrier ? 1.0f : 0.0f;
            const float huskHealth = std::max(18.0f, enemy.maxHp * (doctrineScale > 0.0f ? 0.22f : (hasEquippedSkillNode("mutation_symbiotic_capstone") ? 0.26f : 0.18f)));
            const float huskDamage = std::max(4.0f, enemy.maxHp * (doctrineScale > 0.0f ? 0.04f : (hasEquippedSkillNode("mutation_symbiotic_capstone") ? 0.05f : 0.035f)));
            spawnAlliedUnit(enemy.pos, SkillId::Mutation, "plague_husk", TickRate * 8, huskHealth, huskDamage, 38.0f);
            ++counters.skillSummons[static_cast<std::size_t>(SkillId::Mutation)];
            if (symbioticCarrier) economyState->plagueSymbioticWave = wave;
        }
    }
    if (hasSkillGroup("beastmaster") && !enemy.boss) createBattlefieldRemain(enemy, 1, 0);
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
        if (hasUpgrade(Upgrade::FreezingBlast)) { best->slow = upgradeValueA(Upgrade::FreezingBlast); ++counters.reactionTriggers; }
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
                ++counters.statusApplications;
            }
            if (hasUpgrade(Upgrade::PoisonCoil)) {
                enemy.poison = upgradeValueA(Upgrade::PoisonCoil);
                enemy.poisonDps = upgradeValueB(Upgrade::PoisonCoil) + wave * 0.5f;
                enemy.poisonTicks = static_cast<int>(TickRate * upgradeValueA(Upgrade::PoisonCoil));
                ++counters.statusApplications;
            }
            if (hasUpgrade(Upgrade::FreezingBlast)) { enemy.slow = upgradeValueA(Upgrade::FreezingBlast); ++counters.statusApplications; }
            if (hasUpgrade(Upgrade::ChainLightning)) {
                const bool frozenChain = hasUpgrade(Upgrade::FreezingBlast) && enemy.slow > 0.0f;
                if (frozenChain) ++counters.reactionTriggers;
                chainDamage(enemy.pos, enemy.id, projectile.damage * (frozenChain ? upgradeValueB(Upgrade::ChainLightning) : upgradeValueA(Upgrade::ChainLightning)));
            }
            if (hasUpgrade(Upgrade::Shockwave) && projectile.explosive) {
                for (Enemy& nearby : enemyList) {
                    if (nearby.alive && nearby.id != enemy.id && distanceSquared(nearby.pos, enemy.pos) <= upgradeValueB(Upgrade::Shockwave) * upgradeValueB(Upgrade::Shockwave)) nearby.stun = upgradeValueA(Upgrade::Shockwave);
                }
            }
            if (projectile.explosive) damageArea(enemy.pos, hasUpgrade(Upgrade::ClusterBombs) ? upgradeValueA(Upgrade::ClusterBombs) : 68.0f, projectile.damage * (hasUpgrade(Upgrade::ClusterBombs) ? upgradeValueB(Upgrade::ClusterBombs) : 0.45f), hasUpgrade(Upgrade::FireballShells));
            if (hasUpgrade(Upgrade::BurningShot) && hasUpgrade(Upgrade::WindShear)) {
                // Fire + wind: a secondary tornado-like ring that burns nearby targets.
                ++counters.reactionTriggers;
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
    const float workshopScale = 1.0f + static_cast<float>(workshopModuleLevels[static_cast<std::size_t>(selectedWeapon)]) * 0.01f;
    const float evolutionScale = ultimateBoostTicks > 0 ? 1.25f : 1.0f;
    const float damageScale = workshopScale * evolutionScale * content.chassisWeaponDamageScale[static_cast<std::size_t>(selectedChassis)] * (1.0f + (hasUpgrade(Upgrade::Scavenger) ? upgradeValueA(Upgrade::Scavenger) : 0.0f)) * (1.0f + (hasUpgrade(Upgrade::SteadyAim) ? upgradeValueA(Upgrade::SteadyAim) : 0.0f));
    const float cooldownScale = content.chassisWeaponCooldownScale[static_cast<std::size_t>(selectedChassis)];
    switch (selectedWeapon) {
        case Weapon::RapidFire:
            projectile.damage = content.weaponDamage[0] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[0], dy / length * content.projectileSpeed[0]};
            projectile.pierces = hasUpgrade(Upgrade::PiercingShots) ? static_cast<int>(upgradeValueA(Upgrade::PiercingShots)) : 0;
            projectile.bounces = hasUpgrade(Upgrade::Ricochet) ? static_cast<int>(upgradeValueA(Upgrade::Ricochet)) : 0;
            fireCooldown = hasUpgrade(Upgrade::Overclock) ? std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[0]) * cooldownScale / upgradeValueA(Upgrade::Overclock))) : std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[0]) * cooldownScale));
            break;
        case Weapon::ExplosiveCannon:
            projectile.damage = content.weaponDamage[1] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[1], dy / length * content.projectileSpeed[1]};
            projectile.explosive = true;
            fireCooldown = std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[1]) * cooldownScale));
            break;
        case Weapon::ArcaneBeam:
            damageArea(target->pos, 35.0f, content.weaponDamage[2] * damageScale, false);
            if (hasUpgrade(Upgrade::ChainLightning)) chainDamage(target->pos, target->id, content.weaponDamage[2] * (target->slow > 0.0f ? upgradeValueB(Upgrade::ChainLightning) : upgradeValueA(Upgrade::ChainLightning)));
            fireCooldown = std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[2]) * cooldownScale));
            if (bulletStormTicks > 0) fireCooldown = std::max(1, fireCooldown / 3);
            ++counters.shotsFired;
            return;
        case Weapon::FrostBlaster:
            projectile.damage = content.weaponDamage[3] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[3], dy / length * content.projectileSpeed[3]};
            projectile.explosive = true;
            fireCooldown = std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[3]) * cooldownScale));
            break;
        case Weapon::SniperRailgun:
            projectile.damage = content.weaponDamage[4] * damageScale;
            projectile.velocity = {dx / length * content.projectileSpeed[4], dy / length * content.projectileSpeed[4]};
            projectile.pierces = hasUpgrade(Upgrade::PiercingShots) ? static_cast<int>(upgradeValueA(Upgrade::PiercingShots) + 1.0f) : 0;
            fireCooldown = hasUpgrade(Upgrade::Overclock) ? std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[4]) * cooldownScale / upgradeValueA(Upgrade::Overclock))) : std::max(1, static_cast<int>(static_cast<float>(content.weaponCooldown[4]) * cooldownScale));
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
                            Upgrade::WindShear, Upgrade::PoisonCoil, Upgrade::SteadyAim};
    const auto eligible = [this, &pool](Upgrade candidate) {
        const std::size_t index = static_cast<std::size_t>(candidate);
        if (index >= content.upgradeMetadata.size()) return false;
        const ContentMetadata& metadata = content.upgradeMetadata[index];
        const int ownedCount = static_cast<int>(std::count(ownedUpgrades.begin(), ownedUpgrades.end(), candidate));
        if (ownedCount >= metadata.maxStacks) return false;
        for (const std::string& prerequisite : metadata.prerequisites) {
            bool satisfied = false;
            for (Upgrade owned : ownedUpgrades) {
                if (upgradeIdMatches(owned, prerequisite)) { satisfied = true; break; }
            }
            if (!satisfied) return false;
        }
        for (const std::string& exclusion : metadata.exclusions) {
            for (Upgrade owned : ownedUpgrades) if (upgradeIdMatches(owned, exclusion)) return false;
            for (std::size_t choiceIndex = 0; choiceIndex < std::size(pool); ++choiceIndex) {
                if (std::find(choices.begin(), choices.end(), pool[choiceIndex]) != choices.end() &&
                    upgradeIdMatches(pool[choiceIndex], exclusion)) return false;
            }
        }
        return true;
    };
    const auto threatAffinity = [this](Upgrade upgrade) {
        const auto& threats = content.waveEnemyTypeWeight[static_cast<std::size_t>(std::clamp(wave - 1, 0, 9))];
        const float runners = threats[static_cast<std::size_t>(EnemyType::Runner)];
        const float tanks = threats[static_cast<std::size_t>(EnemyType::Tank)];
        const float shielded = threats[static_cast<std::size_t>(EnemyType::Shielded)];
        const float swarms = threats[static_cast<std::size_t>(EnemyType::Swarmling)];
        const float teleporters = threats[static_cast<std::size_t>(EnemyType::Teleporter)];
        if ((upgrade == Upgrade::ClusterBombs || upgrade == Upgrade::ChainLightning || upgrade == Upgrade::BlackHole) && swarms >= 10.0f) return 1.45f;
        if ((upgrade == Upgrade::FreezingBlast || upgrade == Upgrade::Shockwave) && runners + teleporters >= 20.0f) return 1.40f;
        if ((upgrade == Upgrade::PiercingShots || upgrade == Upgrade::Ricochet || upgrade == Upgrade::BurningShot || upgrade == Upgrade::PoisonCoil) && tanks + shielded >= 20.0f) return 1.35f;
        return 1.0f;
    };
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
                case Upgrade::SteadyAim:
                    return 1.0f;
            }
            return 1.0f;
        };
        bool selected = false;
        for (std::size_t index = 0; index < 15; ++index) {
            const Upgrade candidate = pool[index];
            if (eligible(candidate) && std::find(choices.begin(), choices.end(), candidate) == choices.end()) totalWeight += content.upgradeWeight[index] * affinity(candidate) * threatAffinity(candidate);
        }
        if (totalWeight <= 0.0f) break;
        float roll = random01() * totalWeight;
        for (std::size_t index = 0; index < 15; ++index) {
            const Upgrade candidate = pool[index];
            if (!eligible(candidate) || std::find(choices.begin(), choices.end(), candidate) != choices.end()) continue;
            roll -= content.upgradeWeight[index] * affinity(candidate) * threatAffinity(candidate);
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

bool GameSim::chooseOathReward(int choice) {
    if (choice <= 0 || (choice != economyState->oathRewardChoiceA && choice != economyState->oathRewardChoiceB)) return false;
    if (choice == 1) economyState->resources.resolve = std::min(100, economyState->resources.resolve + 12);
    else if (choice == 2) economyState->guardianWardTicks = std::max(economyState->guardianWardTicks, TickRate * 2);
    else return false;
    emitSkillVisualEvent(SkillId::GuardianWard, SkillVisualPhase::Hit, {260.0f, 360.0f}, 48.0f, TickRate, choice == 1 ? "exemplar_resolve" : "exemplar_ward");
    economyState->oathRewardChoiceA = 0;
    economyState->oathRewardChoiceB = 0;
    return true;
}

bool GameSim::rerollUpgradeChoices() {
    if (!upgradeChoicePending || upgradeRerolls <= 0) return false;
    --upgradeRerolls;
    upgradeChoicePending = false;
    createUpgradeChoices();
    return upgradeChoicePending;
}

void GameSim::activateUltimate() {
    if (ultimateCooldown > 0 || gameOver || victory || upgradeChoicePending) return;
    ++counters.ultimates;
    ultimateCooldown = ultimateMaxCooldown;
    const SkillLoadoutIdentity ultimateIdentity = skillLoadoutIdentity();
    if (ultimateIdentity.primaryCount >= 3 && ultimateIdentity.primaryGroup != "stormcaller") {
        const std::string& group = ultimateIdentity.primaryGroup;
        if (group == "arcanist") {
            damageArea({650.0f, 360.0f}, 420.0f, 150.0f + static_cast<float>(wave) * 18.0f, false);
            economyState->arcanistCadence = 0;
        } else if (group == "legion") {
            for (int index = 0; index < 5; ++index) spawnAlliedUnit({360.0f + static_cast<float>(index * 18), 340.0f + static_cast<float>((index % 2) * 35)}, SkillId::VanguardDrop, "soldier", TickRate * 8, 56.0f, 14.0f, 60.0f);
            economyState->resources.bond = std::min(100, economyState->resources.bond + 4);
        } else if (group == "bloodbinder") {
            economyState->bloodEclipseTicks = TickRate * 10;
            economyState->bloodEclipseHealth = 50;
            damageArea({650.0f, 360.0f}, 330.0f, 150.0f + static_cast<float>(wave) * 12.0f, false);
            emitSkillVisualEvent(SkillId::BloodLance, SkillVisualPhase::Cast, {650.0f, 360.0f}, 330.0f, economyState->bloodEclipseTicks, "red_eclipse");
        } else if (group == "usurper") {
            int converted = 0;
            for (Enemy& enemy : enemyList) if (enemy.alive && !enemy.boss && converted < 3) { enemy.allegiance = 1; enemy.allegianceTicks = TickRate * 8; enemy.confusionTicks = TickRate * 4; ++converted; }
            economyState->resources.discord = std::min(100, economyState->resources.discord + converted * 5);
        } else if (group == "architect") {
            spawnBuilding({520.0f, 360.0f}, SkillId::BulwarkWall, "wall", TickRate * 12, 160.0f);
            spawnBuilding({700.0f, 300.0f}, SkillId::SentryFabricator, "sentry", TickRate * 12, 100.0f);
        } else if (group == "chronomancer") {
            economyState->timeFractureTicks = std::max(economyState->timeFractureTicks, TickRate * 8);
            for (Enemy& enemy : enemyList) if (enemy.alive) enemy.temporalDelayTicks = std::max(enemy.temporalDelayTicks, TickRate * 4);
            economyState->resources.paradox = std::min(100, economyState->resources.paradox + 20);
        } else if (group == "bounty_hunter") {
            Enemy* target = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && (target == nullptr || enemy.hp > target->hp || (enemy.hp == target->hp && enemy.id < target->id))) target = &enemy;
            if (target != nullptr) {
                target->bountyTicks = TickRate * 10;
                target->bountyId = economyState->nextBountyId++;
                economyState->activeBountyId = target->bountyId;
                economyState->activeBountyTargetId = target->id;
                economyState->bountyAgeTicks = 0;
                economyState->bountyIsolationTicks = 0;
                economyState->bountyObjectivesCompleted = 0;
                economyState->bountyTagMask = 0;
                assignBountyObjectives(content, seed, target->id, target->boss, *economyState);
                applyDamage(*target, 180.0f);
            }
        } else if (group == "plaguewright") {
            Enemy* primeHost = nullptr;
            for (Enemy& enemy : enemyList) if (enemy.alive && enemy.infectionTicks > 0 &&
                (primeHost == nullptr || enemy.infectionStacks > primeHost->infectionStacks ||
                 (enemy.infectionStacks == primeHost->infectionStacks && enemy.id < primeHost->id))) primeHost = &enemy;
            economyState->pandemicTicks = TickRate * 12;
            economyState->pandemicPrimeHostId = primeHost == nullptr ? 0 : primeHost->id;
            economyState->pandemicPrimeStrain = primeHost == nullptr ? 1 : std::clamp(primeHost->infectionStrain, 1, 4);
            for (Enemy& enemy : enemyList) if (enemy.alive && (primeHost == nullptr || enemy.id == primeHost->id)) {
                enemy.infectionTicks = std::max(enemy.infectionTicks, TickRate * 12);
                enemy.infectionStacks = std::min(5, enemy.infectionStacks + 1);
                enemy.infectionStrain = economyState->pandemicPrimeStrain;
                enemy.pandemicSpreadUsed = false;
            }
            economyState->resources.biomass = std::min(100, economyState->resources.biomass + 12);
        } else if (group == "salvager") {
            economyState->resources.scrap = std::min(economyState->resources.scrapCarryCap, economyState->resources.scrap + 18);
            spawnBuilding({650.0f, 360.0f}, SkillId::ImprovisedArsenal, "mortar", TickRate * 8, 90.0f);
        } else if (group == "beastmaster") {
            const bool apexEvolution = hasEquippedSkillNode("beast_capstone");
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && unit.role == "beast") {
                unit.damageScale = std::max(unit.damageScale, apexEvolution ? 2.35f : 2.0f);
                unit.speedScale = std::max(unit.speedScale, apexEvolution ? 1.65f : 1.5f);
                if (apexEvolution) unit.damageReduction = std::max(unit.damageReduction, 0.28f);
                unit.buffTicks = std::max(unit.buffTicks, TickRate * (apexEvolution ? 12 : 8));
                emitSkillVisualEvent(SkillId::AlphaBeast, SkillVisualPhase::Hit, unit.pos, unit.radius * (apexEvolution ? 1.5f : 1.0f), unit.buffTicks, apexEvolution ? "apex_evolution" : "beast_evolution");
            }
            economyState->resources.bond = std::min(100, economyState->resources.bond + 20);
        } else if (group == "artillerist") {
            for (Enemy& enemy : enemyList) if (enemy.alive) { enemy.predictedTicks = TickRate * 5; applyDamage(enemy, 90.0f); }
            economyState->resources.targetingData = std::min(100, economyState->resources.targetingData + 20);
        } else if (group == "void_shepherd") {
            for (Enemy& enemy : enemyList) if (enemy.alive) { enemy.pos.x = std::max(92.0f, enemy.pos.x - 240.0f); enemy.banishedTicks = std::max(enemy.banishedTicks, TickRate); }
            economyState->resources.instability = std::min(100, economyState->resources.instability + 25);
        } else if (group == "oathkeeper") {
            lives = std::min(maxLives, lives + 2);
            economyState->resources.resolve = std::min(100, economyState->resources.resolve + 25);
            economyState->guardianWardTicks = std::max(economyState->guardianWardTicks, TickRate * 8);
        } else if (group == "fatebinder") {
            economyState->fateBoostTicks = std::max(economyState->fateBoostTicks, TickRate * 8);
            economyState->fateHouseTicks = std::max(economyState->fateHouseTicks, TickRate * 12);
            economyState->resources.fate = std::min(100, economyState->resources.fate + 25);
        }
        emitSkillVisualEvent(SkillId::ResonancePulse, SkillVisualPhase::Cast, {650.0f, 360.0f}, 300.0f, TickRate * 3, group);
        return;
    }
    if (ultimateIdentity.primaryGroup == "stormcaller" && ultimateIdentity.primaryCount >= 3) {
        // Stormcaller replaces the generic payload with its authored ultimate
        // once the loadout has enough elemental identity to support it.
        economyState->stormPerfectTicks = TickRate * 10;
        economyState->resources.charge = std::min(6, economyState->resources.charge + 2);
        economyState->stormReactionChain = 0;
        economyState->stormLastReaction = 0;
        emitSkillVisualEvent(SkillId::EyeOfTheStorm, SkillVisualPhase::Cast, {650.0f, 360.0f}, 260.0f, economyState->stormPerfectTicks, "perfect_storm");
        return;
    }
    const float damage = (125.0f + wave * 22.0f) * content.ultimateDamageScale[static_cast<std::size_t>(selectedUltimate)] * ultimateModuleScale(content, selectedUltimateModule, selectedUltimate, false);
    switch (selectedUltimate) {
        case Ultimate::MeteorRain:
            if (selectedEvolution == UltimateEvolution::ExtinctionSpear) {
                Enemy* target = nullptr;
                for (Enemy& enemy : enemyList) if (target == nullptr || enemy.hp > target->hp) target = &enemy;
                if (target != nullptr) damageArea(target->pos, 110.0f, damage * 3.5f, false);
            } else if (selectedEvolution == UltimateEvolution::ShatteredSky) {
                damageArea({280.0f, 240.0f}, 190.0f, damage * 0.65f, true);
                damageArea({520.0f, 450.0f}, 190.0f, damage * 0.65f, true);
                damageArea({780.0f, 250.0f}, 190.0f, damage * 0.65f, true);
                damageArea({1020.0f, 430.0f}, 190.0f, damage * 0.65f, true);
            } else {
                const float impact = selectedEvolution == UltimateEvolution::SolarAftermath ? damage * 0.82f : damage;
                damageArea({420.0f, 300.0f}, 260.0f, impact, true);
                damageArea({680.0f, 420.0f}, 260.0f, impact, true);
                damageArea({880.0f, 290.0f}, 220.0f, impact * 0.8f, true);
            }
            break;
        case Ultimate::BulletStorm:
            if (selectedEvolution == UltimateEvolution::ExecutionProtocol) {
                Enemy* target = nullptr;
                for (Enemy& enemy : enemyList) if (target == nullptr || enemy.hp > target->hp) target = &enemy;
                if (target != nullptr) applyDamage(*target, damage * 4.0f);
            } else {
                bulletStormTicks = TickRate * 5;
                if (selectedEvolution == UltimateEvolution::ResonantArsenal) ultimateBoostTicks = TickRate * 5;
                if (selectedEvolution == UltimateEvolution::SuppressiveGrid) for (Enemy& enemy : enemyList) { enemy.slow = 4.0f; enemy.stun = 0.75f; }
                damageArea({650.0f, 360.0f}, 480.0f, damage * (selectedEvolution == UltimateEvolution::SuppressiveGrid ? 0.15f : 0.25f), false);
            }
            break;
        case Ultimate::AbsoluteZero:
            for (Enemy& enemy : enemyList) {
                enemy.slow = selectedEvolution == UltimateEvolution::PermafrostEngine ? 10.0f : 6.0f;
                enemy.stun = 3.0f;
                applyDamage(enemy, damage * (selectedEvolution == UltimateEvolution::BrittleSingularity ? 0.7f : 0.35f));
                if (selectedEvolution == UltimateEvolution::ColdConductor) chainDamage(enemy.pos, enemy.id, damage * 0.35f);
            }
            break;
        case Ultimate::GravityShift:
            for (Enemy& enemy : enemyList) { enemy.pos.x = std::max(92.0f, enemy.pos.x - (selectedEvolution == UltimateEvolution::ChronoReversal ? 300.0f : 180.0f)); enemy.stun = 1.5f; }
            if (selectedEvolution == UltimateEvolution::MassDriver) {
                Enemy* target = nullptr;
                for (Enemy& enemy : enemyList) if (target == nullptr || enemy.hp > target->hp) target = &enemy;
                if (target != nullptr) applyDamage(*target, damage * 4.0f);
            } else {
                damageArea({650.0f, 360.0f}, 260.0f, damage * 0.45f, selectedEvolution == UltimateEvolution::EventHorizon);
            }
            break;
        case Ultimate::EnergySurge:
            if (selectedEvolution == UltimateEvolution::OverdriveLink) {
                ultimateBoostTicks = TickRate * 6;
            } else if (selectedEvolution == UltimateEvolution::TerminalDischarge) {
                Enemy* target = nullptr;
                for (Enemy& enemy : enemyList) if (target == nullptr || enemy.hp > target->hp) target = &enemy;
                if (target != nullptr) applyDamage(*target, damage * 5.0f);
            } else {
                damageArea({650.0f, 360.0f}, 520.0f, damage * (selectedEvolution == UltimateEvolution::ChainReactor ? 0.9f : 1.15f), selectedWeapon == Weapon::ExplosiveCannon);
                if (selectedEvolution == UltimateEvolution::ChainReactor) for (const Enemy& enemy : enemyList) chainDamage(enemy.pos, enemy.id, damage * 0.2f);
            }
            currency += 25;
            break;
    }
}

void GameSim::tick() {
    if (gameOver || victory || upgradeChoicePending) return;
    ++tickCount;
    counters.ticks = tickCount;
    updateSkills();
    if (economyState->resources.paradox > 0 && !(skillLoadoutState.doctrineId == "chronomancer_anchor" && tickCount % 2 != 0)) --economyState->resources.paradox;
    if (economyState->resources.instability > 0) --economyState->resources.instability;
    if (economyState->resources.biomass > 0 && tickCount % (TickRate * 8) == 0) --economyState->resources.biomass;
    if (economyState->resources.fate > 0 && tickCount % (TickRate * 10) == 0) --economyState->resources.fate;
    if (economyState->resources.bond > 0 && tickCount % (TickRate * 12) == 0) --economyState->resources.bond;
    if (economyState->resources.charge > 0 && tickCount % (TickRate * 5) == 0 && economyState->stormReactions == 0) --economyState->resources.charge;
    if (hasSkillGroup("architect") && tickCount % 15 == 0) economyState->resources.buildSupply = std::min(economyState->resources.buildSupplyCap, economyState->resources.buildSupply + (economyState->architectNetworkReady != 0 ? 2 : 1));
    if (economyState->stormPerfectTicks > 0) --economyState->stormPerfectTicks;
    if (economyState->pandemicTicks > 0) --economyState->pandemicTicks;
    if (economyState->pandemicTicks <= 0) {
        economyState->pandemicPrimeStrain = 0;
        economyState->pandemicPrimeHostId = 0;
    }
    if (economyState->fateBoostTicks > 0) --economyState->fateBoostTicks;
    if (economyState->fateHouseTicks > 0) --economyState->fateHouseTicks;
    if (economyState->guardianWardTicks > 0) --economyState->guardianWardTicks;
    if (economyState->timeFractureTicks > 0) --economyState->timeFractureTicks;
    if (economyState->chronomancerDebtBurstTicks > 0) --economyState->chronomancerDebtBurstTicks;
    if (economyState->beastAdaptationTicks > 0 && !economyState->beastAdaptationPersistent) --economyState->beastAdaptationTicks;
    if (economyState->beastPounceEmpoweredTicks > 0) --economyState->beastPounceEmpoweredTicks;
    if (economyState->beastCommandTicks > 0) --economyState->beastCommandTicks;
    if (economyState->beastCommandTicks <= 0) economyState->beastCommandTargetId = 0;
    if (economyState->bloodEclipseTicks > 0) --economyState->bloodEclipseTicks;
    if (economyState->bloodEclipseTicks <= 0) economyState->bloodEclipseHealth = 0;
    updateSkillVisualEvents();
    if (ultimateCooldown > 0) --ultimateCooldown;
    if (bulletStormTicks > 0) --bulletStormTicks;
    if (ultimateBoostTicks > 0) --ultimateBoostTicks;
    spawnWaveIfNeeded();
    if ((endlessMode || wave <= 10) && spawnedThisWave < waveSpawnTarget) {
        if (spawnCooldown <= 0) {
            const bool bossWave = wave >= 10 && wave % 5 == 0;
            spawnEnemy(bossWave && spawnedThisWave == 0);
            spawnCooldown = std::max(4, content.waveSpawnInterval[static_cast<std::size_t>(std::clamp(wave, 1, 10) - 1)]);
        } else --spawnCooldown;
    }
    updateEnemies();
    updateSkillZones();
    updateBuildings();
    updateAlliedUnits();
    updateProjectiles();
    fireWeapon();
    updateEconomyEntities();
    if (economyState->activeVowTicks > 0) {
        if (lives < economyState->vowStartingLives) {
            economyState->activeVowTicks = 0;
            economyState->activeVowKind = -1;
            economyState->activeVowProgress = 0;
        } else {
            --economyState->activeVowTicks;
            economyState->activeVowProgress = std::clamp(economyState->activeVowTarget - economyState->activeVowTicks, 0, economyState->activeVowTarget);
            if (economyState->activeVowTicks == 0) {
                ++economyState->vowsCompleted;
                economyState->resources.resolve = std::min(100, economyState->resources.resolve + (hasEquippedSkillNode("ward_capstone") ? 18 : 12));
                const int completedKind = economyState->activeVowKind;
                if (completedKind == 2 && hasEquippedSkillNode("challenge_clean")) {
                    economyState->resources.resolve = std::min(100, economyState->resources.resolve + 6);
                    emitSkillVisualEvent(SkillId::Challenge, SkillVisualPhase::Hit, {260.0f, 360.0f}, 36.0f, TickRate, "clean_defense");
                }
                if (completedKind >= 0 && completedKind < 3) {
                    economyState->oathVowTypeMask |= 1 << completedKind;
                    int distinctTypes = 0;
                    for (int mask = economyState->oathVowTypeMask; mask != 0; mask >>= 1) distinctTypes += mask & 1;
                    const SkillLoadoutIdentity identity = skillLoadoutIdentity();
                    if (identity.primaryGroup == "oathkeeper" && identity.primaryCount >= 5 && distinctTypes >= 3 && economyState->oathRewardChoiceA == 0) {
                        economyState->oathExemplarReady = 1;
                        emitSkillVisualEvent(SkillId::GuardianWard, SkillVisualPhase::Hit, {260.0f, 360.0f}, 48.0f, TickRate * 2, "exemplar_ready");
                    }
                }
                economyState->activeVowKind = -1;
            }
        }
    }
    if (enemyList.empty() && spawnedThisWave >= waveSpawnTarget) {
        if (!endlessMode && wave >= 10) { victory = true; counters.wave = wave; return; }
        if (hasSkillGroup("beastmaster") && skillLoadoutIdentity().primaryGroup == "beastmaster" && skillLoadoutIdentity().primaryCount >= 5 && economyState->beastSignatureTrait == 0 && economyState->beastParticipationTicks >= std::max(1, TickRate / 3)) {
            const bool petSurvived = std::any_of(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& unit) { return unit.alive && unit.role == "beast"; });
            if (petSurvived) {
                economyState->beastSignatureTrait = 1 + static_cast<int>((seed + static_cast<std::uint32_t>(wave)) % 5u);
                economyState->beastSignatureWave = wave;
                emitSkillVisualEvent(SkillId::AlphaBeast, SkillVisualPhase::Hit, {260.0f, 360.0f}, 34.0f, TickRate * 2, "signature_trait");
            }
        }
        economyState->bloodHeartFragments = 0;
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
    add(static_cast<std::uint32_t>(lives)); add(static_cast<std::uint32_t>(maxLives)); add(static_cast<std::uint32_t>(currency));
    add(static_cast<std::uint32_t>(spawnedThisWave)); add(static_cast<std::uint32_t>(waveSpawnTarget));
    add(static_cast<std::uint32_t>(spawnCooldown)); add(static_cast<std::uint32_t>(fireCooldown));
    add(static_cast<std::uint32_t>(ultimateCooldown)); add(static_cast<std::uint32_t>(ultimateMaxCooldown)); add(static_cast<std::uint32_t>(bulletStormTicks)); add(static_cast<std::uint32_t>(ultimateBoostTicks));
    add(static_cast<std::uint32_t>(nextEnemyId)); add(static_cast<std::uint32_t>(mutationStrainSelection));
    addBool(gameOver); addBool(victory); addBool(upgradeChoicePending); addBool(automaticUltimate); addBool(endlessMode);
    add(static_cast<std::uint32_t>(upgradeRerolls));
    add(static_cast<std::uint32_t>(selectedWeapon)); add(static_cast<std::uint32_t>(selectedChassis)); add(static_cast<std::uint32_t>(selectedSupport)); add(static_cast<std::uint32_t>(selectedSkull)); add(static_cast<std::uint32_t>(selectedSkulls));
    add(static_cast<std::uint32_t>(selectedUltimate)); add(static_cast<std::uint32_t>(selectedEvolution)); add(static_cast<std::uint32_t>(selectedUltimateModule)); add(static_cast<std::uint32_t>(selectedArena));
    add(static_cast<std::uint32_t>(nextSkillCastSequence));
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) { add(static_cast<std::uint32_t>(skillLoadoutState.skills[slot])); add(static_cast<std::uint32_t>(skillCooldowns[slot])); add(static_cast<std::uint32_t>(skillCharges[slot])); for (const unsigned char c : skillLoadoutState.nodeBuilds[slot]) add(c); add(0u); }
    for (const unsigned char c : skillLoadoutState.doctrineId) add(c);
    add(0u);
    for (const SkillId skill : requiredSkills) add(static_cast<std::uint32_t>(skill));
    add(0u);
    for (const SkillId skill : forbiddenSkills) add(static_cast<std::uint32_t>(skill));
    add(0u);
    for (const std::string& branch : allowedSkillBranches) { for (const unsigned char c : branch) add(c); add(0u); }
    add(static_cast<std::uint32_t>(nextAllyId)); add(static_cast<std::uint32_t>(nextBuildingId)); add(static_cast<std::uint32_t>(nextZoneId));
    add(static_cast<std::uint32_t>(economyState->resources.scrap)); add(static_cast<std::uint32_t>(economyState->resources.scrapCarryCap)); add(static_cast<std::uint32_t>(economyState->resources.biomass)); add(static_cast<std::uint32_t>(economyState->resources.paradox)); add(static_cast<std::uint32_t>(economyState->resources.instability)); add(static_cast<std::uint32_t>(economyState->resources.resolve)); add(static_cast<std::uint32_t>(economyState->resources.fate)); add(static_cast<std::uint32_t>(economyState->resources.trophies)); add(static_cast<std::uint32_t>(economyState->resources.targetingData)); add(static_cast<std::uint32_t>(economyState->resources.bond)); add(static_cast<std::uint32_t>(economyState->resources.discord)); add(static_cast<std::uint32_t>(economyState->resources.charge)); add(static_cast<std::uint32_t>(economyState->resources.buildSupply)); add(static_cast<std::uint32_t>(economyState->resources.buildSupplyCap));
    add(static_cast<std::uint32_t>(economyState->nextRemainId)); add(static_cast<std::uint32_t>(economyState->nextDroneId)); add(static_cast<std::uint32_t>(economyState->allowanceWave)); add(static_cast<std::uint32_t>(economyState->starterBundleWave)); add(static_cast<std::uint32_t>(economyState->mineFoundryWave)); add(static_cast<std::uint32_t>(economyState->trapNetworkWave)); add(static_cast<std::uint32_t>(economyState->arcanistCadence)); add(static_cast<std::uint32_t>(economyState->stormReactions)); add(static_cast<std::uint32_t>(economyState->stormLastReaction)); add(static_cast<std::uint32_t>(economyState->stormReactionChain)); add(static_cast<std::uint32_t>(economyState->stormPerfectTicks)); add(static_cast<std::uint32_t>(economyState->stormTidalMemoryReady)); add(static_cast<std::uint32_t>(economyState->fateBoostTicks)); add(static_cast<std::uint32_t>(economyState->fateQueueSize)); add(static_cast<std::uint32_t>(economyState->fateQueueSerial)); add(static_cast<std::uint32_t>(economyState->fateUnfavorableBank)); for (const int event : economyState->fateQueue) add(static_cast<std::uint32_t>(event)); add(static_cast<std::uint32_t>(economyState->guardianWardTicks)); add(static_cast<std::uint32_t>(economyState->nextBountyId)); add(static_cast<std::uint32_t>(economyState->activeBountyId)); add(static_cast<std::uint32_t>(economyState->activeBountyTargetId)); add(static_cast<std::uint32_t>(economyState->bountyAgeTicks)); add(static_cast<std::uint32_t>(economyState->bountyIsolationTicks)); add(static_cast<std::uint32_t>(economyState->bountyObjectivesCompleted)); add(economyState->bountyTagMask); add(static_cast<std::uint32_t>(economyState->activeVowTicks)); add(static_cast<std::uint32_t>(economyState->vowStartingLives)); add(static_cast<std::uint32_t>(economyState->activeVowKind)); add(static_cast<std::uint32_t>(economyState->activeVowProgress)); add(static_cast<std::uint32_t>(economyState->activeVowTarget)); add(static_cast<std::uint32_t>(economyState->vowsCompleted)); add(static_cast<std::uint32_t>(economyState->bloodDebt)); add(static_cast<std::uint32_t>(economyState->bloodEclipseTicks)); add(static_cast<std::uint32_t>(economyState->bloodEclipseHealth)); add(static_cast<std::uint32_t>(economyState->bloodHeartFragments)); add(static_cast<std::uint32_t>(economyState->bloodReservoirReady)); add(static_cast<std::uint32_t>(economyState->bloodGolemReserve)); add(static_cast<std::uint32_t>(economyState->bloodPulseEmpowerTicks));
    add(static_cast<std::uint32_t>(economyState->timeFractureTicks)); add(static_cast<std::uint32_t>(economyState->beastAdaptation)); add(static_cast<std::uint32_t>(economyState->beastAdaptationTicks)); add(static_cast<std::uint32_t>(economyState->beastPounceEmpoweredTicks)); add(static_cast<std::uint32_t>(economyState->beastCommandTargetId)); add(static_cast<std::uint32_t>(economyState->beastCommandTicks)); add(static_cast<std::uint32_t>(economyState->stormResonanceCount)); for (const int reactionId : economyState->stormResonanceIds) add(static_cast<std::uint32_t>(reactionId)); add(static_cast<std::uint32_t>(economyState->pandemicTicks)); add(static_cast<std::uint32_t>(economyState->pandemicPrimeStrain)); add(static_cast<std::uint32_t>(economyState->pandemicPrimeHostId)); add(static_cast<std::uint32_t>(economyState->plagueSymbioticWave));
    add(static_cast<std::uint32_t>(economyState->beastAdaptationStreak)); addBool(economyState->beastAdaptationPersistent);
    for (const unsigned char c : economyState->bountyRetainedWeakness) add(c); add(0u); add(static_cast<std::uint32_t>(economyState->bountyRetainedWeaknessReady));
    add(static_cast<std::uint32_t>(economyState->bountyCollectorReady)); add(static_cast<std::uint32_t>(economyState->chronomancerDebtBurstTicks));
    add(economyState->beastTraitMask); add(static_cast<std::uint32_t>(economyState->beastSignatureTrait)); add(static_cast<std::uint32_t>(economyState->beastSignatureWave)); add(static_cast<std::uint32_t>(economyState->beastParticipationTicks)); add(static_cast<std::uint32_t>(economyState->beastPackTakedownReady)); add(static_cast<std::uint32_t>(economyState->beastHuntPinReady));
    add(static_cast<std::uint32_t>(economyState->turretBatteryWave));
    add(static_cast<std::uint32_t>(economyState->salvageBatteryWave));
add(static_cast<std::uint32_t>(economyState->usurperInfightingKills)); add(static_cast<std::uint32_t>(economyState->usurperRebelEchoes)); add(static_cast<std::uint32_t>(economyState->usurperRiotReady));
    add(static_cast<std::uint32_t>(economyState->usurperCivilWarReady));
    add(static_cast<std::uint32_t>(economyState->salvageModuleReady)); add(static_cast<std::uint32_t>(economyState->salvagerConstructionMask)); add(static_cast<std::uint32_t>(economyState->salvagerMasterworkReady)); add(static_cast<std::uint32_t>(economyState->arsenalAmmoTicks)); add(static_cast<std::uint32_t>(economyState->arsenalAmmoPayouts)); add(static_cast<std::uint32_t>(economyState->arsenalInventoryTicks)); add(static_cast<std::uint32_t>(economyState->arsenalInventoryScrap)); add(static_cast<std::uint32_t>(economyState->legionSummonCasts)); add(static_cast<std::uint32_t>(economyState->legionMinorOrders)); add(static_cast<std::uint32_t>(economyState->legionLastOrderType)); add(static_cast<std::uint32_t>(economyState->architectNetworkMask)); add(static_cast<std::uint32_t>(economyState->architectNetworkReady)); add(static_cast<std::uint32_t>(economyState->oathVowTypeMask)); add(static_cast<std::uint32_t>(economyState->oathExemplarReady)); add(static_cast<std::uint32_t>(economyState->oathRewardChoiceA)); add(static_cast<std::uint32_t>(economyState->oathRewardChoiceB)); add(static_cast<std::uint32_t>(economyState->arcanistAfterimageReady)); add(static_cast<std::uint32_t>(economyState->arcanistArcanumReady)); add(static_cast<std::uint32_t>(economyState->chronomancerOperationMask)); add(static_cast<std::uint32_t>(economyState->chronomancerStableMomentReady));
    add(static_cast<std::uint32_t>(economyState->fateHouseTicks)); add(static_cast<std::uint32_t>(economyState->fateRewriteReady)); add(static_cast<std::uint32_t>(economyState->fateDoomedOutcomeReady)); add(static_cast<std::uint32_t>(economyState->fatePreviewEvent + 1)); add(static_cast<std::uint32_t>(economyState->fateCategoryMask)); add(static_cast<std::uint32_t>(economyState->bountyKillingMomentumReady)); add(static_cast<std::uint32_t>(economyState->bountyMomentumObjective + 1)); add(static_cast<std::uint32_t>(economyState->plagueDistinctInfectedCount)); add(static_cast<std::uint32_t>(economyState->plagueFreeMutationReady)); for (const int id : economyState->plagueInfectedIds) add(static_cast<std::uint32_t>(id)); add(static_cast<std::uint32_t>(economyState->artilleristAccurateImpacts)); add(static_cast<std::uint32_t>(economyState->artilleristFireSolutionReady)); add(static_cast<std::uint32_t>(economyState->voidSpatialOperationMask)); add(static_cast<std::uint32_t>(economyState->voidFixedPointReady));
    for (int value : economyState->bountyObjectiveKinds) add(static_cast<std::uint32_t>(value));
    for (int value : economyState->bountyObjectiveProgress) add(static_cast<std::uint32_t>(value));
    for (int value : economyState->bountyObjectiveTargets) add(static_cast<std::uint32_t>(value));
    addSize(economyState->remains.size());
    for (const BattlefieldRemain& remain : economyState->remains) { add(static_cast<std::uint32_t>(remain.id)); addFloat(remain.pos.x); addFloat(remain.pos.y); add(static_cast<std::uint32_t>(remain.value)); add(static_cast<std::uint32_t>(remain.biomassValue)); add(static_cast<std::uint32_t>(remain.source)); add(static_cast<std::uint32_t>(remain.createdTick)); add(static_cast<std::uint32_t>(remain.expiryTick)); add(static_cast<std::uint32_t>(remain.claimedByDrone)); addBool(remain.consumed); }
    addSize(economyState->drones.size());
    for (const RecoveryDrone& drone : economyState->drones) { add(static_cast<std::uint32_t>(drone.id)); addFloat(drone.pos.x); addFloat(drone.pos.y); addFloat(drone.speed); add(static_cast<std::uint32_t>(drone.carrying)); add(static_cast<std::uint32_t>(drone.boostTicks)); add(static_cast<std::uint32_t>(drone.targetRemainId)); addBool(drone.active); }
    add(static_cast<std::uint32_t>(workshopTowerCoreLevel));
    for (std::uint8_t level : workshopModuleLevels) add(static_cast<std::uint32_t>(level));
    for (std::uint8_t level : workshopSupportLevels) add(static_cast<std::uint32_t>(level));
    add(static_cast<std::uint32_t>(counters.ticks)); add(static_cast<std::uint32_t>(counters.wave)); add(static_cast<std::uint32_t>(counters.kills));
    add(static_cast<std::uint32_t>(counters.leaks)); add(static_cast<std::uint32_t>(counters.upgrades)); add(static_cast<std::uint32_t>(counters.ultimates)); add(static_cast<std::uint32_t>(counters.shotsFired)); add(static_cast<std::uint32_t>(counters.bossAttacks)); add(static_cast<std::uint32_t>(counters.score)); add(static_cast<std::uint32_t>(counters.damageDealt)); add(static_cast<std::uint32_t>(counters.reactionTriggers)); add(static_cast<std::uint32_t>(counters.statusApplications)); add(static_cast<std::uint32_t>(counters.skillCasts)); add(static_cast<std::uint32_t>(counters.failedSkillCasts));
    for (const auto& values : {counters.skillDamage, counters.skillHealing, counters.skillTargets, counters.skillControlTicks, counters.skillSummons}) for (const int value : values) add(static_cast<std::uint32_t>(value));
    addSize(enemyList.size());
    for (const Enemy& enemy : enemyList) {
        add(static_cast<std::uint32_t>(enemy.id)); add(static_cast<std::uint32_t>(enemy.type)); add(static_cast<std::uint32_t>(enemy.phase));
        addBool(enemy.boss); addBool(enemy.alive); addFloat(enemy.pos.x); addFloat(enemy.pos.y); add(static_cast<std::uint32_t>(enemy.pathHistoryCount)); add(static_cast<std::uint32_t>(enemy.pathHistoryHead)); for (const Vec2& sample : enemy.pathHistory) { addFloat(sample.x); addFloat(sample.y); } addFloat(enemy.hp); addFloat(enemy.maxHp); addFloat(enemy.speed); addFloat(enemy.radius);
        addFloat(enemy.slow); addFloat(enemy.stun); addFloat(enemy.burn); addFloat(enemy.burnDps); add(static_cast<std::uint32_t>(enemy.burnTicks)); addBool(enemy.pandemicSpreadUsed);
        for (const unsigned char c : enemy.weaknessTag) add(c); add(0u); addBool(enemy.weaknessRewarded);
        add(static_cast<std::uint32_t>(enemy.infectionStrain));
        add(static_cast<std::uint32_t>(enemy.predictedTicks)); addFloat(enemy.predictedPosition.x); addFloat(enemy.predictedPosition.y); add(static_cast<std::uint32_t>(enemy.spatialCooldownTicks)); add(static_cast<std::uint32_t>(enemy.banishedTicks)); addFloat(enemy.banishReturnPosition.x); addFloat(enemy.banishReturnPosition.y); addBool(enemy.banishReturnArmed); add(static_cast<std::uint32_t>(enemy.challengeTicks));
        addFloat(enemy.poison); addFloat(enemy.poisonDps); add(static_cast<std::uint32_t>(enemy.poisonTicks)); add(static_cast<std::uint32_t>(enemy.shockTicks)); add(static_cast<std::uint32_t>(enemy.soakTicks)); add(static_cast<std::uint32_t>(enemy.freezeTicks)); add(static_cast<std::uint32_t>(enemy.bountyTicks)); add(static_cast<std::uint32_t>(enemy.bountyId)); add(static_cast<std::uint32_t>(enemy.infectionTicks)); add(static_cast<std::uint32_t>(enemy.infectionStacks)); add(static_cast<std::uint32_t>(enemy.infectionGeneration)); add(static_cast<std::uint32_t>(enemy.temporalDelayTicks)); add(static_cast<std::uint32_t>(enemy.temporalCancelTicks)); add(static_cast<std::uint32_t>(enemy.temporalEchoTicks)); add(static_cast<std::uint32_t>(enemy.temporalAnchorTicks)); addFloat(enemy.temporalAnchorPosition.x); addFloat(enemy.temporalAnchorPosition.y); addFloat(enemy.temporalAnchorHealth); addBool(enemy.temporalAnchorValid); addBool(enemy.temporalAnchorProtected); add(static_cast<std::uint32_t>(enemy.allegiance)); add(static_cast<std::uint32_t>(enemy.allegianceTicks)); addBool(enemy.usurperInheritedMark); addBool(enemy.usurperTreasonMark); addBool(enemy.ruinBrittleTriggered); add(static_cast<std::uint32_t>(enemy.confusionTicks)); addFloat(enemy.vulnerability); add(static_cast<std::uint32_t>(enemy.vulnerabilityTicks)); add(static_cast<std::uint32_t>(enemy.attackCooldownTicks)); add(static_cast<std::uint32_t>(enemy.telegraphTicks)); addFloat(enemy.damageResistance); addFloat(enemy.teleportCooldown);
    }
    for (const Enemy& enemy : enemyList) { add(static_cast<std::uint32_t>(enemy.galeTicks)); add(static_cast<std::uint32_t>(enemy.stormReactionCooldownTicks)); add(static_cast<std::uint32_t>(enemy.lastStormReactionId)); add(static_cast<std::uint32_t>(enemy.stormReactionGenerationDepth)); add(static_cast<std::uint32_t>(enemy.stormLastSetupSkill + 1)); add(static_cast<std::uint32_t>(enemy.stormLastReactionSkill + 1)); add(static_cast<std::uint32_t>(enemy.sharedAgonyTicks)); addSize(enemy.trapContactIds.size()); for (const int trapId : enemy.trapContactIds) add(static_cast<std::uint32_t>(trapId)); }
    for (const DeployableBuilding& building : buildings) { addFloat(building.footprintRadius); addFloat(building.effectValue); addFloat(building.networkRangeScale); addFloat(building.networkActionScale); addFloat(building.networkRearmScale); add(static_cast<std::uint32_t>(building.charges)); }
    for (const Enemy& enemy : enemyList) { add(static_cast<std::uint32_t>(enemy.cryoWhiteoutTicks)); add(static_cast<std::uint32_t>(enemy.signalJamTicks)); }
    addSize(projectileList.size());
    for (const Projectile& projectile : projectileList) {
        addBool(projectile.alive); addBool(projectile.explosive); addFloat(projectile.pos.x); addFloat(projectile.pos.y); addFloat(projectile.velocity.x); addFloat(projectile.velocity.y);
        addFloat(projectile.damage); addFloat(projectile.radius); add(static_cast<std::uint32_t>(projectile.pierces)); add(static_cast<std::uint32_t>(projectile.bounces));
    }
    addSize(alliedUnitsList.size());
    for (const AlliedUnit& unit : alliedUnitsList) { add(static_cast<std::uint32_t>(unit.id)); addFloat(unit.pos.x); addFloat(unit.pos.y); addFloat(unit.hp); addFloat(unit.maxHp); addFloat(unit.speed); addFloat(unit.damage); addFloat(unit.damageScale); addFloat(unit.speedScale); addFloat(unit.damageReduction); addBool(unit.nextAttackCooldownReduced); add(static_cast<std::uint32_t>(unit.buffTicks)); add(static_cast<std::uint32_t>(unit.injuryTicks)); add(static_cast<std::uint32_t>(unit.accelerationTailTicks)); addFloat(unit.radius); add(static_cast<std::uint32_t>(unit.attackCooldownTicks)); add(static_cast<std::uint32_t>(unit.lifetimeTicks)); add(static_cast<std::uint32_t>(unit.ownerSkill)); addBool(unit.alive); for (const unsigned char c : unit.role) add(c); add(0u); }
    for (const AlliedUnit& unit : alliedUnitsList) add(static_cast<std::uint32_t>(unit.downedTicks));
    addSize(buildings.size());
    for (const DeployableBuilding& building : buildings) { add(static_cast<std::uint32_t>(building.id)); addFloat(building.pos.x); addFloat(building.pos.y); addFloat(building.hp); addFloat(building.maxHp); add(static_cast<std::uint32_t>(building.lifetimeTicks)); add(static_cast<std::uint32_t>(building.spawnCooldownTicks)); add(static_cast<std::uint32_t>(building.attackCooldownTicks)); add(static_cast<std::uint32_t>(building.ownerSkill)); addBool(building.alive); addFloat(building.actionSpeedScale); addFloat(building.networkRangeScale); addFloat(building.networkActionScale); addFloat(building.networkRearmScale); add(static_cast<std::uint32_t>(building.actionSpeedTicks)); add(static_cast<std::uint32_t>(building.linkedBuildingId)); add(static_cast<std::uint32_t>(building.linkedPrimeTicks)); add(static_cast<std::uint32_t>(building.rampTicks)); add(static_cast<std::uint32_t>(building.rampTargetId)); for (const unsigned char c : building.role) add(c); add(0u); }
    addSize(zones.size());
    for (const SkillZone& zone : zones) { add(static_cast<std::uint32_t>(zone.id)); addFloat(zone.center.x); addFloat(zone.center.y); addFloat(zone.radius); add(static_cast<std::uint32_t>(zone.remainingTicks)); add(static_cast<std::uint32_t>(zone.armTicks)); addFloat(zone.valueA); addFloat(zone.valueB); add(static_cast<std::uint32_t>(zone.ownerSkill)); addBool(zone.triggered); addBool(zone.alive); addBool(zone.pullsToEdge); add(static_cast<std::uint32_t>(zone.processedTicks)); addFloat(zone.predictedPosition.x); addFloat(zone.predictedPosition.y); add(static_cast<std::uint32_t>(zone.predictedEnemyId)); addFloat(zone.secondaryCenter.x); addFloat(zone.secondaryCenter.y); addBool(zone.gravityAftermathTriggered); addBool(zone.gravitySingularityLocked); }
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

std::string GameSim::failureGuidance() const {
    if (!gameOver || victory) return {};
    const auto& threats = content.waveEnemyTypeWeight[static_cast<std::size_t>(std::clamp(wave - 1, 0, 9))];
    const float swarm = threats[static_cast<std::size_t>(EnemyType::Swarmling)];
    const float fast = threats[static_cast<std::size_t>(EnemyType::Runner)] + threats[static_cast<std::size_t>(EnemyType::Teleporter)];
    const float armor = threats[static_cast<std::size_t>(EnemyType::Tank)] + threats[static_cast<std::size_t>(EnemyType::Shielded)];
    if (swarm >= fast && swarm >= armor && swarm > 0.0f) return "LOW SWARM CLEAR // SEEK AREA, CHAIN, OR PIERCE";
    if (fast >= armor && fast > 0.0f) return "CONTROL UPTIME TOO LOW // SEEK SLOW, STUN, OR DISPLACEMENT";
    if (armor > 0.0f) return "INSUFFICIENT ARMOR PRESSURE // SEEK BURST, FIRE, OR POISON";
    return "INSUFFICIENT SINGLE-TARGET DAMAGE // MATCH THE WEAPON'S CORE TAGS";
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

const char* weaponDescription(Weapon weapon) {
    switch (weapon) {
        case Weapon::RapidFire: return "FAST KINETIC FIRE // BUILDS PIERCE AND RATE";
        case Weapon::ExplosiveCannon: return "AREA BLASTS // BUILDS FIRE, STUN, AND SPLASH";
        case Weapon::ArcaneBeam: return "SUSTAINED BEAM // BUILDS ELECTRIC CHAINS AND STATUS";
        case Weapon::FrostBlaster: return "SLOWING CONTROL // BUILDS FREEZE AND SHATTER";
        case Weapon::SniperRailgun: return "HIGH BURST // BUILDS PIERCE AND EXECUTION DAMAGE";
    }
    return "UNKNOWN WEAPON";
}

const char* chassisName(TowerChassis chassis) {
    switch (chassis) {
        case TowerChassis::Vanguard: return "VANGUARD";
        case TowerChassis::Bastion: return "BASTION";
        case TowerChassis::Catalyst: return "CATALYST";
    }
    return "UNKNOWN CHASSIS";
}

const char* chassisDescription(TowerChassis chassis) {
    switch (chassis) {
        case TowerChassis::Vanguard: return "STANDARD FIRE // FLEXIBLE LOADOUT // RELIABLE BASELINE";
        case TowerChassis::Bastion: return "+4 LIVES // SLOWER FIRE // LEAK FORGIVENESS";
        case TowerChassis::Catalyst: return "FASTER ULTIMATE // STATUS-READY DAMAGE PROFILE";
    }
    return "UNKNOWN CHASSIS";
}

const char* supportModuleName(SupportModule support) {
    switch (support) {
        case SupportModule::None: return "NO SUPPORT";
        case SupportModule::CreditRelay: return "CREDIT RELAY";
        case SupportModule::StasisField: return "STASIS FIELD";
        case SupportModule::RepairDrones: return "REPAIR DRONES";
        case SupportModule::CorrosionAmp: return "CORROSION AMP";
    }
    return "UNKNOWN SUPPORT";
}

const char* supportModuleDescription(SupportModule support) {
    switch (support) {
        case SupportModule::None: return "NO SIDE EFFECT // PURE PRIMARY BUILD";
        case SupportModule::CreditRelay: return "+2 CREDITS PER NORMAL KILL // +30 PER BOSS";
        case SupportModule::StasisField: return "APPLIES A LIGHT LANE SLOW // CONTROL SETUP";
        case SupportModule::RepairDrones: return "RESTORES 1 LIFE EVERY 8 KILLS // DEFENSIVE";
        case SupportModule::CorrosionAmp: return "+20% DAMAGE AGAINST BURNING OR POISONED TARGETS";
    }
    return "UNKNOWN SUPPORT EFFECT";
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

const char* skullDescription(Skull skull) {
    switch (skull) {
        case Skull::None: return "NO ADDITIONAL RISK";
        case Skull::Swarm: return "MORE ENEMIES PER WAVE // AREA COVERAGE MATTERS";
        case Skull::GlassCannon: return "FEWER STARTING LIVES // LEAKS ARE EXPENSIVE";
        case Skull::Haste: return "FASTER ENEMIES // CONTROL AND TARGETING MATTER";
        case Skull::Greed: return "MORE REWARD CURRENCY // PRESSURE STAYS STANDARD";
    }
    return "UNKNOWN MODIFIER";
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
        case Upgrade::SteadyAim: return "STEADY AIM";
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
        case Upgrade::SteadyAim: return "SIX PERCENT MORE DAMAGE";
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

const char* ultimateDescription(Ultimate ultimate) {
    switch (ultimate) {
        case Ultimate::MeteorRain: return "MULTI-IMPACT AREA DAMAGE // STRONG AGAINST GROUPS";
        case Ultimate::BulletStorm: return "TEMPORARY FIRE-RATE BURST // STACKS WITH KINETIC BUILDS";
        case Ultimate::AbsoluteZero: return "GLOBAL FREEZE // CREATES TIME FOR CONTROL BUILDS";
        case Ultimate::GravityShift: return "REWIND AND STUN // RESETS THE MOST ADVANCED THREATS";
        case Ultimate::EnergySurge: return "GLOBAL BURST // BONUS DAMAGE AND CREDITS";
    }
    return "UNKNOWN ULTIMATE";
}

const char* skillName(SkillId skill) {
    switch (skill) {
        case SkillId::GravityWell: return "GRAVITY WELL";
        case SkillId::PhaseMine: return "PHASE MINE";
        case SkillId::VanguardDrop: return "VANGUARD DROP";
        case SkillId::ForwardBarracks: return "FORWARD BARRACKS";
        case SkillId::RuinHex: return "RUIN HEX";
        case SkillId::RallyBeacon: return "RALLY BEACON";
        case SkillId::SentryFabricator: return "SENTRY FABRICATOR";
        case SkillId::CryoField: return "CRYO FIELD";
        case SkillId::DroneSwarm: return "DRONE SWARM";
        case SkillId::ResonancePulse: return "RESONANCE PULSE";
        case SkillId::ArcBolt: return "ARC BOLT";
        case SkillId::ChainLightning: return "CHAIN LIGHTNING";
        case SkillId::TemporalAnchor: return "TEMPORAL ANCHOR";
        case SkillId::PatientZero: return "PATIENT ZERO";
        case SkillId::ScrapCache: return "SCRAP CACHE";
        case SkillId::Wanted: return "WANTED";
        case SkillId::AlphaBeast: return "POUNCE";
        case SkillId::MortarBarrage: return "MORTAR BARRAGE";
        case SkillId::RiftGate: return "RIFT GATE";
        case SkillId::GuardianWard: return "GUARDIAN WARD";
        case SkillId::LoadedDice: return "LOADED DICE";
        case SkillId::BloodLance: return "BLOOD LANCE";
        case SkillId::LifeSiphon: return "LIFE SIPHON";
        case SkillId::HemorrhageField: return "HEMORRHAGE FIELD";
        case SkillId::BloodGolem: return "BLOOD GOLEM";
        case SkillId::LastPulse: return "LAST PULSE";
        case SkillId::TreasonMark: return "TREASON MARK";
        case SkillId::RiotWhisper: return "RIOT WHISPER";
        case SkillId::PuppetThread: return "PUPPET THREAD";
        case SkillId::FalseOrders: return "FALSE ORDERS";
        case SkillId::SharedAgony: return "SHARED AGONY";
        case SkillId::Thunderhead: return "THUNDERHEAD";
        case SkillId::FlashFlood: return "FLASH FLOOD";
        case SkillId::ThermalSurge: return "THERMAL SURGE";
        case SkillId::EyeOfTheStorm: return "EYE OF THE STORM";
        case SkillId::BulwarkWall: return "BULWARK WALL";
        case SkillId::TrapFoundry: return "TRAP FOUNDRY";
        case SkillId::Accelerate: return "ACCELERATE";
        case SkillId::Delay: return "DELAY";
        case SkillId::Rewind: return "REWIND";
        case SkillId::BorrowedTime: return "BORROWED TIME";
        case SkillId::DeadeyeShot: return "DEADEYE SHOT";
        case SkillId::Harpoon: return "HARPOON";
        case SkillId::ExploitWeakness: return "EXPLOIT WEAKNESS";
        case SkillId::CollectorDrone: return "COLLECTOR DRONE";
        case SkillId::VectorSwarm: return "VECTOR SWARM";
        case SkillId::Mutation: return "MUTATION";
        case SkillId::RuptureHost: return "RUPTURE HOST";
        case SkillId::Quarantine: return "QUARANTINE";
        case SkillId::MineLayer: return "MINE LAYER";
        case SkillId::JuryRiggedTurret: return "JURY-RIGGED TURRET";
        case SkillId::StripForParts: return "STRIP FOR PARTS";
        case SkillId::ImprovisedArsenal: return "IMPROVISED ARSENAL";
        case SkillId::SpotterDrone: return "SPOTTER DRONE";
        case SkillId::RailCannon: return "RAIL CANNON";
        case SkillId::ClusterShell: return "CLUSTER SHELL";
        case SkillId::WalkingBarrage: return "WALKING BARRAGE";
        case SkillId::SpatialCollapse: return "SPATIAL COLLAPSE";
        case SkillId::Banish: return "BANISH";
        case SkillId::PhaseExchange: return "PHASE EXCHANGE";
        case SkillId::EventHorizon: return "EVENT HORIZON";
        case SkillId::Intercept: return "INTERCEPT";
        case SkillId::Challenge: return "CHALLENGE";
        case SkillId::Sanctuary: return "SANCTUARY";
        case SkillId::Judgment: return "JUDGMENT";
        case SkillId::Misfortune: return "MISFORTUNE";
        case SkillId::LuckyShot: return "LUCKY SHOT";
        case SkillId::StackDeck: return "STACK THE DECK";
        case SkillId::DoubleNothing: return "DOUBLE OR NOTHING";
        case SkillId::Feed: return "FEED";
        case SkillId::Adaptation: return "ADAPTATION";
        case SkillId::PackCall: return "PACK CALL";
        case SkillId::HuntCommand: return "HUNT COMMAND";
        case SkillId::Count: break;
    }
    return "UNKNOWN SKILL";
}

const char* skillIdString(SkillId skill) {
    switch (skill) {
        case SkillId::GravityWell: return "gravity_well";
        case SkillId::PhaseMine: return "phase_mine";
        case SkillId::VanguardDrop: return "vanguard_drop";
        case SkillId::ForwardBarracks: return "forward_barracks";
        case SkillId::RuinHex: return "ruin_hex";
        case SkillId::RallyBeacon: return "rally_beacon";
        case SkillId::SentryFabricator: return "sentry_fabricator";
        case SkillId::CryoField: return "cryo_field";
        case SkillId::DroneSwarm: return "drone_swarm";
        case SkillId::ResonancePulse: return "resonance_pulse";
        case SkillId::ArcBolt: return "arc_bolt";
        case SkillId::ChainLightning: return "chain_lightning";
        case SkillId::TemporalAnchor: return "temporal_anchor";
        case SkillId::PatientZero: return "patient_zero";
        case SkillId::ScrapCache: return "scrap_cache";
        case SkillId::Wanted: return "wanted";
        case SkillId::AlphaBeast: return "alpha_beast";
        case SkillId::MortarBarrage: return "mortar_barrage";
        case SkillId::RiftGate: return "rift_gate";
        case SkillId::GuardianWard: return "guardian_ward";
        case SkillId::LoadedDice: return "loaded_dice";
        case SkillId::BloodLance: return "blood_lance";
        case SkillId::LifeSiphon: return "life_siphon";
        case SkillId::HemorrhageField: return "hemorrhage_field";
        case SkillId::BloodGolem: return "blood_golem";
        case SkillId::LastPulse: return "last_pulse";
        case SkillId::TreasonMark: return "treason_mark";
        case SkillId::RiotWhisper: return "riot_whisper";
        case SkillId::PuppetThread: return "puppet_thread";
        case SkillId::FalseOrders: return "false_orders";
        case SkillId::SharedAgony: return "shared_agony";
        case SkillId::Thunderhead: return "thunderhead";
        case SkillId::FlashFlood: return "flash_flood";
        case SkillId::ThermalSurge: return "thermal_surge";
        case SkillId::EyeOfTheStorm: return "eye_of_the_storm";
        case SkillId::BulwarkWall: return "bulwark_wall";
        case SkillId::TrapFoundry: return "trap_foundry";
        case SkillId::Accelerate: return "accelerate";
        case SkillId::Delay: return "delay";
        case SkillId::Rewind: return "rewind";
        case SkillId::BorrowedTime: return "borrowed_time";
        case SkillId::DeadeyeShot: return "deadeye_shot";
        case SkillId::Harpoon: return "harpoon";
        case SkillId::ExploitWeakness: return "exploit_weakness";
        case SkillId::CollectorDrone: return "collector_drone";
        case SkillId::VectorSwarm: return "vector_swarm";
        case SkillId::Mutation: return "mutation";
        case SkillId::RuptureHost: return "rupture_host";
        case SkillId::Quarantine: return "quarantine";
        case SkillId::MineLayer: return "mine_layer";
        case SkillId::JuryRiggedTurret: return "jury_rigged_turret";
        case SkillId::StripForParts: return "strip_for_parts";
        case SkillId::ImprovisedArsenal: return "improvised_arsenal";
        case SkillId::SpotterDrone: return "spotter_drone";
        case SkillId::RailCannon: return "rail_cannon";
        case SkillId::ClusterShell: return "cluster_shell";
        case SkillId::WalkingBarrage: return "walking_barrage";
        case SkillId::SpatialCollapse: return "spatial_collapse";
        case SkillId::Banish: return "banish";
        case SkillId::PhaseExchange: return "phase_exchange";
        case SkillId::EventHorizon: return "event_horizon";
        case SkillId::Intercept: return "intercept";
        case SkillId::Challenge: return "challenge";
        case SkillId::Sanctuary: return "sanctuary";
        case SkillId::Judgment: return "judgment";
        case SkillId::Misfortune: return "misfortune";
        case SkillId::LuckyShot: return "lucky_shot";
        case SkillId::StackDeck: return "stack_deck";
        case SkillId::DoubleNothing: return "double_nothing";
        case SkillId::Feed: return "feed";
        case SkillId::Adaptation: return "adaptation";
        case SkillId::PackCall: return "pack_call";
        case SkillId::HuntCommand: return "hunt_command";
        case SkillId::Count: break;
    }
    return "unknown_skill";
}

SkillId skillIdFromString(const std::string& id) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(SkillId::Count); ++index) {
        const SkillId skill = static_cast<SkillId>(index);
        if (id == skillIdString(skill)) return skill;
    }
    return SkillId::Count;
}

const char* skillDescription(SkillId skill) {
    switch (skill) {
        case SkillId::GravityWell: return "PULLS ENEMIES INTO A DAMAGING ZONE";
        case SkillId::PhaseMine: return "REWINDS ENEMIES THAT CROSS THE MINE";
        case SkillId::VanguardDrop: return "SUMMONS ALLIED SOLDIERS";
        case SkillId::ForwardBarracks: return "BUILDS A SOLDIER-PRODUCING STRUCTURE";
        case SkillId::RuinHex: return "WEAKENS ENEMIES IN AN AREA";
        case SkillId::RallyBeacon: return "HEALS AND BUFFS ALLIED UNITS";
        case SkillId::SentryFabricator: return "DEPLOYS A TEMPORARY TOWER";
        case SkillId::CryoField: return "SLOWS AND FREEZES ENEMIES";
        case SkillId::DroneSwarm: return "DEPLOYS AUTONOMOUS SUPPORT DRONES";
        case SkillId::ResonancePulse: return "A DATA-COMPOSED AREA PULSE FOR EXTENSIBLE SKILL FIXTURES";
        case SkillId::ArcBolt: return "FIRES A LOW-COOLDOWN DIRECT DAMAGE BOLT";
        case SkillId::ChainLightning: return "BURSTS A GROUP AND CONSUMES SHOCK SETUP";
        case SkillId::TemporalAnchor: return "MARKS A TIMELINE STATE FOR A LATER SNAPBACK";
        case SkillId::PatientZero: return "STARTS A DETERMINISTIC INFECTION CHAIN";
        case SkillId::ScrapCache: return "CREATES A SCRAP-READY FIELD REMNANT";
        case SkillId::Wanted: return "MARKS AN ELITE FOR A DETERMINISTIC BOUNTY";
        case SkillId::AlphaBeast: return "COMMANDS THE BONDED PET TO LEAP AND STRIKE AN AREA";
        case SkillId::MortarBarrage: return "PREDICTS A DELAYED LONG-RANGE BOMBARDMENT";
        case SkillId::RiftGate: return "OPENS A DETERMINISTIC SPATIAL DISPLACEMENT FIELD";
        case SkillId::GuardianWard: return "SHIELDS ALLIES AND BUILDS DEFENSIVE RESOLVE";
        case SkillId::LoadedDice: return "PREVIEWS AND IMPROVES A DETERMINISTIC FATE EVENT";
        case SkillId::BloodLance: return "SPENDS TOWER LIFE FOR AREA DAMAGE";
        case SkillId::LifeSiphon: return "SACRIFICES A SUMMON TO RESTORE TOWER LIFE";
        case SkillId::HemorrhageField: return "CREATES A BLOOD ZONE THAT BUILDS DEBT";
        case SkillId::BloodGolem: return "SACRIFICES HEALTH TO SUMMON A GOLEM";
        case SkillId::LastPulse: return "SPENDS BLOOD DEBT ON A DESPERATE PULSE";
        case SkillId::TreasonMark: return "TURNS ONE ENEMY AGAINST ITS FORMER ALLIES";
        case SkillId::RiotWhisper: return "STARTS A BOUNDED ENEMY RIOT";
        case SkillId::PuppetThread: return "CONTROLS ONE ENEMY AS A LIVING SHIELD";
        case SkillId::FalseOrders: return "CONFUSES AN ENEMY GROUP WITH FALSE COMMANDS";
        case SkillId::SharedAgony: return "LINKS ENEMIES SO DAMAGE ECHOES BETWEEN THEM";
        case SkillId::Thunderhead: return "MOVES A STORM CLOUD THAT APPLIES SHOCK";
        case SkillId::FlashFlood: return "SENDS A DIRECTIONAL WAVE THAT SOAKS ENEMIES";
        case SkillId::ThermalSurge: return "IGNITES ENEMIES AND CONVERTS COLD STATES";
        case SkillId::EyeOfTheStorm: return "PULLS ENEMIES AND RESOLVES A STORED REACTION";
        case SkillId::BulwarkWall: return "PLACES AN ATTACKABLE BLOCKING WALL";
        case SkillId::TrapFoundry: return "PLACES A REARMING FLOOR TRAP";
        case SkillId::Accelerate: return "SPEEDS UP ALLIED ACTORS IN AN AREA";
        case SkillId::Delay: return "POSTPONES A HOSTILE EVENT";
        case SkillId::Rewind: return "SENDS ENEMIES BACK ALONG THE LANE";
        case SkillId::BorrowedTime: return "REFRESHES ANOTHER SKILL FOR PARADOX DEBT";
        case SkillId::DeadeyeShot: return "FIRES A HEAVY SHOT THAT REWARDS ISOLATING A BOUNTY";
        case SkillId::Harpoon: return "PULLS A PRIORITY TARGET AWAY FROM ITS GROUP";
        case SkillId::ExploitWeakness: return "REVEALS A DETERMINISTIC TARGET WEAKNESS";
        case SkillId::CollectorDrone: return "COLLECTS A TROPHY FROM THE ACTIVE BOUNTY";
        case SkillId::VectorSwarm: return "SPREADS AN INFECTION BETWEEN NEARBY HOSTS";
        case SkillId::Mutation: return "EVOLVES INFECTED HOSTS INTO A CHOSEN STRAIN";
        case SkillId::RuptureHost: return "BURSTS AN INFECTED HOST AND SPREADS ITS DISEASE";
        case SkillId::Quarantine: return "CONTAINS AND AMPLIFIES INFECTION IN AN AREA";
        case SkillId::MineLayer: return "SPENDS SCRAP TO PLACE A REARMING DAMAGE TRAP";
        case SkillId::JuryRiggedTurret: return "SPENDS SCRAP TO BUILD A TEMPORARY TURRET";
        case SkillId::StripForParts: return "DISMANTLES A TEMPORARY BUILDING FOR SCRAP";
        case SkillId::ImprovisedArsenal: return "SPENDS STORED SCRAP ON A STRONG IMPROVISED WEAPON";
        case SkillId::SpotterDrone: return "REVEALS FUTURE ENEMY POSITIONS FOR ACCURATE BOMBARDMENT";
        case SkillId::RailCannon: return "FIRES A MASSIVE LONG-RANGE LINE SHOT";
        case SkillId::ClusterShell: return "DELIVERS A DELAYED CLUSTER BOMBARDMENT";
        case SkillId::WalkingBarrage: return "MARCHES EXPLOSIONS THROUGH A PREDICTED LANE";
        case SkillId::SpatialCollapse: return "TELEPORTS ENEMIES TOWARD A CHOSEN POINT";
        case SkillId::Banish: return "REMOVES ONE ENEMY FROM THE ARENA BRIEFLY";
        case SkillId::PhaseExchange: return "SWAPS THE POSITIONS OF TWO ENEMY GROUP MEMBERS";
        case SkillId::EventHorizon: return "DISPLACES ENEMIES CROSSING A SPATIAL FIELD";
        case SkillId::Intercept: return "SUMMONS A DEFENDER THAT INTERCEPTS PRESSURE";
        case SkillId::Challenge: return "CHALLENGES A THREAT AND BUILDS RESOLVE";
        case SkillId::Sanctuary: return "HEALS AND PROTECTS ALLIES IN A DEFENSIVE ZONE";
        case SkillId::Judgment: return "SPENDS RESOLVE ON A RETALIATORY STRIKE";
        case SkillId::Misfortune: return "EXPOSES ENEMIES TO A VISIBLE BAD OUTCOME";
        case SkillId::LuckyShot: return "FIRES A VARIABLE-LOOKING BUT DETERMINISTIC STRIKE";
        case SkillId::StackDeck: return "IMPROVES THE NEXT VISIBLE FATE EVENT";
        case SkillId::DoubleNothing: return "WAGERS FATE TO REFRESH ANOTHER SKILL";
        case SkillId::Feed: return "FEEDS THE BONDED PET FROM A BATTLEFIELD REMAIN";
        case SkillId::Adaptation: return "GIVES THE BONDED PET A CONTEXTUAL TRAIT";
        case SkillId::PackCall: return "REGROUPS AND BUFFS ALLIED UNITS AROUND THE PET";
        case SkillId::HuntCommand: return "ASSIGNS THE BONDED PET A PRIORITY TARGET";
        case SkillId::Count: break;
    }
    return "UNKNOWN SKILL EFFECT";
}

const char* skillTargetModeName(SkillTargetMode mode) {
    switch (mode) {
        case SkillTargetMode::None: return "SELF";
        case SkillTargetMode::WorldPoint: return "POINT";
        case SkillTargetMode::Area: return "AREA";
        case SkillTargetMode::Enemy: return "ENEMY";
        case SkillTargetMode::Ally: return "ALLY";
        case SkillTargetMode::Placement: return "PLACEMENT";
        case SkillTargetMode::Lane: return "LANE";
        case SkillTargetMode::Direction: return "DIRECTION";
    }
    return "UNKNOWN TARGET";
}

const char* ultimateEvolutionName(UltimateEvolution evolution) {
    switch (evolution) {
        case UltimateEvolution::None: return "BASE PROTOCOL";
        case UltimateEvolution::SolarAftermath: return "SOLAR AFTERMATH";
        case UltimateEvolution::ExtinctionSpear: return "EXTINCTION SPEAR";
        case UltimateEvolution::ShatteredSky: return "SHATTERED SKY";
        case UltimateEvolution::ResonantArsenal: return "RESONANT ARSENAL";
        case UltimateEvolution::SuppressiveGrid: return "SUPPRESSIVE GRID";
        case UltimateEvolution::ExecutionProtocol: return "EXECUTION PROTOCOL";
        case UltimateEvolution::BrittleSingularity: return "BRITTLE SINGULARITY";
        case UltimateEvolution::PermafrostEngine: return "PERMAFROST ENGINE";
        case UltimateEvolution::ColdConductor: return "COLD CONDUCTOR";
        case UltimateEvolution::EventHorizon: return "EVENT HORIZON";
        case UltimateEvolution::ChronoReversal: return "CHRONO REVERSAL";
        case UltimateEvolution::MassDriver: return "MASS DRIVER";
        case UltimateEvolution::OverdriveLink: return "OVERDRIVE LINK";
        case UltimateEvolution::ChainReactor: return "CHAIN REACTOR";
        case UltimateEvolution::TerminalDischarge: return "TERMINAL DISCHARGE";
    }
    return "UNKNOWN EVOLUTION";
}

const char* ultimateEvolutionDescription(UltimateEvolution evolution) {
    switch (evolution) {
        case UltimateEvolution::None: return "STANDARD ULTIMATE BEHAVIOR";
        case UltimateEvolution::SolarAftermath: return "BURNING IMPACT ZONES // FIRE DAMAGE BUILDS";
        case UltimateEvolution::ExtinctionSpear: return "FOCUS FIRE ON THE HIGHEST-HEALTH TARGET";
        case UltimateEvolution::ShatteredSky: return "FOUR SMALLER IMPACTS // BROAD AOE COVERAGE";
        case UltimateEvolution::ResonantArsenal: return "ULTIMATE EMPOWERS THE EQUIPPED WEAPON";
        case UltimateEvolution::SuppressiveGrid: return "PROJECTILES SLOW AND BRIEFLY STUN";
        case UltimateEvolution::ExecutionProtocol: return "CONCENTRATED SINGLE-TARGET EXECUTION";
        case UltimateEvolution::BrittleSingularity: return "FROZEN TARGETS TAKE HEAVIER BURST DAMAGE";
        case UltimateEvolution::PermafrostEngine: return "LONG SLOW FIELD SUPPORTS DOT BUILDS";
        case UltimateEvolution::ColdConductor: return "FROZEN TARGETS CHAIN ELECTRIC DAMAGE";
        case UltimateEvolution::EventHorizon: return "PULL ZONE AMPLIFIES AREA AND DOT DAMAGE";
        case UltimateEvolution::ChronoReversal: return "REWINDS ADVANCED TARGETS MUCH FARTHER";
        case UltimateEvolution::MassDriver: return "GRAVITY COLLAPSES ONTO ONE TARGET";
        case UltimateEvolution::OverdriveLink: return "TEMPORARILY EMPOWERS WEAPON AND REACTIONS";
        case UltimateEvolution::ChainReactor: return "DAMAGE JUMPS THROUGH STATUS-AFFECTED ENEMIES";
        case UltimateEvolution::TerminalDischarge: return "MASSIVE BOSS-FOCUSED FINISHER";
    }
    return "UNKNOWN EVOLUTION";
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
