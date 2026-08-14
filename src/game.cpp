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
    static constexpr const char* ids[] = {"gravity_well", "phase_mine", "vanguard_drop", "forward_barracks", "ruin_hex", "rally_beacon", "sentry_fabricator", "cryo_field", "drone_swarm", "resonance_pulse"};
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
    gameOver = false;
    victory = false;
    upgradeChoicePending = false;
    upgradeRerolls = 1;
    enemyList.clear();
    projectileList.clear();
    alliedUnitsList.clear();
    buildings.clear();
    zones.clear();
    skillCooldowns.fill(0);
    nextSkillCastSequence = 1;
    nextAllyId = 1;
    nextBuildingId = 1;
    nextZoneId = 1;
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
    unit.hp = health;
    unit.maxHp = health;
    unit.speed = speed;
    unit.damage = damage;
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
    DeployableBuilding building;
    building.id = nextBuildingId++;
    building.pos = position;
    building.hp = health;
    building.maxHp = health;
    building.lifetimeTicks = lifetime;
    building.ownerSkill = owner;
    building.role = role;
    building.spawnCooldownTicks = 30;
    buildings.push_back(building);
    if (owner != SkillId::Count) ++counters.skillSummons[static_cast<std::size_t>(owner)];
}

bool GameSim::executeAuthoredSkill(const SkillCastRequest& request, const SkillDefinition& definition, float radius, float valueA, float valueB) {
    bool executed = false;
    for (const std::string& operation : definition.operations) {
        if (operation == "create_zone") {
            if (zones.size() >= content.maxSkillZones) continue;
            zones.push_back({nextZoneId++, request.target.world, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
            executed = true;
        } else if (operation == "damage_area") {
            const int before = counters.damageDealt;
            damageArea(request.target.world, radius, valueA, false);
            counters.skillDamage[static_cast<std::size_t>(request.skill)] += std::max(0, counters.damageDealt - before);
            executed = true;
        } else if (operation == "apply_weakness") {
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, request.target.world) <= radius * radius) {
                enemy.vulnerability = std::max(enemy.vulnerability, valueA);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks);
                ++counters.skillTargets[static_cast<std::size_t>(request.skill)];
            }
            executed = true;
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
    (void)error;
    const std::size_t slot = request.slot;
    const SkillDefinition& definition = skillDefinition(request.skill);
    float radiusScale = 1.0f;
    float valueScale = 1.0f;
    float cooldownScale = 1.0f;
    int chargesDelta = 0;
    std::string branch;
    for (const SkillNodeDefinition& node : content.skillNodes) {
        if (node.skillId != definition.id || skillNodeRank(slot, node.id) <= 0) continue;
        const int rank = std::min(skillNodeRank(slot, node.id), node.maxRank);
        radiusScale *= std::pow(node.radiusScale, static_cast<float>(rank));
        valueScale *= std::pow(node.valueScale, static_cast<float>(rank));
        cooldownScale *= std::pow(node.cooldownScale, static_cast<float>(rank));
        chargesDelta += node.chargesDelta * rank;
        if (node.tier >= 2) branch = node.branchId;
    }
    const int maximumCharges = std::max(1, definition.charges + chargesDelta);
    skillCharges[slot] = std::min(skillCharges[slot], maximumCharges);
    skillCharges[slot] = std::max(0, skillCharges[slot] - 1);
    skillCooldowns[slot] = std::max(1, static_cast<int>(static_cast<float>(definition.cooldownTicks) * cooldownScale));
    const Vec2 target = request.target.world;
    const float radius = definition.radius * radiusScale;
    const float valueA = definition.valueA * valueScale;
    const float valueB = definition.valueB * valueScale;
    const int persistentVisualDuration = request.skill == SkillId::GravityWell || request.skill == SkillId::PhaseMine || request.skill == SkillId::RallyBeacon || request.skill == SkillId::CryoField || request.skill == SkillId::ResonancePulse
        ? definition.durationTicks : 10;
    emitSkillVisualEvent(request.skill, SkillVisualPhase::Cast, target, radius, persistentVisualDuration, branch);
    if (!definition.operations.empty() && executeAuthoredSkill(request, definition, radius, valueA, valueB)) {
        emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
        return true;
    }
    switch (request.skill) {
        case SkillId::GravityWell:
            if (zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, target, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true, branch == "edge_horizon"});
            break;
        case SkillId::PhaseMine:
            if (zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, target, radius, definition.durationTicks, TickRate / 2, valueA, valueB, request.skill, false, true});
            break;
        case SkillId::VanguardDrop: {
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            const int count = std::max(2, static_cast<int>(valueA) + chargesDelta);
            for (int index = 0; index < count; ++index) spawnAlliedUnit({target.x + static_cast<float>(index * 18), target.y + static_cast<float>((index % 2) * 20 - 10)}, request.skill, branch == "bulwark" ? "bulwark" : (branch == "strike_team" ? "striker" : "soldier"), definition.durationTicks, valueB * (branch == "bulwark" ? 1.35f : 1.0f), 10.0f * (branch == "strike_team" ? 1.55f : 1.0f), branch == "strike_team" ? 72.0f : 52.0f);
            break;
        }
        case SkillId::ForwardBarracks:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 12, branch);
            spawnBuilding(target, request.skill, branch == "field_armory" ? "armory" : "barracks", definition.durationTicks, valueA * (branch == "field_armory" ? 1.3f : 1.0f));
            break;
        case SkillId::RuinHex:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(enemy.pos, target) <= radius * radius) { enemy.vulnerability = std::max(enemy.vulnerability, valueA); enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, definition.durationTicks); if (branch == "withering") enemy.slow = std::max(enemy.slow, 3.0f); ++counters.skillTargets[static_cast<std::size_t>(request.skill)]; }
            break;
        case SkillId::RallyBeacon:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Hit, target, radius, 12, branch);
            for (AlliedUnit& unit : alliedUnitsList) if (unit.alive && distanceSquared(unit.pos, target) <= radius * radius) { const float before = unit.hp; unit.hp = std::min(unit.maxHp, unit.hp + valueA); unit.damageScale = std::max(unit.damageScale, valueB); unit.speedScale = std::max(unit.speedScale, 1.25f); unit.buffTicks = std::max(unit.buffTicks, definition.durationTicks); ++counters.skillTargets[static_cast<std::size_t>(request.skill)]; counters.skillHealing[static_cast<std::size_t>(request.skill)] += static_cast<int>(std::round(unit.hp - before)); }
            break;
        case SkillId::SentryFabricator:
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            spawnBuilding(target, request.skill, branch == "mortar" ? "mortar" : "sentry", definition.durationTicks, valueA);
            break;
        case SkillId::CryoField:
            if (zones.size() < content.maxSkillZones) zones.push_back({nextZoneId++, target, radius, definition.durationTicks, 0, valueA, valueB, request.skill, false, true});
            break;
        case SkillId::DroneSwarm: {
            emitSkillVisualEvent(request.skill, SkillVisualPhase::Spawn, target, radius, 10, branch);
            const int count = std::max(2, static_cast<int>(valueA) + chargesDelta);
            for (int index = 0; index < count; ++index) spawnAlliedUnit({target.x + static_cast<float>(index * 12), target.y + static_cast<float>((index % 3) * 14 - 14)}, request.skill, branch == "disruptor" ? "disruptor" : "drone", definition.durationTicks, 22.0f, valueB * (branch == "hunter" ? 1.45f : 1.0f), 85.0f);
            break;
        }
        default:
            break;
        case SkillId::Count: break;
    }
    return true;
}

SkillSnapshot GameSim::skillSnapshot(std::size_t slot) const {
    SkillSnapshot snapshot;
    if (slot >= SkillSlotCount) return snapshot;
    const SkillDefinition& definition = skillDefinition(skillLoadoutState.skills[slot]);
    snapshot.skill = skillLoadoutState.skills[slot];
    snapshot.targetMode = targetModeFromString(definition.targetMode);
    snapshot.cooldownRemaining = skillCooldowns[slot];
    snapshot.cooldownMaximum = definition.cooldownTicks;
    snapshot.charges = skillCharges[slot];
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
    for (Enemy& enemy : enemyList) {
        if (!enemy.alive) continue;
        if (enemy.stun > 0.0f) enemy.stun -= 1.0f / TickRate;
        if (enemy.slow > 0.0f) enemy.slow -= 1.0f / TickRate;
        if (enemy.vulnerabilityTicks > 0) --enemy.vulnerabilityTicks;
        if (enemy.vulnerabilityTicks <= 0) enemy.vulnerability = 0.0f;
        if (enemy.teleportCooldown > 0.0f) enemy.teleportCooldown -= 1.0f / TickRate;
        if (enemy.attackCooldownTicks > 0) --enemy.attackCooldownTicks;
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
        if (enemy.boss) {
            if (enemy.telegraphTicks > 0) {
                --enemy.telegraphTicks;
                if (enemy.telegraphTicks == 0) {
                    lives = std::max(0, lives - content.bossAttackLives);
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
        if (!enemy.boss && enemy.attackCooldownTicks <= 0) {
            AlliedUnit* allyTarget = nullptr;
            float allyDistance = 42.0f * 42.0f;
            for (AlliedUnit& ally : alliedUnitsList) {
                const float distance = distanceSquared(enemy.pos, ally.pos);
                if (ally.alive && distance <= allyDistance) { allyDistance = distance; allyTarget = &ally; }
            }
            DeployableBuilding* buildingTarget = nullptr;
            float buildingDistance = 48.0f * 48.0f;
            for (DeployableBuilding& building : buildings) {
                const float distance = distanceSquared(enemy.pos, building.pos);
                if (building.alive && distance <= buildingDistance) { buildingDistance = distance; buildingTarget = &building; }
            }
            if (allyTarget != nullptr && (buildingTarget == nullptr || allyDistance <= buildingDistance)) {
                allyTarget->hp -= 8.0f;
                enemy.attackCooldownTicks = 24;
            } else if (buildingTarget != nullptr) {
                buildingTarget->hp -= 12.0f;
                enemy.attackCooldownTicks = 30;
            }
        }
        const float slowFactor = enemy.slow > 0.0f ? std::max(0.25f, 0.45f - static_cast<float>(supportLevel) * 0.005f) : 1.0f;
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
    const int before = counters.damageDealt;
    applyDamage(enemy, damage);
    if (owner != SkillId::Count) counters.skillDamage[static_cast<std::size_t>(owner)] += std::max(0, counters.damageDealt - before);
}

void GameSim::updateSkillZones() {
    for (SkillZone& zone : zones) {
        if (!zone.alive) continue;
        if (zone.armTicks > 0) { --zone.armTicks; continue; }
        const float radiusSq = zone.radius * zone.radius;
        for (Enemy& enemy : enemyList) {
            if (!enemy.alive || distanceSquared(enemy.pos, zone.center) > radiusSq) continue;
            if (zone.ownerSkill == SkillId::GravityWell) {
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
                enemy.pos.x = std::max(92.0f, enemy.pos.x - zone.valueA);
                enemy.vulnerability = std::max(enemy.vulnerability, 0.18f);
                enemy.vulnerabilityTicks = std::max(enemy.vulnerabilityTicks, TickRate * 3);
                enemy.slow = std::max(enemy.slow, 2.0f);
                zone.triggered = true;
                ++counters.reactionTriggers;
            } else if (zone.ownerSkill == SkillId::CryoField) {
                enemy.slow = std::max(enemy.slow, zone.valueA);
                if (zone.valueB > 0.0f) enemy.stun = std::max(enemy.stun, zone.valueB / 10.0f);
                ++counters.skillTargets[static_cast<std::size_t>(zone.ownerSkill)];
                ++counters.skillControlTicks[static_cast<std::size_t>(zone.ownerSkill)];
            }
        }
        if (zone.remainingTicks > 0) --zone.remainingTicks;
        if (zone.remainingTicks == 0 || (zone.ownerSkill == SkillId::PhaseMine && zone.triggered)) zone.alive = false;
    }
    zones.erase(std::remove_if(zones.begin(), zones.end(), [](const SkillZone& zone) { return !zone.alive; }), zones.end());
}

void GameSim::updateAlliedUnits() {
    for (AlliedUnit& unit : alliedUnitsList) {
        if (!unit.alive) continue;
        if (unit.lifetimeTicks > 0) --unit.lifetimeTicks;
        if (unit.buffTicks > 0) --unit.buffTicks;
        else { unit.damageScale = 1.0f; unit.speedScale = 1.0f; }
        if (unit.attackCooldownTicks > 0) --unit.attackCooldownTicks;
        Enemy* target = nullptr;
        float bestDistance = 100000000.0f;
        for (Enemy& enemy : enemyList) {
            if (!enemy.alive) continue;
            const float distance = distanceSquared(unit.pos, enemy.pos);
            if (distance < bestDistance || (distance == bestDistance && (target == nullptr || enemy.id < target->id))) { bestDistance = distance; target = &enemy; }
        }
        if (target == nullptr) { unit.pos.x += unit.speed * unit.speedScale / static_cast<float>(TickRate); }
        else if (bestDistance <= 78.0f * 78.0f && unit.attackCooldownTicks <= 0) {
            applySkillDamage(*target, unit.damage * unit.damageScale, unit.ownerSkill);
            emitSkillVisualEvent(unit.ownerSkill, SkillVisualPhase::Hit, target->pos, unit.role == "disruptor" ? 18.0f : 12.0f, 4, unit.role);
            unit.attackCooldownTicks = unit.role == "striker" || unit.role == "drone" || unit.role == "disruptor" ? 12 : 20;
            if (unit.role == "disruptor") target->slow = std::max(target->slow, 2.0f);
            if (unit.role == "bulwark") target->stun = std::max(target->stun, 0.15f);
        } else {
            const float direction = target->pos.x >= unit.pos.x ? 1.0f : -1.0f;
            unit.pos.x += direction * unit.speed * unit.speedScale / static_cast<float>(TickRate);
            unit.pos.y += (target->pos.y - unit.pos.y) * 0.08f;
        }
        if (unit.hp <= 0.0f || unit.lifetimeTicks == 0 || unit.pos.x > static_cast<float>(Width + 100) || unit.pos.x < 0.0f) unit.alive = false;
    }
    alliedUnitsList.erase(std::remove_if(alliedUnitsList.begin(), alliedUnitsList.end(), [](const AlliedUnit& unit) { return !unit.alive; }), alliedUnitsList.end());
}

void GameSim::updateBuildings() {
    for (DeployableBuilding& building : buildings) {
        if (!building.alive) continue;
        if (building.lifetimeTicks > 0) --building.lifetimeTicks;
        if (building.spawnCooldownTicks > 0) --building.spawnCooldownTicks;
        if (building.attackCooldownTicks > 0) --building.attackCooldownTicks;
        if (building.role == "barracks" || building.role == "armory") {
            if (building.spawnCooldownTicks <= 0) {
                spawnAlliedUnit({building.pos.x + 18.0f, building.pos.y}, SkillId::ForwardBarracks, building.role == "armory" ? "striker" : "soldier", 240, building.role == "armory" ? 72.0f : 48.0f, building.role == "armory" ? 18.0f : 8.0f, building.role == "armory" ? 44.0f : 52.0f);
                building.spawnCooldownTicks = building.role == "armory" ? 70 : 45;
            }
        } else if (building.attackCooldownTicks <= 0) {
            Enemy* target = nullptr;
            float bestDistance = 260.0f * 260.0f;
            for (Enemy& enemy : enemyList) if (enemy.alive && distanceSquared(building.pos, enemy.pos) < bestDistance) { bestDistance = distanceSquared(building.pos, enemy.pos); target = &enemy; }
            if (target != nullptr) {
                if (building.role == "mortar") damageArea(target->pos, 78.0f, 22.0f, false);
                else applySkillDamage(*target, 18.0f, building.ownerSkill);
                emitSkillVisualEvent(building.ownerSkill, SkillVisualPhase::Hit, target->pos, building.role == "mortar" ? 78.0f : 18.0f, 4, building.role);
                building.attackCooldownTicks = building.role == "mortar" ? 32 : 14;
            }
        }
        if (building.hp <= 0.0f || building.lifetimeTicks == 0) building.alive = false;
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

void GameSim::applyDamage(Enemy& enemy, float damage) {
    if (!enemy.alive) return;
    if (selectedSupport == SupportModule::CorrosionAmp && (enemy.burn > 0.0f || enemy.poison > 0.0f)) damage *= 1.20f + static_cast<float>(workshopSupportLevels[static_cast<std::size_t>(selectedSupport)]) * 0.01f;
    damage *= 1.0f + std::max(0.0f, enemy.vulnerability);
    const float dealt = std::max(0.0f, damage * (1.0f - enemy.damageResistance));
    enemy.hp -= dealt;
    counters.damageDealt += static_cast<int>(std::round(dealt));
    if (enemy.hp <= 0.0f) resolveDeath(enemy);
}

void GameSim::resolveDeath(Enemy& enemy) {
    if (!enemy.alive) return;
    enemy.alive = false;
    ++counters.kills;
    currency += enemy.boss ? 100 : 5;
    const std::uint8_t supportLevel = workshopSupportLevels[static_cast<std::size_t>(selectedSupport)];
    if (selectedSupport == SupportModule::CreditRelay) currency += enemy.boss ? 30 + supportLevel * 2 : 2 + supportLevel;
    if (selectedSupport == SupportModule::RepairDrones && counters.kills % std::max(3, 8 - static_cast<int>(supportLevel) / 4) == 0) lives = std::min(maxLives, lives + 1);
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
    if (enemyList.empty() && spawnedThisWave >= waveSpawnTarget) {
        if (!endlessMode && wave >= 10) { victory = true; counters.wave = wave; return; }
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
    add(static_cast<std::uint32_t>(nextEnemyId));
    addBool(gameOver); addBool(victory); addBool(upgradeChoicePending); addBool(automaticUltimate); addBool(endlessMode);
    add(static_cast<std::uint32_t>(upgradeRerolls));
    add(static_cast<std::uint32_t>(selectedWeapon)); add(static_cast<std::uint32_t>(selectedChassis)); add(static_cast<std::uint32_t>(selectedSupport)); add(static_cast<std::uint32_t>(selectedSkull)); add(static_cast<std::uint32_t>(selectedSkulls));
    add(static_cast<std::uint32_t>(selectedUltimate)); add(static_cast<std::uint32_t>(selectedEvolution)); add(static_cast<std::uint32_t>(selectedUltimateModule)); add(static_cast<std::uint32_t>(selectedArena));
    add(static_cast<std::uint32_t>(nextSkillCastSequence));
    for (std::size_t slot = 0; slot < SkillSlotCount; ++slot) { add(static_cast<std::uint32_t>(skillLoadoutState.skills[slot])); add(static_cast<std::uint32_t>(skillCooldowns[slot])); add(static_cast<std::uint32_t>(skillCharges[slot])); for (const unsigned char c : skillLoadoutState.nodeBuilds[slot]) add(c); add(0u); }
    for (const SkillId skill : requiredSkills) add(static_cast<std::uint32_t>(skill));
    add(0u);
    for (const SkillId skill : forbiddenSkills) add(static_cast<std::uint32_t>(skill));
    add(0u);
    for (const std::string& branch : allowedSkillBranches) { for (const unsigned char c : branch) add(c); add(0u); }
    add(static_cast<std::uint32_t>(nextAllyId)); add(static_cast<std::uint32_t>(nextBuildingId)); add(static_cast<std::uint32_t>(nextZoneId));
    add(static_cast<std::uint32_t>(workshopTowerCoreLevel));
    for (std::uint8_t level : workshopModuleLevels) add(static_cast<std::uint32_t>(level));
    for (std::uint8_t level : workshopSupportLevels) add(static_cast<std::uint32_t>(level));
    add(static_cast<std::uint32_t>(counters.ticks)); add(static_cast<std::uint32_t>(counters.wave)); add(static_cast<std::uint32_t>(counters.kills));
    add(static_cast<std::uint32_t>(counters.leaks)); add(static_cast<std::uint32_t>(counters.upgrades)); add(static_cast<std::uint32_t>(counters.ultimates)); add(static_cast<std::uint32_t>(counters.shotsFired)); add(static_cast<std::uint32_t>(counters.bossAttacks)); add(static_cast<std::uint32_t>(counters.score)); add(static_cast<std::uint32_t>(counters.damageDealt)); add(static_cast<std::uint32_t>(counters.reactionTriggers)); add(static_cast<std::uint32_t>(counters.statusApplications)); add(static_cast<std::uint32_t>(counters.skillCasts)); add(static_cast<std::uint32_t>(counters.failedSkillCasts));
    for (const auto& values : {counters.skillDamage, counters.skillHealing, counters.skillTargets, counters.skillControlTicks, counters.skillSummons}) for (const int value : values) add(static_cast<std::uint32_t>(value));
    addSize(enemyList.size());
    for (const Enemy& enemy : enemyList) {
        add(static_cast<std::uint32_t>(enemy.id)); add(static_cast<std::uint32_t>(enemy.type)); add(static_cast<std::uint32_t>(enemy.phase));
        addBool(enemy.boss); addBool(enemy.alive); addFloat(enemy.pos.x); addFloat(enemy.pos.y); addFloat(enemy.hp); addFloat(enemy.maxHp); addFloat(enemy.speed); addFloat(enemy.radius);
        addFloat(enemy.slow); addFloat(enemy.stun); addFloat(enemy.burn); addFloat(enemy.burnDps); add(static_cast<std::uint32_t>(enemy.burnTicks));
        addFloat(enemy.poison); addFloat(enemy.poisonDps); add(static_cast<std::uint32_t>(enemy.poisonTicks)); addFloat(enemy.vulnerability); add(static_cast<std::uint32_t>(enemy.vulnerabilityTicks)); add(static_cast<std::uint32_t>(enemy.attackCooldownTicks)); add(static_cast<std::uint32_t>(enemy.telegraphTicks)); addFloat(enemy.damageResistance); addFloat(enemy.teleportCooldown);
    }
    addSize(projectileList.size());
    for (const Projectile& projectile : projectileList) {
        addBool(projectile.alive); addBool(projectile.explosive); addFloat(projectile.pos.x); addFloat(projectile.pos.y); addFloat(projectile.velocity.x); addFloat(projectile.velocity.y);
        addFloat(projectile.damage); addFloat(projectile.radius); add(static_cast<std::uint32_t>(projectile.pierces)); add(static_cast<std::uint32_t>(projectile.bounces));
    }
    addSize(alliedUnitsList.size());
    for (const AlliedUnit& unit : alliedUnitsList) { add(static_cast<std::uint32_t>(unit.id)); addFloat(unit.pos.x); addFloat(unit.pos.y); addFloat(unit.hp); addFloat(unit.maxHp); addFloat(unit.damage); addFloat(unit.damageScale); addFloat(unit.speedScale); add(static_cast<std::uint32_t>(unit.attackCooldownTicks)); add(static_cast<std::uint32_t>(unit.lifetimeTicks)); add(static_cast<std::uint32_t>(unit.ownerSkill)); for (const unsigned char c : unit.role) add(c); add(0u); }
    addSize(buildings.size());
    for (const DeployableBuilding& building : buildings) { add(static_cast<std::uint32_t>(building.id)); addFloat(building.pos.x); addFloat(building.pos.y); addFloat(building.hp); addFloat(building.maxHp); add(static_cast<std::uint32_t>(building.lifetimeTicks)); add(static_cast<std::uint32_t>(building.spawnCooldownTicks)); add(static_cast<std::uint32_t>(building.attackCooldownTicks)); add(static_cast<std::uint32_t>(building.ownerSkill)); for (const unsigned char c : building.role) add(c); add(0u); }
    addSize(zones.size());
    for (const SkillZone& zone : zones) { add(static_cast<std::uint32_t>(zone.id)); addFloat(zone.center.x); addFloat(zone.center.y); addFloat(zone.radius); add(static_cast<std::uint32_t>(zone.remainingTicks)); add(static_cast<std::uint32_t>(zone.armTicks)); addFloat(zone.valueA); addFloat(zone.valueB); add(static_cast<std::uint32_t>(zone.ownerSkill)); addBool(zone.triggered); addBool(zone.pullsToEdge); }
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
