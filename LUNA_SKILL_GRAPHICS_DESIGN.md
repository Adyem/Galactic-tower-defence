# Luna 5.6 brief: readable skill graphics

## Goal

Give every active skill an immediately recognizable cast, persistent effect, and result cue while preserving Tower Ascend's existing visual language: dark surfaces, neon outlines, simple geometric silhouettes, limited particles, and color reinforced by shape. The player should be able to answer three questions without reading text:

1. Which skill was cast?
2. Where is it active and for how long?
3. What did it do to enemies or allies?

Do not add sprites, SDL_ttf, post-processing, or simulation-side animation logic for this pass. Build the effects from the existing `ring`, `line`, `circle`, `hexagon`, `filledDiamond`, `filledHexagon`, `segmentedBar`, and color helpers in `src/main.cpp`.

## Shared visual rules

- Use shape as the primary identity and color as reinforcement. Effects must remain distinguishable in the monochrome and color-blind palettes.
- Reserve violet + concentric circles for gravity/void, amber + hexagons for traps, mint + chevrons/diamonds for soldiers, cyan + mechanical shapes for drones/turrets, ice blue + radial spokes for cold, magenta + broken hexagons for debuffs, and mint/white + crosses for healing.
- Every cast gets a short 6-12 tick arrival cue, every duration effect has a stable boundary, and every result gets a 4-10 tick confirmation cue. Keep no more than roughly 24 small lines/points per effect per frame.
- Animate from simulation ticks, not wall-clock time. Reduced-flash mode should remove alternating bright frames and halve pulse frequency, not hide gameplay information.
- A talent branch adds one small outer glyph or changes the interior pattern. It must not replace the base silhouette, so the skill remains recognizable.
- Target previews should use the skill's real authored radius instead of the current generic 48/86 pixel rings. Invalid placement remains red; valid placement uses the skill color and base silhouette.

## Small presentation interface to add first

Several skills apply their result immediately and leave no object for the renderer to inspect. Add a bounded presentation-only event stream rather than inferring effects in `drawWorld`.

Suggested data:

```cpp
enum class SkillVisualPhase { Cast, Trigger, Hit, Expire, Spawn };

struct SkillVisualEvent {
    std::uint32_t id;
    SkillId skill;
    SkillVisualPhase phase;
    Vec2 position;
    float radius;
    int remainingTicks;
    std::string branchId;
};
```

Store at most 64 events in `GameSim`, decrement them at the fixed 30 Hz tick, and expose a const accessor. Include them in the state hash only if they are stored in `GameSim`; alternatively keep an event sequence counter and presentation cache in the client. Never let a visual event alter targeting, damage, movement, RNG consumption, or replay commands.

Add `drawSkillCastEffects`, `drawSkillZone`, `drawSkillBuilding`, and `drawSkillAlly` helpers. Dispatch by `SkillId` and role, with branch decoration supplied by `branchId`. Keep the HUD glyph and world silhouette built from the same small base-glyph helper so they cannot drift apart.

## Skills that need changes

### 1. Gravity Well — minor improvement

Current state: recognizable violet zone with an inner ring, but it does not clearly show inward pull or damage ticks.

- Cast: contract three broken outer arcs toward the center over 8 ticks.
- Active: retain the outer violet boundary; draw four short inward-pointing radial lines and a small dark core with a white rim.
- Result: when an enemy is pulled, show a short violet tether from its previous direction toward the core. Do not draw a tether every tick for every enemy; cap it to the nearest six affected enemies.
- `event_horizon`: add a second thin outer orbit.
- `singularity`: add a white target diamond around the focused enemy.

### 2. Phase Mine — required

Current state: only an amber circular zone; armed state and rewind trigger are unclear.

- Placement/cast: show the existing filled hexagon glyph landing at the target, then three amber arming segments fill during `armTicks`.
- Armed: use a small central hexagon, six short outward ticks, and a faint radius ring. Pulse once every 20 ticks.
- Trigger: draw a bright amber hexagonal flash and two copies of the victim silhouette connected by a backward dashed line. The rear copy fades after 6 ticks. This communicates displacement without text.
- `rewind_array`: display two or three linked mini-hexagons.
- `phase_snare`: add an ice-blue crosshair around the triggered enemy for the slow/weakness result.

### 3. Vanguard Drop — required

Current state: soldiers appear as generic diamonds with no arrival cue; base soldiers, bulwarks, and strikers are easy to confuse.

- Cast: draw a mint vertical drop line and expanding landing diamond for 8 ticks, then spawn units around it in a deliberate wedge.
- Soldier: mint filled diamond plus one short forward barrel line.
- Bulwark: wider diamond with a white front shield arc.
- Striker: amber narrow diamond with a small crosshair pip.
- Attack: draw a 3-tick line from soldier to target. Use mint for soldiers, amber for strikers, and white impact ticks for bulwarks.
- Keep the existing health/buff feedback, but add a tiny health bar only after a unit takes damage to limit clutter.

### 4. Forward Barracks — required

Current state: barracks and sentries share almost the same generic hexagonal building.

- Cast: build the silhouette in three steps—base plate, walls, then roof antenna—over 10 ticks.
- Active barracks: chamfered rectangular body on a hexagonal base, mint doorway, and two roof antenna lines. Preserve its health bar.
- Soldier spawn: open the doorway for 4 ticks and draw two outward chevrons behind the new unit.
- `recruitment`: add two small mint unit pips beside the doorway.
- `field_armory`: use an amber roof stripe and a horizontal weapon-bar glyph.
- Expire/destroy: collapse the roof and walls inward with four short lines; do not use a full-screen flash.

### 5. Ruin Hex — highest priority

Current state: the gameplay debuff is applied instantly, but there is no world-space cast graphic or clear vulnerability marker.

- Cast: draw a magenta broken hexagon at the authored radius for 10 ticks, with six lines converging on affected enemies.
- Affected enemy: retain a small broken shield/hexagon above or around the enemy for `vulnerabilityTicks`. This is more important than keeping the cast circle visible.
- Damage amplification feedback: on an amplified hit, briefly split the shield glyph into two halves.
- `brittle`: use a visibly cracked white/magenta shield.
- `withering`: use three downward magenta chevrons and a slow ring; do not recolor the whole enemy because cold already owns that signal.

### 6. Rally Beacon — highest priority

Current state: healing and buffing happen immediately; only the existing generic violet ring on buffed allies remains.

- Cast: mint/white expanding ring with a central plus sign and four outward rays for 8 ticks.
- Heal result: draw a short upward health segment beside each healed ally and one small rising plus. Cap simultaneous plus signs to eight.
- Buff result: replace the generic violet ring with four outward chevrons around the ally; this communicates increased speed/damage rather than protection.
- `combat_medics`: retain the mint plus and add a slow rotating white outer cross.
- `war_cry`: switch the branch decoration to amber forward chevrons and a three-line sound wave.

### 7. Sentry Fabricator — required

Current state: the sentry uses the same filled hexagon as other buildings and its attacks have no clear origin.

- Cast: cyan wireframe base closes into a turret over 8 ticks.
- Base sentry: cyan hexagonal base, dark center, short barrel pointing toward its current target, and two rear support legs.
- Attack: 3-tick cyan tracer from barrel to target plus a tiny target impact cross.
- `gatling`: twin barrels and a segmented spin ring that fills while repeatedly attacking.
- `mortar`: one upward angled barrel; show a slow amber shell diamond and an outlined impact circle. Reuse the existing world projectile style where possible.
- Keep the health bar and make the lifetime visible as a thin cyan outer arc only when below 25% remaining.

### 8. Cryo Field — moderate improvement

Current state: the ice color communicates cold, but the field is a generic ring and stun/freeze is not distinct from ordinary slow.

- Cast: six ice-blue radial spokes grow outward from the center.
- Active: outer ice ring plus sparse triangular shard ticks pointing inward. Avoid a filled translucent area.
- Slowed enemy: retain the current ice body color and add one small horizontal frost line.
- Stunned/frozen enemy: add a complete six-sided ice outline, visually distinct from slow.
- `permafrost`: add a second broken outer ring.
- `shatter`: on the relevant burst, send six short shard lines outward and briefly crack the enemy's ice outline.

### 9. Drone Swarm — required

Current state: drones are rendered as ordinary allied diamonds, so the swarm and its role are not obvious.

- Cast: cyan carrier ring opens and releases drone pips in alternating directions.
- Drone body: tiny cyan circle/hexagon with two side-wing lines; keep it smaller and rounder than soldiers.
- Movement: add a one-segment trailing line only while moving quickly.
- Attack: short cyan beam or tracer to the selected target.
- `hunter`: amber center pip plus a target diamond on the chosen high-health enemy.
- `disruptor`: violet center pip and a two-ring signal ripple when slow is applied.

### 10. Resonance Pulse — highest priority

Current state: it is rendered as another generic violet zone, so it reads like Gravity Well even though it is a weakness/control pulse.

- Cast: three cyan-violet rings expand once in sequence over 12 ticks; each ring should be broken into four arcs to distinguish it from Gravity Well.
- Active: keep one thin broken boundary and a central waveform made from four connected line segments.
- Hit result: affected enemies receive a tiny oscillating two-line mark plus the same vulnerability marker family used by Ruin Hex.
- `amplifier`: add an extra bright outward pulse and a plus notch on the HUD/world glyph.
- `dampener`: use slower, wider-spaced rings and downward notches; never reuse Cryo's snowflake/spokes.

## Implementation order

1. Add the presentation event structure, shared skill color/glyph lookup, authored-radius target preview, and reduced-flash behavior.
2. Implement Ruin Hex, Rally Beacon, and Resonance Pulse because their results are currently invisible or ambiguous.
3. Differentiate Vanguard Drop, Drone Swarm, Forward Barracks, and Sentry Fabricator by role and attack/spawn feedback.
4. Improve Phase Mine trigger readability.
5. Polish Gravity Well and Cryo Field, which are already broadly recognizable.
6. Add branch overlays only after every base skill passes readability checks.

## Acceptance checks

- At 1280x720, all five equipped skills can be cast together without hiding enemies or the lane.
- A tester viewing a muted recording can identify at least 8 of 10 skills from the world effect alone.
- Base, specialist branch, cooldown, valid target, active duration, and expire/trigger states remain distinct in all three color palettes and reduced-flash mode.
- No visual helper changes simulation state, consumes RNG, or changes replay/state-hash results.
- Add headless tests for event creation, phase, duration, radius, branch ID, capacity, and expiry. Keep screenshot/render-smoke checks under SDL's dummy video driver.
- Use `--text-audit` during dummy render smoke if any labels are added; this pass should prefer shape-only world feedback.

