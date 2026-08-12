# Tower Ascend (single-player C++ prototype)

This repository contains the first playable implementation of the Tower Ascend design: a deterministic, offline, single-player tower-defense roguelite built with C++17 and SDL2. Multiplayer/PvP/co-op features are intentionally out of scope.

## Build and test

Requirements already available on the development computer:

- Clang or GCC
- CMake 3.22+
- Ninja
- SDL2 2.0+

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/ta_sim_tests
./build/ta_content_check assets/content
./build/tower_ascend --headless
./build/tower_ascend --render-smoke
./build/ta_replay_check --self-test
./build/ta_balance_check 4
./build/tower_ascend --headless --record-replay /tmp/tower_ascend.replay
./build/ta_replay_check /tmp/tower_ascend.replay 100000
cmake --install build --prefix /tmp/tower-ascend-install
TA_CONTENT_DIR=/tmp/tower-ascend-install/share/tower_ascend/content /tmp/tower-ascend-install/bin/tower_ascend --headless
```

Equivalent reproducible presets are available through `CMakePresets.json`: `cmake --preset linux-debug`, `cmake --build --preset linux-debug`, `cmake --preset linux-clang`, and `cmake --preset linux-asan`.

For a sanitizer pass:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

## Play

Run `./build/tower_ascend`. The loadout screen is intentionally icon-based so it has no runtime font dependency:

- `1`–`5`: choose Rapid Fire, Cannon, Arcane Beam, Frost, or Railgun.
- `Q`–`T`: toggle skull modifiers (Swarm, Glass Cannon, Haste, or Greed); combinations multiply score risk/reward.
- `Y`/`U`/`I`/`O`/`P`: choose Meteor Rain, Bullet Storm, Absolute Zero, Gravity Shift, or Energy Surge before starting.
- `A`: toggle automatic ultimate mode before starting; it fires when the battlefield is crowded or the boss enters phase two.
- `6`–`0`: select an unlocked cosmetic tower skin; `K` unlocks the previewed skin when you have 100 cosmetic shards.
- `F1`–`F3`: choose Moonbase, Ember Crater, or Neon Ruins before starting.
- `D`: start the deterministic daily challenge for the current UTC date.
- `Enter`/`Space`: start the run.
- During a run, `1`–`3` select upgrade cards when a wave ends, and `U`/`Space` activates the ultimate.
- Mouse/touch: tap weapon/skull/start cards, upgrade cards, and the ultimate area of the HUD.
- On touch loadout, tap an arena card to select it and tap a locked skin to spend shards and equip it.
- `P`: pause; `F`: reduced-flash/audio accessibility mode; `C`: high-contrast mode.
- `V`: toggle gameplay captions; `B`: toggle vibration preference; `G`: cycle color-blind palettes; `-`/`=`: adjust UI scale.
- `[`/`]`: lower/raise master volume; `,`/`.` adjust SFX volume; `;`/`'` adjust UI volume. Accessibility and volume preferences are saved in the profile.
- `F10`: open the touch-safe settings panel. `F6`/`F7`/`F8`/`F9` then the next key remaps Ultimate/Pause/Confirm/Restart; remaps persist in profile version 6. SDL game controllers map A/B/X/Y to the same Confirm/Pause/Ultimate/Restart actions and expose the active-device prompt.
- `R`: restart with the selected loadout; `Esc`: quit.

The window title reports weapon, wave, score, and state. The renderer exposes the specified 1920×1080 logical design canvas while the deterministic simulation uses 1280×720 world units, and falls back to SDL’s software renderer when accelerated rendering is unavailable, which is useful for CI and remote machines.

Runtime content lookup honors `TA_CONTENT_DIR`, then the compiled source tree, then `share/tower_ascend/content` relative paths. CMake install rules package the executable, tools, manifests, authored content, and build documentation without introducing networking or multiplayer dependencies.

## What is implemented

- Fixed 30 Hz deterministic combat simulation with seeded RNG and state hashing.
- Five weapons with materially different firing behavior.
- Ten escalating waves, a final boss, lives, leaks, currency, score, and victory/failure states.
- Three deterministic arenas with different lane shapes and movement/health balance profiles.
- Seven enemy archetypes: grunt, runner, tank, shielded, swarmling, teleporter, and a two-phase boss.
- Upgrade drafts with fifteen upgrades, status effects, splash damage, piercing, ricochet, burning, freezing, poison, teleport traps, wind/fire tornado reactions, and black-hole chaining.
- Upgrade drafts use authored weighted rarity values from `assets/content/upgrades.json`, while remaining deterministic for a given seed.
- Upgrade effect magnitudes (pierce counts, splash/stun radii, status durations, reaction scales, displacement, and utility rewards) are also authored in `assets/content/upgrades.json`.
- Upgrade drafts also bias toward the selected weapon’s branch, so kinetic, explosive, arcane, and frost builds produce different choices without locking out experimentation.
- Four skull modifiers and weapon-independent ultimate behavior.
- Skull spawn pressure, starting lives, enemy speed, and greed rewards are authored in `assets/content/skulls.json`.
- Five selectable, weapon-independent ultimates with distinct area, control, movement, and fire-rate effects.
- A two-phase boss with an authored 500 ms telegraphed attack, deterministic life damage, warning VFX, HUD caption, and audio cue.
- Offline daily challenge rotation with a date-derived seed, recommended weapon, skull modifier, and arena.
- Loadout preview shows the daily weapon, arena, skull, and bonus shard reward before starting; dated challenge generation is exposed and covered across a seven-day regression window.
- Explicit build synergies: Fire + Wind creates a tornado-like secondary burn, Ice + Electricity amplifies chain damage on slowed targets, and Poison + Teleport applies displacement bonus damage.
- SDL2 window/input/rendering, a fixed 16:9 design viewport that scales cleanly from a 1920×1080 desktop canvas to mobile safe areas, pooled-style vector collections, and a no-font dependency visual HUD.
- Dependency-free procedural combat art: each enemy archetype has a distinct silhouette, shield/teleport/boss rings, status-effect rings, weapon-colored projectile trails, and an Arcane Beam presentation; these are replaceable by atlas sprites later.
- Upgrade cards show concise effect descriptions in-run, making each draft a readable tactical decision.
- Terminal results panel with reusable `RunSummary` data for score, wave, kills, leaks, arena, victory, and duration.
- Procedural audio cues through SDL audio (shots, wave transitions, upgrade prompts, victory/failure), with reduced-flash volume scaling.
- Pause handling, touch/mouse hit targets, high-contrast grid/HUD treatment, and persisted reduced-flash preference.
- Headless tests for deterministic replay behavior, wave/upgrade progression, skull pressure, ultimate cooldowns, and full-run victory.
- Versioned profile and replay persistence with atomic writes (`tower_ascend.profile` and `tower_ascend.last.replay` are created in SDL's per-user application-data directory after a completed run), including UI scale, color palette, captions, vibration, volume, and reduced-flash preferences.
- Standalone replay verifier: `ta_replay_check <file> [ticks]` validates and re-simulates saved command streams outside the game client.
- Headless mode can record a complete client-style replay with `--record-replay`; CTest records and independently verifies one end-to-end run.
- Client-generated replays include an authored-content fingerprint; the verifier rejects replay files made with different balance/content data instead of silently producing a misleading hash.
- Cosmetic-only progression: runs award shards, five tower skins can be unlocked/equipped, and no skin changes combat statistics.
- Loadout shows saved cosmetic shards and best score before each run.
- Authored content manifests in `assets/content/` for weapons, upgrades, skulls, and waves, checked by `ta_content_check` in CI.
- Ultimate cooldowns and damage scaling are authored and validated in `assets/content/ultimates.json` rather than hardcoded in combat.
- Runtime bitmap labels make weapon, skull, ultimate, HUD, and upgrade choices readable without requiring SDL_ttf or a system font.
- Authored enemy and asset manifests with a build-time license/ID validation pass.
- Enemy health, speed, radius, resistance, and teleporter timing are data-driven in `assets/content/enemies.json`; loader tests reject incomplete or unsafe archetype records.
- Wave budgets, spawn cadence, and per-wave archetype mix weights are authored in `assets/content/waves.json`, so daily/balance variants can change enemy composition without recompiling the simulation.
- Arena path rendering reads the same authored path amplitude/frequency values as simulation, keeping visual lanes synchronized with collision movement.
- The tester suite includes deterministic simulation, manifest/path/license validation, full runtime content loading, replay verification, headless runtime smoke, dummy-driver render smoke, and an ASAN/UBSAN CTest configuration. See [`TOWER_ASCEND_BUILD_PLAN.md`](TOWER_ASCEND_BUILD_PLAN.md) for the complete architecture, asset, library, and verification plan.
- `ta_balance_check` runs the 15-cell weapon × arena matrix plus all 16 skull combinations over configurable seeds and fails if any run cannot reach victory or game-over.

The next production steps are expanding the content manifests, adding production art/audio assets, accessibility/localization, and platform packaging. The pure simulation in `src/game.*` is deliberately isolated so those additions do not require rewriting combat.
