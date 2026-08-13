# Tower Ascend — Luna Implementation Roadmap

This document describes the next major product pass for Tower Ascend. The goal is to turn the current playable prototype into a game with a clear front end, understandable choices, meaningful long-term progression, and runs that reward building around a loadout instead of allowing arbitrary upgrade choices to win.

The current game already has a deterministic combat simulation, five weapons, fifteen in-run upgrades, four skull modifiers, five ultimates, three arenas, a daily challenge, cosmetic shards, profiles, replays, authored JSON content, and headless balance tools. Preserve those systems while reorganizing how the player enters a run and how choices are communicated and balanced.

## Current implementation checkpoint

This roadmap is being continued from the recent frontend, content, and progression pass. The following pieces are already present in the worktree and should be treated as the baseline for the next work:

| Area | Current baseline | Planning consequence |
|---|---|---|
| Frontend flow | Main Menu, Run Type, Loadout, Modifier Select, Workshop, Collection, and Settings are represented in `src/app_state.hpp` and routed by `src/main.cpp`. | Extend the existing state flow for skill loadouts and Workshop skill trees; do not design a second frontend shell. |
| Hit detection | Shared rectangles and `contains()` helpers are centralized in `src/ui_layout.hpp`, with UI-layout tests. | Add skill-bar and targeting rectangles to the same layout source; rendering and clicks must continue to consume one geometry definition. |
| Daily content | Authored Daily recipes, briefings, enemy/skull descriptions, required equipment, rotation validation, and first-clear Legend Core claims exist. | Add skill restrictions, loaned skills, and talent-branch rules to the existing Daily recipe model. |
| Workshop/profile | Core Parts, Legend Cores, tower chassis, support modules, skill unlocks, node ranks, equipped skill IDs, presets, purchases, and profile migration are present in schema 15. | Continue content expansion and balance tuning without changing existing currency semantics. |
| Ultimate system | Ultimate cooldowns, evolutions, sidegrades, manual activation, replay events, and authored content exist. The legacy `auto_ultimate` field is retained only for compatibility and is ignored by new runs. | Keep ultimate activation on the shared manual-cast contract and preserve the compatibility reader for old data. |
| Balance tools | Headless balance policies, weapon/arena/profile matrices, evolution/module cells, and terminal-run checks are present. | Add skill-cast policies and skill contribution telemetry after the first deterministic skill slice works. |
| Content pipeline | Authored JSON manifests, content fingerprints, loaders, and content validation cover skills, trees, statuses, allies, buildings, and reusable authored operations. | Add new content by composing validated operations and metadata before introducing new simulation primitives. |

The active-skill foundation, summon layer, Workshop trees, Daily restrictions, replay format, and headless balance policies are now implemented. Remaining work is content production, richer operation coverage, tuning, and playtesting; existing changes must keep passing while those are expanded.

## 1. Product goals

The next version should make these promises to the player:

1. The main menu makes every major activity obvious.
2. The run setup screen explains what the selected tower, module, modifier, arena, and run type will do.
3. The workshop gives satisfying persistent progression without making early runs trivial.
4. Every selectable item has a name, short description, mechanical details, and synergy guidance.
5. A random collection of upgrades should usually fail at high waves.
6. A coherent build that matches the tower loadout should create a visible force multiplier and reach higher waves.
7. Difficulty should come from readable strategic requirements, not hidden rules or unavoidable damage.
8. The player brings five manually activated skills plus one separately equipped ultimate, creating a second layer of tactical decisions beyond the upgrade draft.

## 2. Target player flow

Introduce an explicit application state machine instead of treating the current loadout screen as the game's front page.

```text
Boot
  -> Main Menu
       -> Start Run
            -> Run Type
            -> Loadout
            -> Modifiers
            -> Run
            -> Results / Rewards
       -> Workshop
            -> Tower Upgrades
            -> Module Upgrades
            -> Skill Trees
            -> Unlocks
       -> Collection / Codex
       -> Settings
       -> Quit
```

Required states:

- `MainMenu`: Start Run, Workshop, Collection/Codex, Settings, Quit.
- `RunTypeSelect`: Standard, Endless, Daily Challenge, and later a challenge/custom mode.
- `LoadoutSelect`: tower chassis, weapon module, five active skills, ultimate, and optional support module.
- `ModifierSelect`: arena and skull/risk modifiers, with reward and difficulty previews.
- `Run`: existing deterministic simulation.
- `UpgradeDraft`: existing wave-end selection, expanded with synergy information.
- `Results`: performance breakdown, rewards, build summary, and next action.
- `Workshop`: persistent tower/module progression.

Keep navigation consistent across mouse, touch, keyboard, and controller. Every screen needs Back, Confirm, focus indication, and a predictable default selection. Use one shared rectangle/layout definition for rendering and hit detection so button visuals and clickable areas cannot drift apart.

## 3. Main menu and run setup

### Main menu

The first screen should contain:

- `START RUN`: opens run-type selection.
- `WORKSHOP`: opens persistent progression.
- `COLLECTION`: browses discovered towers, modules, upgrades, enemies, and synergies.
- `SETTINGS`: opens the existing accessibility, audio, input, and display controls.
- `QUIT`: desktop only; mobile uses platform navigation.
- A compact resource header showing all persistent currencies.
- Current profile summary: best wave, best score, completed runs, and selected tower.

### Run types

Implement run types as authored definitions rather than scattered conditionals.

| Run type | Purpose | Progression | Suggested rules |
|---|---|---|---|
| Standard | Main balanced experience | Full rewards | Fixed final wave and boss |
| Endless | Test build strength | Rewards with diminishing returns | Scaling waves until defeat |
| Daily | Shared deterministic challenge | Daily bonus | Fixed seed, loadout constraints, modifiers |
| Challenge | Hand-authored puzzle run | One-time reward | Special enemy mix or restricted modules |

Each card must show expected duration, wave structure, reward multiplier, restrictions, and whether workshop upgrades apply.

### Daily Challenge mission briefing

The Daily Challenge must feel like a deliberately designed mission, not a Standard run with a different seed. Each UTC date should select a complete deterministic challenge recipe containing a theme, tower setup rule, enemy roster, wave profile, skulls, special mutators, objective, and first-clear reward.

Before the player enters loadout selection, show a full briefing card with:

- A distinct mission title, such as `FROZEN CIRCUIT`, `LAST SHELL`, or `SWARM PROTOCOL`.
- A short two- or three-sentence description explaining the fantasy and strategic problem.
- Required tower chassis, weapon, active skills, ultimate, support module, or Legendary Evolution.
- Any equipment that is fixed, required, forbidden, or freely selectable.
- Active skull modifiers and their exact effects.
- Special Daily-only mutators and their exact effects.
- Arena, number of waves, boss or final threat, and expected run duration.
- Enemy roster with icons, names, traits, and approximate prevalence: common, frequent, rare, boss.
- A threat summary such as `HIGH SWARM DENSITY`, `ARMORED TARGETS`, or `CONTROL RESISTANCE`.
- Suggested strategy and upgrade tags without revealing one mandatory draft sequence.
- First-clear Legend Cores and ordinary repeat rewards.
- Whether workshop bonuses are normalized, disabled, or active.

The briefing should provide both concise and expanded views. The card itself gives the short description and major threats; hovering, focusing, or tapping skull/enemy/mutator icons opens their full descriptions. On touch, the first tap reveals details and the second confirms.

### Daily loadout rules

Use several loadout-rule patterns so different days ask the player to construct or pilot different towers:

- `Fixed`: the full tower setup is supplied. The challenge tests adapting upgrade choices to that build.
- `Required Core`: weapon or chassis is fixed, while ultimate/support modules remain selectable.
- `Required Synergy`: two components are fixed and the player chooses the remaining slot to complete or cover the build.
- `Restricted Pool`: one or more weapons, elements, or modules are forbidden, forcing an unusual solution.
- `Drafted Loadout`: choose one of three authored tower packages before the run.
- `Open Counterbuild`: all equipment is available, but the enemy roster strongly rewards a specific kind of response.

If a Daily requires equipment the player has not permanently unlocked, lend it for that Daily only. Daily completion must test strategy rather than account age. A loaned Legendary Evolution is clearly marked and does not become permanently owned afterward.

The player must not be allowed to begin while a required slot is invalid. Explain the problem directly, for example: `TODAY REQUIRES A FROST PRIMARY MODULE`. The final confirmation panel repeats the complete tower setup and challenge rules.

### Daily themes and examples

| Mission | Setup requirement | Enemies and modifiers | Intended gameplay |
|---|---|---|---|
| `Frozen Circuit` | Frost Blaster required; choose Arcane or control support | Runners, Shielded enemies, Haste skull; enemies take more electric damage while slowed | Build Ice + Electric control and chain damage |
| `Last Shell` | Cannon plus Extinction Spear Meteor evolution | Tanks and boss elites; fewer enemies, greatly increased health | Focus burn or armor-breaking single-target damage |
| `Swarm Protocol` | Railgun required; support slot freely chosen | Swarmlings and Grunts, Swarm skull, denser spawn groups | Solve Railgun's weak area coverage through ricochet, pierce, or an AOE ultimate |
| `Toxic Transit` | Poison Coil plus the Phase Mine skill supplied; remaining skills selectable | Teleporters and fast enemies; displaced enemies become vulnerable | Build Poison + displacement burst damage |
| `Blackout` | Arcane Beam required; Energy Surge evolution chosen by player | Shielded enemies, reduced ultimate charge, periodic power surges | Decide between weapon overdrive and chain/status amplification |
| `No Safe Distance` | Short-range chassis; no Frost module | Runners and teleporters; Haste + Glass Cannon | Use stun, knockback, rewind, and fast target acquisition |
| `Burning Economy` | Cannon or fire-tagged module required | Greed skull; burning kills award Credits, direct kills award less | Invest in damage over time and decide when economy is worth delayed damage |

These are templates, not seven permanently repeated dailies. Build a library of authored themes, then deterministically vary compatible arena, enemy mix, objective, required module, and mutator values within validated limits.

### Daily generation and rotation

Replace the current date hash that independently chooses one weapon, skull, and arena with a recipe-based generator:

1. Hash the UTC date into a stable daily ID and seed.
2. Choose an authored theme that has not appeared recently.
3. Select only compatible loadout rules, arenas, enemies, skulls, objectives, and mutators declared by that theme.
4. Produce the fixed or constrained tower setup.
5. Validate that the setup has at least two viable upgrade routes and that all required equipment can be loaned.
6. Run headless policy simulations before accepting a recipe in the authored pool.

Rotation rules:

- Do not repeat the same theme, primary weapon, dominant enemy trait, or skull combination on consecutive days.
- Over a rolling week, feature several weapons, enemy pressures, and strategic roles.
- Avoid impossible combinations, such as control-immune enemies in a challenge whose only intended solution is control.
- Avoid a Daily where the supplied tower automatically wins without meaningful upgrade choices.
- Keep the challenge definition deterministic and include its complete recipe in replay/content fingerprints.
- If generated recipes are not validated by a server, ship a locally validated calendar or a set of validated recipes selected deterministically by date.

Suggested authored Daily fields:

- `id`, `title`, `short_description`, `long_description`, and `theme_tags`.
- `loadout_rule`, required/fixed/forbidden equipment IDs, and loan policy.
- Required, fixed, forbidden, or loaned skill IDs and allowed talent-branch rules.
- `arena_pool`, `wave_profile`, `enemy_roster`, and final threat.
- `skull_mask` and Daily-only `mutators`.
- `objective`, optional bonus objectives, and failure conditions.
- `recommended_upgrade_tags` and threat-summary text.
- First-clear and repeat rewards.
- Workshop normalization policy.
- Compatibility constraints and balance-policy thresholds.

### Loadout screen

Split the current dense loadout screen into clear steps or columns:

- Tower chassis: defines base identity, such as attack speed, range, module slots, or durability.
- Primary weapon module: Rapid Fire, Cannon, Arcane Beam, Frost, or Railgun.
- Ultimate module: current five ultimates.
- Active skills: equip up to five distinct skills and preview each skill's target type, cooldown, role, and purchased talent branch.
- Support module: introduce later for economy, control, defense, or status specialization.
- Arena and skull modifiers: move to a dedicated modifier step.

Display live derived statistics: damage, attacks per second, range, area damage, control strength, economy value, and expected role. When a selection changes, highlight which values changed.

Before confirmation, show a compact build summary:

- Strengths, such as `STRONG: SWARMS / BURN / AREA`.
- Weaknesses, such as `WEAK: SHIELDS / FAST TARGETS`.
- Preferred upgrade tags, such as `SEEK: EXPLOSIVE + FIRE + WIND`.
- Run difficulty and reward multiplier.

## 4. Workshop and persistent progression

The workshop should create long-term goals while preserving the importance of in-run decisions.

### Currency model

Use visually and mechanically distinct currencies:

- `Credits`: earned during a run and spent only during that run if a run shop is added.
- `Core Parts`: persistent progression currency earned from completed runs and milestones.
- `Shards`: cosmetic currency; keep the existing cosmetic role.
- `Legend Cores`: rare late-game currency used for transformative ultimate evolutions and, later, legendary weapon modules. The first successful Daily Challenge clear for each UTC date awards a small amount.
- Optional `Blueprints`: unlock new tower chassis or modules through achievements/challenges rather than raw grinding.

Never use one icon or color for multiple currencies. The results screen must explain exactly why each reward was earned.

#### Daily first-clear rewards

- The first successful Daily Challenge clear for a given UTC date awards `Legend Cores` in addition to its normal Shards/Core Parts reward.
- Replaying that same Daily can improve score or earn ordinary rewards, but cannot award its Legend Cores again.
- Store claimed daily IDs in the profile using the authored challenge date/seed identity, not only a single “last claimed date” field.
- Clearly mark the Daily card as `LEGEND CORE AVAILABLE`, `CLAIMED`, or `NOT COMPLETED`.
- Show the first-clear reward before starting and itemize it on the Results screen.
- Keep a slow non-daily source, such as major progression milestones or difficult challenge first-clears, so a player who misses days is delayed rather than permanently locked out.
- Decide whether Daily mode applies workshop bonuses. For comparable challenge scores, the preferred rule is to normalize ordinary workshop stats while still allowing the selected legendary evolution if it is part of the Daily loadout rules.

### Workshop categories

#### Tower chassis upgrades

Examples:

- Base integrity or one additional leak allowance.
- Small targeting-range improvements.
- Additional module slot unlocked at a major progression milestone.
- A choice between mutually exclusive chassis specializations.

#### Weapon module upgrades

Examples:

- Rapid Fire: kinetic calibration, magazine behavior, crit or pierce specialization.
- Cannon: blast radius, impact force, burn payload specialization.
- Arcane Beam: chaining, sustained damage, electric reaction specialization.
- Frost: slow strength, freeze threshold, shatter specialization.
- Railgun: charge speed, overkill pierce, marked-target specialization.

#### Ultimate module upgrades

Use sidegrades such as shorter cooldown versus stronger effect, or focused versus broad targeting. Avoid a single linear damage ladder.

#### Active skill trees

Each unlocked active skill has its own node tree. Early nodes improve broadly useful properties such as cooldown, range, duration, charges, or reliability. The tree then branches into mutually exclusive specializations that change the skill's tactical role. Purchased nodes are persistent, but the player selects one legal build for each equipped skill before a run. Respeccing between runs should be free or inexpensive enough to encourage experimentation.

Use Blueprints or challenge milestones to unlock a skill, then Core Parts for ordinary nodes. Reserve Legend Cores for ultimate evolutions and truly legendary skill capstones only; do not make every usable skill depend on Daily grinding.

#### Legendary ultimate evolutions

After substantial progression, each ultimate can unlock one mutually exclusive Legendary Evolution using Legend Cores. An evolution should visibly change how the ultimate plays and push the entire loadout toward a specialization. It is equipped in the loadout screen, not permanently active merely because it was purchased.

The player may own several evolutions but equip only one evolution for the selected ultimate per run. Switching is free between runs. Every evolution needs a strength, a tradeoff, matching tags, and distinctive presentation. Suggested concepts:

| Ultimate | Legendary evolution | Specialization and behavior | Tradeoff |
|---|---|---|---|
| Meteor Rain | `Solar Aftermath` | Impacts leave burning zones; weapon hits against burning enemies deal more damage. Strong with Cannon, Arcane fire, Wind, and damage-over-time builds. | Lower immediate impact damage and longer cooldown. |
| Meteor Rain | `Extinction Spear` | All meteors converge on the highest-health target for extreme single-target and armor-breaking damage. Strong for bosses and Railgun builds. | Loses most swarm-clearing coverage. |
| Meteor Rain | `Shattered Sky` | Many smaller impacts cover the whole lane, applying vulnerability so subsequent area damage is amplified. | Weak direct boss damage. |
| Bullet Storm | `Resonant Arsenal` | The tower continues firing during the storm; every weapon hit builds stacks that increase weapon damage and attack speed for a short period. | Ultimate projectiles deal less independent damage. |
| Bullet Storm | `Suppressive Grid` | Projectiles pin and slow enemies, with repeated hits escalating to a brief stun. Strong for Frost, Electric, and control builds. | Reduced damage and poor value against control-resistant bosses. |
| Bullet Storm | `Execution Protocol` | Concentrates fire on marked/highest-health targets and deals increasing damage to the same target. | Very little area coverage and stacks reset on target change. |
| Absolute Zero | `Brittle Singularity` | Frozen enemies become brittle; weapon hits and explosive damage can shatter them for area damage. Strong with Cannon and Railgun burst. | Shorter global freeze and bosses cannot be instantly shattered. |
| Absolute Zero | `Permafrost Engine` | Creates a long-duration slowing field and increases damage-over-time duration while enemies remain inside it. Strong with Poison and Burn. | Minimal immediate damage. |
| Absolute Zero | `Cold Conductor` | Frozen/slowed targets chain electric damage between one another and take increased module damage. Strong with Arcane Beam and Chain Lightning. | Less effective when enemies are spread out. |
| Gravity Shift | `Event Horizon` | Pulls enemies into one persistent kill zone that amplifies area damage and damage over time. Strong with Cannon, Gravity Well, Burn, and Poison. | Does not rewind enemies as far and can be weak against a lone boss. |
| Gravity Shift | `Chrono Reversal` | Rewinds the most advanced enemies much farther and restores a portion of tower cooldowns for each enemy displaced. | Low direct damage and a cap on cooldown restoration. |
| Gravity Shift | `Mass Driver` | Collapses gravity onto one target; nearby enemies and projectiles are redirected into it for high single-target damage. | Sacrifices broad crowd control. |
| Energy Surge | `Overdrive Link` | Temporarily empowers the equipped weapon module: faster attacks, improved status application, and stronger upgrade reactions. | The surge itself deals no global burst damage. |
| Energy Surge | `Chain Reactor` | Damage jumps across enemies and each active status adds another jump or damage bonus. Strong with mixed-element builds. | Weak against isolated targets and has a lower base multiplier. |
| Energy Surge | `Terminal Discharge` | Consumes stored ultimate energy for a massive execute-style hit against bosses or the highest-health enemy. | Resets supportive buffs and has the longest cooldown. |

Additional evolution directions for later content:

- Summon or drone play: an ultimate becomes a temporary autonomous module.
- Economy play: marked enemies drop additional run Credits, but the ultimate loses damage.
- Defensive play: converts damage into repairs, shields, or leak prevention.
- Status conversion: consumes Burn, Poison, Freeze, or Shock stacks for a burst, then removes them.
- Combo play: ultimate strength scales with the number of different upgrade tags in the current build.
- Purist play: an ultimate becomes stronger when the loadout commits to one tag and avoids conflicting tags.

Legendary Evolutions should be authored in a new `ultimate_evolutions.json` file with stable IDs, parent ultimate, description, cost, unlock condition, tags, conflicts, numerical effects, and presentation IDs. Their effects must be represented in replay data and the content fingerprint.

Unlock pacing proposal:

- Reveal evolutions in the Workshop early so players have a long-term target.
- Require an ultimate mastery condition before purchase, such as using that ultimate in several completed runs or reaching a wave milestone with it.
- Price the first evolution as a meaningful multi-day goal, not a multi-week wall; later evolutions can cost more.
- Award enough Legend Cores from a first Daily clear that progress is always visible, while requiring several first-clears for one evolution.
- Never sell raw Legend Core power as a mandatory linear tier. The expensive reward is a new rule set and specialization.

### Progression guardrails

- Permanent bonuses should be modest; target roughly 10–20% total raw-power growth across a complete workshop branch.
- Most workshop nodes should unlock options, alter behavior, or improve consistency rather than simply multiply damage.
- Standard balance tests need two baselines: fresh profile and expected mid-progression profile.
- Daily and challenge modes may normalize or disable workshop bonuses for fair deterministic comparisons.
- Legendary Evolutions must be included in separate balance matrix cells; each should create a specialization with a measurable weakness, not improve every matchup.
- Every purchase requires a confirmation panel showing cost, owned currency, resulting balance, and the exact mechanical change.
- Profile schema must be versioned and migrated; never invalidate existing saves.

## 5. Active skills, summons, and node-based skill trees

Active skills are reusable, manually activated abilities with individual cooldowns. They include transformed versions of active-like effects already in the upgrade pool, such as Black Hole and Teleport Trap, plus summons, temporary buildings, buffs, debuffs, healing, and deployable towers. They are chosen before a run and should complement the tower loadout rather than replace its weapon.

### Loadout and activation rules

- A normal loadout has exactly five active-skill slots and one separate ultimate slot. The ultimate never consumes a skill slot.
- Skills are distinct by default; duplicate copies are illegal unless a future mode explicitly enables them.
- Every skill declares one target mode: `None`, `WorldPoint`, `Area`, `Lane`, `Enemy`, `Ally`, `Direction`, or `Placement`.
- Pressing a skill button selects it and enters targeting mode. Show range, area, placement footprint, affected units, and whether the current target is legal. Confirm casts; Back/right-click cancels without spending a charge or starting cooldown.
- For quick self-cast and no-target skills, one press may cast immediately. Provide an optional quick-cast setting only after the confirm/cancel path is reliable.
- Invalid targets must give immediate visual/audio feedback and a short reason such as `OUT OF RANGE`, `BLOCKED`, or `NO ALLIED TARGET`.
- Cooldowns advance on simulation time, not wall-clock time, and stop while paused. Each skill owns its cooldown and optional charge state independently.
- The ultimate is always manually activated and uses the same targeting contract where applicable, but has a much longer cooldown and stronger presentation. Remove the current auto-ultimate toggle and migrate old profile/replay values to manual activation.
- Daily and Challenge recipes may fix, lend, ban, or restrict skills and talent branches. Loaned skills are usable for that challenge without becoming permanently unlocked.
- Active-like effects moved into this system must leave the passive upgrade draft. Passive weapon modifiers remain draft upgrades; an upgrade may augment an equipped skill only when its text and prerequisites say so explicitly.

Recommended desktop defaults are keys `1` through `5` for skills and a separate key such as `Space` for the ultimate. Controller and touch mappings need radial or focus navigation that does not require pointer precision. Input bindings should name `SkillSlot1` through `SkillSlot5`, `Ultimate`, `ConfirmTarget`, and `CancelTarget` rather than naming specific skills.

### Bottom skill bar and targeting UI

Place a compact skill bar at the bottom-center of the run HUD. It contains five equally sized skill slots and one larger, visually separated ultimate slot. Build it from the same shared `UiElement` geometry used for hit testing.

Each skill slot shows:

- Base skill icon and slot/key label.
- Radial cooldown wipe plus a numeric seconds value; show tenths below one second if useful.
- Charge pips and recharge progress when the skill has multiple charges.
- Ready, selected/targeting, cooling down, disabled, invalid-target, and challenge-loaned states.
- A small role/branch badge and visual layers produced by equipped talents.
- A short tooltip with the resolved values after all workshop nodes and run modifiers.

Talent changes must be recognizable without replacing the base icon with an unrelated image. Use a composable icon grammar: stable base silhouette, branch-colored frame, one or two authored overlay glyphs, charge pips, and an optional altered reticle. Examples include a wide ring for an area branch, a crosshair for single-target specialization, frost edging for slow, soldier pips for additional summons, a shield corner for defensive summons, and a flame/rune layer for damage-over-time. Major branch gateways and capstones change an overlay or frame; minor percentage nodes only update values and need not invent a new icon. Never rely on color alone.

While targeting, dim unrelated HUD controls, keep the selected slot highlighted, and draw an accessible world-space preview. The preview is authoritative: it uses the same range and shape calculation as cast validation. Touch targets must remain large enough at supported UI scales and the bar must respect safe areas.

### Skill-tree structure

Use the same topology for every skill so new trees are easy to author and understand:

```text
Unlock / Root
  -> Tier 1: two or three generic nodes (range, cooldown, duration, reliability)
  -> Tier 2: choose one specialization gateway
       -> Tier 3: branch behavior node + branch efficiency node
       -> Tier 4: branch capstone that changes how the skill is used
```

- Generic nodes may have multiple ranks, but raw-power growth across them should remain bounded.
- Specialization gateways are mutually exclusive for the equipped build. Owning multiple branches is allowed; activating them together is not unless a later node explicitly permits it.
- A capstone should introduce a rule, target preference, interaction, summon type, or effect conversion rather than merely `+50% damage`.
- Every node declares parents, rank limit, cost, tags, conflicts, effect modifications, description fragments, and presentation changes.
- The Workshop previews the entire resolved skill, including cooldown, target shape, affected tags, branch tradeoff, and mutated HUD icon.
- Store purchased ranks separately from the currently equipped tree build. This permits presets and free between-run branch switching without losing progression.

Suggested authored node fields are `id`, `skill_id`, `parent_ids`, `tier`, `max_rank`, `cost`, `branch_id`, `exclusive_group`, `requirements`, `effect_modifiers`, `granted_effects`, `tags`, `icon_layers`, `reticle_id`, `short_description`, and `tradeoff_text`.

### Initial skill catalog and talent proposals

The values below are design directions, not final balance numbers. Every skill needs at least two useful branches and a clear weakness.

| Skill | Target and baseline role | Generic opening nodes | Specialized branch ideas |
|---|---|---|---|
| `Gravity Well` (migrated Black Hole) | Place an area that pulls enemies toward its center for a short duration. | Range, radius, duration, cooldown. | `Event Horizon`: persistent zone, area/DoT amplification, enemies expire into a small pull; `Singularity`: smaller field locks onto the highest-health target and ends in a burst; `Void Current`: weaker pull but drags enemies backward and spreads statuses through the cluster. |
| `Phase Mine` (migrated Teleport Trap) | Place a trap that rewinds the first valid enemy crossing it. | Placement range, arming speed, charges, recharge time. | `Rewind Array`: multiple mines and chained displacement; `Phase Snare`: smaller rewind but long slow and vulnerability; `Unstable Relay`: teleported enemy damages nearby enemies and carries Poison/Burn to its destination. |
| `Vanguard Drop` | Target a point and summon a temporary allied squad that advances toward enemies. | Squad duration, health, summon count, deployment cooldown. | `Bulwark Corps`: shielded soldiers taunt/intercept and protect other allies; `Strike Team`: fewer fast soldiers specialize in single-target damage; `Saboteurs`: attacks apply slow, weakness, or status amplification. |
| `Forward Barracks` | Place a temporary building that periodically produces soldiers. | Building health, lifetime, spawn interval, placement range. | `Recruitment Center`: many inexpensive melee units; `Field Armory`: slower elite/ranged units that scale with weapon tags; `Field Hospital`: fewer fighters, but the building heals and reinforces nearby allies. |
| `Ruin Hex` | Mark an enemy or area and weaken affected enemies. | Radius, duration, cooldown, status reliability. | `Brittle Curse`: armor/shield weakness and stronger direct hits; `Withering Field`: strong slow and reduced control resistance; `Mark of Ruin`: one priority target takes increasing damage from repeated hits and allied soldiers. |
| `Rally Beacon` | Target allied units to heal them and grant a short buff. | Heal amount, buff radius, duration, cooldown. | `Combat Medics`: healing-over-time, cleanse, and one limited revive; `War Cry`: attack/move speed and damage, with less healing; `Fortification`: shields, resistance, and temporary protection for buildings/turrets. |
| `Sentry Fabricator` | Place a small temporary autonomous tower. | Tower duration, range, rotation/acquisition speed, cooldown. | `Gatling Nest`: ramps single-target fire; `Mortar Post`: slow area bombardment and knockback; `Arc Coil`: chain damage and control with lower boss damage. |
| `Cryo Field` | Place a zone that slows enemies and builds toward a brief freeze. | Radius, slow strength, duration, cooldown. | `Permafrost`: long control zone and stronger damage-over-time duration; `Shatter Zone`: shorter field makes frozen targets burst when hit; `Cold Conductor`: slowed targets chain electric/status damage but the field has reduced raw control. |
| `Drone Swarm` | Send support drones to a point; they choose nearby enemies or allies by branch. | Drone count, duration, travel speed, cooldown. | `Hunter Drones`: focus marked/high-health targets; `Disruptors`: spread weakness and slow; `Guardian Drones`: repair, shield, and escort allied soldiers or structures. |

The initial catalog now includes `Gravity Well`, `Phase Mine`, `Vanguard Drop`, `Forward Barracks`, `Ruin Hex`, `Rally Beacon`, `Sentry Fabricator`, `Cryo Field`, `Drone Swarm`, and the data-only `Resonance Pulse` fixture. This exercises area, placement, enemy/ally targeting, charges, summons, debuffs, healing, icon mutation, and all five loadout slots.

### Allied units and deployable buildings

Summons require an explicit allied-combat model rather than cosmetic projectiles:

- Allied units have stable IDs, tags, health, movement, attack cadence, target policy, lifetime, and owner skill ID.
- Define deterministic target priorities and tie-breakers. Equal candidates resolve by progress, distance, then stable entity ID.
- Soldiers advance or intercept in the lane, attack enemies in range, and can be damaged by authored enemy attacks/contact rules. Do not silently make them invulnerable.
- Buildings have a validated placement footprint, health, lifetime, and optional spawn or aura components. They do not block the lane unless a skill explicitly owns and communicates that behavior.
- Healing and buffs query tags such as `Ally`, `Soldier`, `Building`, `Tower`, and `Summon`, preventing one-off type checks for every new spell.
- Summon damage, healing, prevented damage, kills, lifetime, and active-unit peaks are attributed to the originating skill for results and balance telemetry.
- Set authored global and per-skill entity caps. At the cap, apply a documented refresh/replace rule so a summon build cannot exhaust memory or degrade simulation speed.

### Extensible simulation and content architecture

Do not add one enum case and one `GameSim` switch branch for every future skill. Use stable string IDs at the content/save boundary and resolve them to dense runtime indices after validation. The first implementation should introduce these typed concepts:

- `SkillDefinition`: identity, target mode, cooldown/charges, tags, effect list, presentation, and tree reference.
- `SkillTreeDefinition` and `SkillNodeDefinition`: graph topology, costs, modifiers, exclusions, and icon mutations.
- `SkillLoadout`: five skill IDs, ultimate ID, and selected node builds.
- `SkillRuntimeState`: remaining cooldown, charges, recharge progress, cast sequence, and active spawned entities.
- `SkillCastRequest`: slot, cast tick, target kind, quantized world position, optional target entity ID, and facing/direction.
- `EffectDefinition`: a small validated operation plus parameters and target selector.
- `StatusDefinition`, `AllyDefinition`, and `BuildingDefinition`: reusable content referenced by effects.

Begin with a registry of composable effect operations: `Damage`, `Heal`, `ApplyStatus`, `RemoveStatus`, `Pull`, `Push`, `Teleport`, `CreateZone`, `SpawnAlly`, `SpawnBuilding`, `Buff`, `Shield`, and `ModifyCooldown`. A new skill composed only from existing operations should require authored data and presentation assets, not edits to `GameSim`. Add a new C++ operation only when content genuinely needs a new simulation primitive. Avoid a general scripting language in the first pass; typed operations are easier to validate, replay, test, and balance.

Recommended files and ownership:

- `skill_catalog.*`: load and validate definitions/tree graphs; no live run state.
- `skill_system.*`: cooldowns, charges, targeting validation, casts, and effect dispatch in deterministic simulation time.
- `skill_effects.*`: operation registry and reusable selectors.
- `ally_system.*`: allied units, buildings, targeting, combat, caps, and attribution.
- `skill_progression.*`: purchases, legal node builds, respecs, and resolved stats.
- `ui_skill_bar.*` and `ui_targeting.*`: presentation consuming read-only simulation snapshots.
- `skills.json`, `skill_trees.json`, `statuses.json`, `allies.json`, and `buildings.json`: authored content with stable IDs.

Keep `main.cpp` responsible only for routing normalized input, application-state transitions, and calling screen renderers. The simulation must not depend on SDL coordinates. Convert a pointer/controller action into a `SkillCastRequest` at the UI boundary, then let `skill_system` perform authoritative validation.

### Migration of current systems

1. Add the new definitions and runtime state without changing existing combat behavior.
2. Re-author Black Hole as `gravity_well` and Teleport Trap as `phase_mine`; remove them from `Upgrade` draft candidates only when their skill versions pass replay and balance tests.
3. Review Emergency Repair and other active-sounding upgrades. Move Emergency Repair to a future targeted/self-cast repair skill or rewrite it as a clearly passive trigger. Keep weapon modifiers such as Piercing Shots, Burning Shot, and Chain Lightning in the draft.
4. Replace `activateUltimate()` with the shared cast/target contract. Give each ultimate an authored long cooldown, preserve any explicitly designed charge requirement, and keep Legendary Evolution behavior data in the resolved cast.
5. Remove `AutoUltimate` from new input/settings UI. Profile migration ignores or clears the legacy flag; legacy replays retain their recorded activation events where possible.
6. Version profiles and replays. Store stable skill IDs, five equipped slots, selected talent node IDs/ranks, and the content fingerprint.
7. Record a cast event with slot, resolved skill ID, simulation tick, quantized target position, optional target entity ID, and cast sequence. Replays must never recompute a pointer target from screen coordinates.

Add skill telemetry to headless runs: casts, failed casts, cooldown-ready idle time, damage/healing by skill, control uptime, summon contribution, and targets hit. Extend balance policies with `NoSkill`, `RandomCast`, `RoleAwareCast`, and `OracleCast` behaviors so tower/upgrades are not balanced around impossible perfect skill timing.

## 6. Descriptions, tooltips, and the collection screen

Move all player-facing gameplay information into authored content. Names and effect values should not be duplicated in C++ switch statements and JSON.

Every selectable item should support:

- `display`: short name.
- `short_description`: one-line purpose.
- `long_description`: two or three lines explaining behavior.
- `mechanics`: formatted values generated from authored numbers.
- `strengths`: enemy traits or situations it handles well.
- `weaknesses`: situations it handles poorly.
- `tags`: kinetic, explosive, fire, ice, electric, control, economy, etc.
- `synergy_tags`: tags that improve this item.
- `conflict_tags`: combinations that are intentionally inefficient or incompatible.
- `icon_id`: stable visual reference.

Apply this metadata to weapons, upgrades, ultimates, skulls, arenas, enemies, tower chassis, support modules, run types, currencies, and workshop nodes.

### Tooltip behavior

- Mouse: show after roughly 250–350 ms of stable hover; keep visible while moving into the tooltip.
- Touch: first tap selects and reveals details; second tap confirms. Do not require hover.
- Controller/keyboard: focused item always shows its details panel.
- Upgrade draft: descriptions are always visible because the choice is time-critical and there is room on the cards.
- Small screens: use a fixed details panel or modal sheet rather than a floating tooltip.
- Tooltips must stay inside the safe area and never cover the currently focused card.

### Description style

Use three layers of information:

1. Name: `CHAIN LIGHTNING`.
2. Immediate effect: `Hits jump to two nearby enemies.`
3. Build guidance: `Stronger against slowed targets. Best with Frost or electric modules.`

Use actual values when useful: `+2 pierces`, `45% splash damage`, `slows by 45% for 2s`. Avoid vague words such as “greatly” when a number exists.

Add a Collection/Codex screen so players can learn without being inside a run. Locked entries may show unlock conditions while hiding only genuine discoveries such as bosses or secret reactions.

## 7. Build identity and force-multiplier balance

The central balance change is to make upgrade value conditional on the selected loadout and the threats in the run.

### Design model

Each loadout has:

- A primary damage pattern: single-target, area, sustained, burst, or control.
- One or two native tags.
- A weakness that becomes dangerous in later waves.
- Two or more viable build paths that solve that weakness differently.

Upgrades should belong to one of four roles:

- `Core`: directly amplifies the selected weapon's native behavior.
- `Reaction`: combines two tags to produce multiplicative or threshold-based value.
- `Coverage`: solves a weakness, such as giving Railgun swarm control.
- `Utility`: economy, repair, rerolls, or tempo; useful but insufficient as the entire build.

### Force multiplier rules

Do not make every compatible upgrade a flat damage multiplier. Use interactions the player can understand:

- Fire + Wind: burning enemies spread or create a damaging tornado zone.
- Ice + Electric: slowed/frozen enemies take amplified chain damage.
- Poison + Teleport: displacement triggers poison burst damage.
- Kinetic + Pierce: excess damage or consecutive aligned targets increases penetration value.
- Explosive + Stun: stunned clusters take stronger splash or repeated shockwaves.

A complete two- or three-part synergy should produce substantially more effective wave clear than the sum of unrelated upgrades. As a starting target:

- Compatible single upgrade: about 15–30% improvement in its intended situation.
- Two-piece synergy: about 1.5–1.8x effective output in its intended situation.
- Three-piece mature build: about 2.2–3.0x effective output against matching threats.
- Outside its intended situation, the same build should remain useful but not dominant.

These are initial tuning targets, not permanent constants. Measure time-to-kill, leaks, wave reached, and damage by source before finalizing them.

### Upgrade draft changes

The existing draft only biases weights toward a weapon branch. Extend it with:

- Prerequisites and explicit synergy links.
- Upgrade tiers or stack levels.
- At least one relevant option in most drafts, without guaranteeing the perfect option.
- Limited rerolls or a paid skip so bad luck can be managed.
- Threat preview for the next wave, allowing the player to choose coverage intelligently.
- Card labels such as `CORE`, `SYNERGY`, `COVERAGE`, and `UTILITY`.
- A synergy preview: `FROST + CHAIN LIGHTNING -> CONDUCTIVE FREEZE`.
- Duplicate and exclusion rules so mutually incompatible paths do not silently appear together.

Avoid making “correct” mean one predetermined upgrade. Each loadout should have at least two viable high-wave routes, and the enemy composition should determine which route is best in a particular run.

### Enemy and wave pressure

Higher waves should ask specific questions:

- Dense swarm wave: requires area damage, chaining, or strong control.
- Armored/tank wave: requires burst, resistance bypass, or damage-over-time scaling.
- Fast wave: requires targeting speed, slows, stuns, or displacement.
- Shield wave: requires multi-hit pressure, shield break, or a counter tag.
- Mixed wave: tests whether the build has both a specialty and adequate coverage.
- Boss: tests sustained single-target damage while preserving add control.

Give the player advance warning through wave-preview icons and short text. A loss should be explainable: `LOW SWARM CLEAR`, `INSUFFICIENT SHIELD DAMAGE`, or `CONTROL UPTIME TOO LOW`.

## 8. Balance-tool redesign

The current `ta_balance_check` always chooses upgrade option zero and only fails when a run does not reach any terminal state. That proves simulation termination, not strategic balance.

Add multiple deterministic decision policies:

- `RandomPolicy`: chooses randomly; should have a low high-wave clear rate.
- `FirstOptionPolicy`: regression baseline for deterministic behavior.
- `TagMatchPolicy`: chooses upgrades matching the weapon's native tags.
- `ThreatAwarePolicy`: chooses upgrades that counter upcoming enemy traits.
- `SynergyPolicy`: builds toward a declared two- or three-part combination.
- `OraclePolicy`: picks the strongest legal option using known authored relationships; establishes the practical power ceiling.

Report by weapon, arena, run type, profile progression tier, skull mask, policy, and seed:

- Victory rate and average wave reached.
- Leaks and lives remaining.
- Damage by weapon, upgrade, status, reaction, and ultimate.
- Upgrade pick sequence and completed synergies.
- Time to kill by enemy archetype.
- Currency earned/spent and workshop reward.
- Non-terminal runs and simulation errors.

Initial target bands for Standard mode:

| Policy/build quality | Expected result |
|---|---|
| Random unrelated picks | Usually fails before the final waves |
| Coherent tag-matched build | Can reach late waves; mixed victory rate |
| Threat-aware synergy build | High but not guaranteed victory rate |
| Strong build plus difficult skulls | Viable, but meaningfully lower victory rate |

Do not hard-code one universal win-rate target yet. First collect distributions across at least 100 seeds per matrix cell, then set target bands per difficulty and run type. The test should fail on impossible content, trivial universal wins, non-terminal runs, or a synergy policy that performs no better than random.

## 9. Data and architecture changes

Before adding more screens to `src/main.cpp`, split responsibilities:

- `app_state.*`: state stack and transitions.
- `ui_layout.*`: shared visual rectangles, focus order, and hit testing.
- `ui_screens.*` or one file per major screen.
- `content_catalog.*`: typed definitions and descriptions loaded from JSON.
- `progression.*`: currencies, workshop tree, unlock rules, purchases.
- `skill_catalog.*`, `skill_system.*`, and `skill_effects.*`: authored active skills, deterministic cast state, and reusable effects.
- `ally_system.*`: allied soldiers, deployable buildings, combat targeting, and summon attribution.
- `ui_skill_bar.*` and `ui_targeting.*`: five-slot HUD, ultimate slot, cooldown presentation, and targeting previews.
- `build_analysis.*`: strengths, weaknesses, tags, and synergy completion.
- `balance_policy.*`: automated upgrade-choice strategies used by tools/tests.

Rendering and input must consume the same `UiElement` definitions. A button should own one rectangle, action ID, enabled state, tooltip ID, and focus neighbors; do not independently duplicate its position in drawing and click code.

Suggested new authored files:

- `run_types.json`
- `daily_challenges.json` for mission themes, loadout rules, enemy rosters, mutators, descriptions, and reward definitions
- `tower_chassis.json`
- `modules.json`
- `workshop.json`
- `currencies.json`
- `synergies.json`
- `skills.json` and `skill_trees.json`
- `statuses.json`, `allies.json`, and `buildings.json`
- `ui_text.json` or localization-ready string tables

Profile schema 15 stores unlocked skills, skill-tree node ranks, active skill builds, presets, discovered codex entries, and selected loadout, with migrations preserved from earlier versions. Replays use `TA_REPLAY 3` for deterministic targeted skill casts, quantized targets, directions, cast sequences, and the expanded content fingerprint while retaining readable legacy playback paths for versions 1 and 2.

## 10. Implementation phases

### Phase 0 — Specification and metrics

- Define the initial tower chassis, module slots, currencies, and Standard/Endless rules.
- Assign every existing weapon and upgrade its roles, tags, strengths, weaknesses, and synergy links.
- Classify existing effects as passive upgrades, active skills, or ultimates; specify the first five-skill vertical slice and target modes.
- Define baseline balance metrics and fresh/mid-progression profiles.

Exit criteria: every existing choice has an authored identity and at least one intended use case.

### Phase 1 — UI foundation and application states

- Extract shared UI elements, hit testing, focus, and navigation from `main.cpp`.
- Add Main Menu, Run Type, Loadout, Modifier, Workshop placeholder, Collection placeholder, Settings, and Results transitions.
- Reserve the five-skill plus ultimate loadout and bottom-HUD geometry even if the first iteration uses placeholders.
- Preserve keyboard, mouse, touch, and controller behavior.

Current status: the main application states and a shared `ui_layout.hpp` rectangle layer are already present. Treat this phase as a baseline-hardening phase: add regression coverage when new skill controls are introduced, but do not restart the frontend extraction.

Exit criteria: all screens are reachable and return correctly; visual and click rectangles come from the same data; navigation has automated tests.

### Phase 2 — Description system

- Extend content schemas with short/long descriptions, mechanics, strengths, weaknesses, and synergy metadata.
- Add desktop hover tooltips, touch details behavior, and focus-driven controller details.
- Add always-visible upgrade guidance and the first Collection/Codex version.

Current status: descriptions, loadout hover details, upgrade-card guidance, Daily briefings, and a Collection screen exist for the current content. The remaining work is to move skill definitions, resolved talent values, cooldown text, target rules, and icon mutations through the same authored description path.

Exit criteria: no selectable gameplay item lacks an explanation; content validation fails when required text or references are missing.

### Phase 3 — Workshop and economy

- Add persistent Core Parts, Legend Cores, and optional Blueprints while retaining cosmetic Shards.
- Add first-clear Daily Challenge reward tracking and prevent duplicate Legend Core claims for the same challenge ID.
- Replace the simple Daily date hash with validated mission recipes, briefing data, loadout constraints, equipment loans, and rotation rules.
- Implement workshop nodes, prerequisites, purchases, save migration, and results rewards.
- Implement the first Legendary Evolution for each ultimate, then add the remaining branches after their tradeoffs pass balance tests.
- Add fresh/mid/max progression fixtures for tests.

Current status: current currencies, Daily first-clear rewards, tower/support Workshop purchases, skill unlocks, skill-tree purchases, talent builds, three presets, ultimate evolutions/modules, and profile migration are implemented through schema 15.

Exit criteria: purchases persist, cannot overspend, migrate safely, and have bounded combat impact.

### Phase 4 — Skill foundation and deterministic targeting

- Add typed skill/tree definitions, content validation, five-slot loadouts, runtime cooldowns/charges, and composable effect operations.
- Implement normalized `SkillCastRequest` targeting, confirm/cancel behavior, target previews, and replay serialization.
- Migrate Black Hole to Gravity Well and Teleport Trap to Phase Mine; remove them from the passive draft after parity tests.
- Route ultimates through the shared manual cast contract and remove auto-ultimate from new profiles and UI.
- Add the bottom skill bar with cooldown/charge states and composable talent icon layers.

Exit criteria: the first two migrated skills and three additional skills can be equipped together, cast manually, replay exactly, and run headlessly without SDL or wall-clock dependencies.

### Phase 5 — Summons and skill workshop trees

- Add allied units, buildings, deterministic targeting/combat, entity caps, healing/buff tags, and contribution telemetry.
- Implement Vanguard Drop and Sentry Fabricator first, then Forward Barracks and Rally Beacon.
- Add node-tree graph validation, branch exclusivity, purchases, build presets/respecs, resolved stats, and presentation mutations.
- Integrate skill trees into Workshop, Collection, loadout summaries, Daily loans/restrictions, and results.

Exit criteria: every initial skill has a legal generic-to-specialized tree; summon-heavy runs remain bounded and deterministic; branch choices visibly change behavior and HUD presentation.

### Phase 6 — Build and synergy redesign

- Add upgrade roles, prerequisites, exclusions, levels, reactions, and threat previews.
- Give each weapon at least two viable build paths and one explicit weakness.
- Add run-end build analysis and failure guidance.

Exit criteria: coherent builds measurably outperform unrelated picks; at least two strategies per weapon can clear Standard across representative seeds.

### Phase 7 — Balance automation

- Implement the six upgrade decision policies, skill-casting policies, and richer telemetry.
- Run policy × weapon × arena × profile tier × modifier matrices.
- Establish target bands and regression thresholds from collected distributions.

Exit criteria: random choices do not routinely win; synergy and threat-aware policies outperform random; no mandatory single upgrade exists across all successful runs.

### Phase 8 — Polish and playtest

- Improve menu transitions, feedback, sounds, icons, tooltip placement, and reward presentation.
- Conduct blind playtests focused on comprehension and build decisions.
- Tune content from observed choices and failure reasons.

Exit criteria: new players can explain their loadout, identify a useful upgrade, understand why they lost, and locate the workshop/currencies without instruction.

## 11. Required tests and acceptance criteria

Add automated coverage for:

- Every app-state transition and Back/Confirm behavior.
- Shared button geometry and hit detection at supported resolutions/UI scales.
- Every content item having valid descriptions, tags, icon IDs, and references.
- Tooltip safe-area placement and touch first-tap/second-tap behavior.
- Currency earning, purchase validation, overspend prevention, and profile migration.
- Daily first-clear identity tracking, duplicate-claim prevention, and missed-day fallback rewards.
- Daily briefing completeness: title, description, tower rules, skulls, mutators, enemy roster, threats, objective, and rewards.
- Daily recipe determinism, recent-theme repetition limits, equipment lending, compatibility validation, and valid required-loadout enforcement.
- Seven-day and longer rotation tests proving meaningful variation in weapons, enemy traits, skulls, arenas, and strategic roles.
- Workshop prerequisite and exclusion rules.
- Legendary Evolution equip limits, mastery requirements, tradeoffs, replay serialization, and content-fingerprint validation.
- Five-skill plus one ultimate equip invariants, duplicate rejection, missing-content migration, and Daily loan/restriction enforcement.
- Skill-tree graph validation: missing parents, cycles, unreachable nodes, rank limits, prerequisites, branch exclusions, purchases, presets, and respecs.
- Exact cooldown, charge, recharge, pause, and cooldown-modifier ordering under deterministic simulation ticks.
- Target validation for every target mode, including range edges, placement bounds, invalid/dead entity IDs, cancel-without-cost, and confirm-once behavior.
- Replay equality for casts using quantized world targets and stable entity IDs; pointer/screen coordinates must never appear in simulation replay data.
- Manual-only ultimate activation and migration of the legacy auto-ultimate profile field.
- Bottom skill-bar geometry, cooldown/charge presentation, safe-area behavior, touch hit targets, keyboard/controller focus, and render/hit-test rectangle identity.
- Talent presentation resolution: branch gateways and capstones produce valid icon layers/reticles while retaining a recognizable base icon and non-color cue.
- Deterministic ally/building target selection, combat, healing/buff tag filters, despawn rules, entity caps, and damage/healing attribution.
- Data-only extensibility test: a fixture skill composed from existing effect operations loads, validates, casts, replays, and appears in the HUD without adding a `GameSim` case.
- Upgrade prerequisites, exclusions, stack limits, and deterministic draft behavior.
- Synergy activation and exact multiplier/effect ordering.
- Fresh, mid, and max progression balance fixtures.
- Random versus tag-matched versus synergy-policy outcome distributions.
- Golden replays for each run type after the new systems stabilize.

The feature pass is ready for broader content production when:

- The game opens on a real Main Menu rather than directly on loadout.
- Start Run, Workshop, Collection, Settings, and Results form a complete loop.
- All currencies are visible and explained where they are earned or spent.
- Every choice has concise text and accessible details on mouse, touch, and controller.
- Five active skills and one separate ultimate can be equipped, understood, targeted, and activated without ambiguous clicks or hidden cooldown state.
- Every initial skill tree begins with generic nodes and ends in at least two behavior-changing specializations with visible HUD mutations.
- Summons, buildings, debuffs, healing, and deployable towers use reusable effects/tags instead of skill-specific combat branches.
- Arbitrary upgrade selection is not a reliable route to victory.
- Loadout-aware and threat-aware choices create a measurable force multiplier.
- Each weapon supports at least two successful build identities.
- Losses provide enough information for the player to make a better next decision.

## 12. Recommended next Luna task

The original vertical-slice recommendation has been completed and expanded into the full initial skill system. Future Luna work should focus on adding authored content and tuning through the existing registries, not rebuilding the simulation boundary.

1. Add new skills by composing validated operations in `skills.json`, with tree nodes and metadata in the corresponding authored packs.
2. Add operation types only when an existing typed primitive cannot express the desired behavior; cover each new operation with headless cast, cap, telemetry, and replay tests.
3. Extend Daily recipes with authored skill restrictions when a challenge needs them, then verify loadout enforcement and first-clear rewards.
4. Run the balance matrix before and after major content changes so random, coherent, and oracle policies remain separated.
5. Add visual assets and playtest feedback only after the headless contracts remain deterministic.

The implementation milestone is complete when a data-only skill can be added, validated, equipped, cast, replayed, and exposed to the HUD without adding a new `GameSim` switch branch; the existing weapon, upgrade, Daily, profile, and ultimate regression tests still pass; and all verification can run headlessly without launching the game window. This milestone is covered by the `Resonance Pulse` fixture and the full headless suite.

### Exact cast contract for the first slice

Use this lifecycle as the implementation boundary between the existing SDL client and the deterministic simulation:

```text
Input event
  -> normalized skill intent (slot / confirm / cancel / pointer or controller world point)
  -> target preview (read-only; no gameplay mutation)
  -> SkillCastRequest on confirm
  -> authoritative validation
  -> consume charge and start cooldown
  -> execute ordered effect operations
  -> emit SkillSnapshot, replay event, and telemetry
```

The request should contain `cast_sequence`, simulation `tick`, skill slot, resolved skill ID, target kind, quantized world position, optional stable target entity ID, and direction when relevant. Quantize world points at the request boundary—never store a screen pixel or recompute a target from a future cursor position. A target preview and the authoritative validator must call the same shape/range functions so a green preview cannot produce an invalid cast.

The first runtime API should expose the equivalent of:

- `beginTargeting(slot)`: returns the target mode and resolved presentation data without changing cooldowns.
- `previewTarget(slot, TargetSpec)`: returns valid/invalid, reason, shape, and affected-entity preview data.
- `tryCast(SkillCastRequest)`: validates and commits exactly once, or returns a reason without mutation.
- `advance(tickDelta)`: advances cooldowns and charges using simulation ticks only.
- `snapshot()`: read-only per-slot state for the HUD, results, and headless telemetry.

Commit ordering must be stable: validate slot/loadout and target first, consume the resource second, then apply effects in authored order and emit telemetry last. If an effect fails because a target disappeared between preview and confirm, the cast still has one deterministic result—either the whole cast is rejected before consumption or the skill definition explicitly permits an empty result. Do not let individual effect failures create partial, frame-order-dependent casts.

### Dependency gates and change boundaries

| Gate | Must be true before continuing | Keep out of that gate |
|---|---|---|
| Baseline freeze | Existing profile migrations through schema 15, replay versions 1–3, content fingerprints, Daily claims, and current headless balance cells have regression fixtures. | New skill balance tuning. |
| Content layer | Skill IDs, target modes, effect parameters, tree references, presentation IDs, and references are validated before runtime use. | SDL rendering and Workshop purchase UI. |
| Simulation layer | Cooldowns, charges, target validation, effect ordering, state hashing, and cast telemetry are deterministic without SDL. | Pointer hitboxes and icon drawing. |
| Replay layer | A cast request and resolved skill loadout round-trip in the new replay format, while legacy replays remain readable. | Replay support for unfinished summon entities. |
| Client adapter | Keyboard, controller, mouse, and touch all produce the same normalized intent; shared layout owns the skill-bar rectangles. | New talent-tree purchasing screens. |
| Progression layer | Skill unlocks, node ranks, branch conflicts, presets, and Daily loans serialize and validate against the content fingerprint. | New allied combat primitives. |
| Summon layer | Ally/building tags, caps, targeting, damage/healing attribution, and deterministic despawn behavior pass headless tests. | Large-scale content expansion. |

Keep future changes incremental: do not combine a broad `GameSim` rewrite, a new scripting language, full visual asset production, or balance-matrix retuning with one content feature. Those changes obscure whether a failure came from casting, persistence, targeting, rendering, or balance. Each new gate should remain mergeable and testable in headless mode.
