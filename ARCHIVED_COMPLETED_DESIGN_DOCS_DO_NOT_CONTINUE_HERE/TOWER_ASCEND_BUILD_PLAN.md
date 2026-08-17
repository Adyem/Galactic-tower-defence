# Tower Ascend — C++ Single-Player Build and Test Plan

This is the implementation guide for the current repository. The target is a complete offline solo game: all co-op, PvP, shared-tower, chat, lobby, matchmaking, leaderboard, and other extra-player features are explicitly excluded. The code already contains the core run loop, five weapons, seven enemy archetypes, ten waves, boss phases, fifteen upgrades, five ultimates, skull combinations, three arenas, daily rotation, cosmetics, profile/replay persistence, SDL2 UI/audio, and headless verification tools. The sections below define how to continue building it with the software available on this computer.

## 1. Product and first shippable scope

Build a single-player, mobile-first, landscape 2D tower-defense roguelite for Android, iOS, Windows, Linux, and macOS. One run consists of selecting a weapon and ultimate, defending a single winding lane, choosing one of three upgrades after milestones, applying optional score-modifying “skulls,” and defeating a final boss. The simulation must be deterministic enough for reproducible runs and replays.

The current playable build contains:

- Three arenas, all five weapon styles, fifteen upgrades, seven enemy archetypes, one two-phase boss, five ultimates, four skulls, daily challenges, cosmetics, and a complete ten-wave run.
- Touch, mouse, keyboard, procedural audio, save data, settings, score/replay recording, and adaptive 16:9–20:9 UI.
- Single-player only. No co-op, PvP, shared tower control, chat, lobbies, or other multi-player features are included in the current product scope.

Defer adaptive bosses, live-service economy, and the complete weapon roster until metrics prove the core loop fun. Excluding multiplayer keeps the project focused on combat depth, replayability, and polished cross-platform solo play.

## 2. Technology stack

Use **C++17**, **CMake 3.22+**, and **Ninja**. The current computer already provides Clang 12, GCC, CMake 3.22, Ninja, SDL2, OpenGL/OpenGL ES, Vulkan, GLFW, FreeType, and HarfBuzz.

The checked-in build currently requires only the C++ standard library, CMake/Ninja, and SDL2 (the simulation and HUD intentionally avoid extra runtime dependencies). The remaining libraries in the table are production options to add only when their corresponding subsystem is implemented.

| Need | Choice | Source |
|---|---|---|
| Window, input, controller, audio device, platform lifecycle | SDL2 | Installed; vendor a pinned version for reproducible mobile/CI builds |
| Rendering | OpenGL ES 3.0 API through SDL2 | Installed; portable to Android/iOS and adequate for stylized 2D |
| GL function loading on desktop | glad2 | Pin with CMake FetchContent or commit generated files |
| Math | GLM | Vendor/pin |
| Data files and save serialization | nlohmann/json | Vendor/pin; use JSON for authored content and versioned saves |
| Entity storage | EnTT | Vendor/pin; optional but recommended for enemies/projectiles/effects |
| UI | Dear ImGui only for developer tools; custom retained game UI for shipping | Vendor/pin |
| Text | FreeType + HarfBuzz | Installed; bundle mobile binaries and licensed fonts |
| Image decode | stb_image | Vendor single header |
| Audio decode/mixing | SDL_mixer or miniaudio | Vendor/pin; miniaudio gives the smallest cross-platform integration |
| Compression | zstd | Optional for replays/content packs; vendor when introduced |
| Tests | Current lightweight C++ test executable; Catch2 later if suites grow | `ta_sim_tests` is dependency-free; vendor/pin Catch2 only when useful |

Do not start with Vulkan: it increases renderer and mobile bring-up work without improving this game’s 2D presentation. All third-party versions and licenses belong in `third_party/manifest.json` and `THIRD_PARTY_NOTICES.md`; never depend on unpinned system packages for release builds. For the current Linux build, SDL2 2.0.20, CMake 3.22, Ninja, Clang 12, and GCC 10 are available and are sufficient for the implemented prototype.

## 3. Repository layout

```text
TowerAscend/
  CMakeLists.txt              build targets and install rules
  CMakePresets.json           reproducible GCC/Clang/release/sanitizer presets
  cmake/                      warnings, sanitizers, platform helpers
  src/
    app/                      startup, platform lifecycle, game states
    core/                     time, IDs, RNG, events, logging, jobs
    sim/                      fixed-step combat; no rendering or network calls
    gameplay/                 weapons, upgrades, effects, waves, scoring
    render/                   sprites, particles, animation, camera, batching
    ui/                       HUD, menus, touch layout, accessibility
    audio/                    music, SFX, buses, haptics
    content/                  JSON schemas, loading, validation, asset registry
    persistence/              settings, versioned save, replay, profile
  tools/                      content validator, replay inspector, balance/test runners
  assets_src/                 editable art/audio source; not shipped
  assets/                     optimized runtime atlases, fonts, audio, JSON
  tests/                      unit, simulation, replay, content validation
  docs/                       schemas, balance notes, art bible
```

Produce separate targets: `ta_client`, `ta_tests`, `ta_sim_runner` (headless balance/replay tool), and `ta_content_check`. Keep platform entry points thin and all gameplay portable.

## 4. Runtime architecture

Run combat at a fixed **30 Hz simulation tick** and render independently at the display refresh rate. Inputs become timestamped commands for the next valid tick. Use an explicitly seeded PCG RNG, stable entity ordering, integer ticks, and either fixed-point values or carefully quantized floats for gameplay-critical positions and timers. Never use wall-clock time or renderer state inside `sim/`.

```text
SDL platform/input -> command queue -> deterministic simulation -> event stream
                                          |                         |
                                    save/replay                 render/audio/UI
```

Key systems:

- **Game state stack:** Boot, Main Menu, Loadout, Run, Upgrade Choice, Results.
- **Entities/components:** Transform, PathProgress, Health, Resistances, StatusSet, Targeting, Projectile, Lifetime, VisualRef. A spatial grid limits target queries.
- **Content-driven abilities:** Weapons, ultimates, upgrade weights, and upgrade magnitudes are JSON definitions referring to small, tested C++ effect primitives such as Damage, Splash, Pierce, Chain, Slow, Stun, Burn, Pull, and Teleport. Compose synergies through tags and reactions (`burning + wind -> fire_tornado`) rather than bespoke subclass trees.
- **Combat pipeline:** acquire target → fire/cast → resolve hit → apply damage/status → detect reactions → emit presentation events → death/reward. Cap chains, spawned effects, and status stacks to prevent runaway combinations.
- **Upgrade draft:** seeded weighted pools, prerequisites, exclusions, rarity, and duplicate rules. Store IDs, never display strings, in save/replay data.
- **Waves:** timeline-authored spawn groups, budgets, spawn cadence, and per-wave enemy-mix weights. Daily challenges use a deterministic UTC-date seed, explicit bonus-shard reward, and local content, so they work offline and can be regression-tested for arbitrary dates.
- **Scoring:** base score × difficulty/skull multipliers, with separately recorded time, leaks, damage, and seed. Skull gameplay modifiers and rewards are authored in content. Scores stay local; replays are verified locally.
- **Persistence:** atomic write to a temporary file then rename; schema version and migration functions. Replays carry the authored content fingerprint and are rejected when installed balance data differs, preventing unverifiable scores.

## 5. Art, audio, and asset requirements

Adopt a **clean, vibrant 2D sci-fi style**: dark navy battlefield, bright elemental accents, chunky readable silhouettes, restrained bloom, and effects whose shapes as well as colors communicate meaning. Use a slightly tilted top-down view with a fixed logical canvas of **1920×1080**, safe-area anchors, scalable UI, and a 32 px world grid. Design for legibility on a 6-inch screen; avoid critical details below 3 physical pixels.

Required vertical-slice assets:

| Category | Deliverables |
|---|---|
| Tower | Base, two weapon modules, muzzle/rotation parts, damage states, selection ring, 2–4 frame idle cues |
| Enemies | Six distinct silhouettes with move/hit/death states; boss with telegraphed attacks and phases |
| Environment | One seamless ground set, path pieces, entry/exit, blockers, background props, decals |
| Combat VFX | Muzzle flashes, tracers, explosions, burn, freeze, stun, chain lightning, teleport, black-hole placeholder, Meteor Rain, impact/death particles |
| UI | HUD panels, buttons and states, weapon/upgrade/skull/ultimate icons, resource and status icons, cooldown masks, pause/settings controls, nine-slice frames |
| Fonts | One licensed UI family with regular/bold and needed language glyphs; optional display face |
| Audio | Two music loops, UI set, tower shots/impacts, elemental/status cues, enemy/boss cues, ultimate stingers, win/loss, ambience |

Create in layered SVG/PSD/Krita sources, export lossless PNG, and pack sprites into 2048×2048 atlases with 4 px extrusion. The current client uses dependency-free vector placeholders with distinct archetype silhouettes and status rings, so gameplay is readable before final art arrives. Author icons as SVG when possible, but rasterize into atlases for runtime. Ship texture tiers (0.5×/1×/2×), OGG Vorbis music, and short mono/stereo OGG/WAV effects as appropriate. Keep sound groups with 3–5 variations and pitch jitter. Every dangerous enemy action needs animation, VFX, and an audio cue at least 300–500 ms before impact.

Maintain `assets/manifest.json` containing stable ID, type, source, runtime path, dimensions, pivot, atlas, tags, license, and content hash. The build must fail on missing IDs, invalid upgrade references, atlas overflow, or unlicensed third-party material. Initial production budget: roughly 120–160 icons, 60–90 VFX sprite sequences, 8–12 enemy/boss animation sets, 70–100 SFX, 8 UI screens, and 2 music tracks for a polished vertical slice. Placeholders may be simple geometric sprites generated in-repo.

## 6. UI and platform rules

- Landscape touch is primary: large 48 dp minimum targets, one-thumb ultimate, pause-safe upgrade cards, no precision dragging during combat.
- Mouse/keyboard/controller expose the same actions through an input-action map. Show prompts for the active device.
- Respect notches and system gestures; test 16:9, 18:9, 19.5:9, 20:9, tablet, and resizable desktop windows.
- Support UI scale, color-blind palettes, reduced flashes/shake, separate volume buses, subtitles/captions for gameplay cues, vibration toggle, and remapping on desktop.
- Pool enemies, projectiles, particles, and audio voices. Batch sprites by atlas/material. Target 60 fps rendering and 30 Hz simulation on a mid-tier Android device; define budgets of <16.6 ms frame time, <500 MB peak RAM, and fast resume after suspension.

## 7. Build and delivery

Use CMake presets for `linux-debug`, `linux-asan`, `linux-release`, `android-arm64`, and later Apple/Windows. Enable strict warnings, Address/UndefinedBehavior sanitizers in developer builds, LTO in release, and deterministic content packaging. The current CMake project installs the client, replay/balance/content tools, manifests, authored content, and build documentation; `TA_CONTENT_DIR` selects an external content pack at runtime. Android uses the NDK and SDL Android project; iOS uses SDL’s UIKit backend and an Xcode generator/toolchain file. Signing credentials and store configuration stay outside the repository.

CI should perform formatting/lint checks, compile with Clang and GCC, run unit/content/replay tests, run headless simulations for thousands of seeds, build release packages, and publish crash symbols. Add telemetry only with consent and data minimization: run start/end, choices, deaths, performance class, disconnects, and anonymized error context.

Test priorities are effect primitives, synergy ordering, save migrations, seeded upgrade drafts, score rules, offline fallback, and golden replays whose checksums must match at selected ticks. Also fuzz JSON/content loaders and test corrupt saves, interrupted writes, invalid challenge signatures, and content-version mismatches.

## 8. Tester and verification plan

Testing is a first-class build target, not a manual afterthought. Keep simulation free of SDL so it can run in CI and on a developer laptop.

| Test layer | Target | What it proves |
|---|---|---|
| Unit/integration simulation | `ta_sim_tests` | Seed determinism, wave progression, every weapon/arena, enemy archetypes, upgrades, synergies, ultimates, skulls, daily selection, profile migration, cosmetic neutrality, and replay hashes |
| Content validation | `ta_content_check` | Required IDs, duplicate-ID/path/license checks, JSON delimiter balance, numeric manifests, actual runtime loading, and a negative missing-pack smoke test |
| Replay verifier | `ta_replay_check` | A saved command stream reproduces its terminal state outside the client; `--self-test` and the headless record→verify CTest cover the persistence path |
| Balance matrix | `ta_balance_check` | Every weapon × arena combination and all 16 skull masks reach a terminal state across deterministic seeds and report victory/failure rate and score range |
| Runtime smoke | `tower_ascend --headless` | The complete run reaches a terminal state without a window or audio device |
| Render smoke | `tower_ascend --render-smoke` | SDL video/audio initialization, software fallback, draw path, and clean shutdown under dummy drivers |
| Sanitizers | `build-asan` + CTest | Address/UndefinedBehavior errors in every automated target |

Required commands after each gameplay or content change:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
./build/tower_ascend --headless
./build/ta_replay_check --self-test
```

Add a new deterministic regression test whenever a balance rule, content field, upgrade reaction, or persistence format changes. Run matrix tests across all weapon × arena combinations and fuzz malformed content/save/replay input before release.

## 9. Implementation sequence and exit criteria

1. **Foundation (1–2 weeks):** CMake targets, SDL window/input/audio, ES3 sprite batch, asset loader, test runner. Exit: textured scene runs on Linux and one Android device.
2. **Combat prototype (2–3 weeks):** fixed-step simulation, pathing, targeting, damage, pooling, basic HUD, one tower and three enemies. Exit: deterministic replay produces identical checksums.
3. **Vertical slice (4–6 weeks):** two weapons, upgrade draft, statuses/synergies, waves, boss, ultimate, skulls, scoring, save/settings, final art/audio pass. Exit: complete 15-minute run at target performance with no placeholder-critical UI.
4. **Content and daily mode (2–4 weeks):** second-pass balance, deterministic challenge rotation, local profile/replay validation, accessibility/localization, and crash reporting. Exit: seven days of internally verified daily challenges.
5. **Production expansion:** additional weapons, ultimates, maps, modifiers, art/audio passes, accessibility/localization, device certification, and cosmetic economy. Do not add extra-player systems to this scope.

Before full production, validate three questions with playtests: whether upgrade choices visibly change play, whether skull risk makes scoring interesting, and whether the tower requires enough active decisions to sustain a full solo run. If any answer is no, revise the mechanic before creating the large asset set.

## 10. Immediate next deliverable

Create the foundation plus a gray-box combat room using geometric placeholder sprites. Implement one lane, one tower, one enemy, targeting, projectiles, damage, and a deterministic replay checksum. This is the smallest slice that proves the architecture, renderer, input model, and reproducible simulation without committing to costly content.
