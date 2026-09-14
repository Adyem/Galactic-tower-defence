# Tower Ascend Interface Overhaul Plan

**Status:** Active plan. Visual screen migration is not complete.

**Progress checkpoint:** The shared-contract migration is underway without changing the visual interface. `UiScene`/`UiNode` now provide stable node IDs, parent and viewport containment checks, semantic action IDs, sibling-overlap diagnostics, and deterministic topmost interactive hit testing. `UiTextNode` adds measured, wrapped-text overflow checks and text/control collision diagnostics. Main-menu, run-selection, loadout, workshop, collection, settings, modifier-selection, and their key modal mouse actions now consume the same semantic control contract, with deduplicated runtime layout warnings. The standalone `ta_ui_layout_audit` tool checks representative screen scenes and dynamic text metrics without SDL. The existing renderer has not yet been fully migrated to this model.

**Implementation boundary:** The current work is deliberately an audit/migration foundation, not the completed overhaul. Controls have begun using the shared hit-test scene, but most drawing still supplies literal rectangles and the renderer does not yet register every drawn text item as a `UiTextNode`. Therefore a passing audit currently proves the covered contracts and samples only; it does not yet prove every visible screen state is collision-free. The implementation phases below are the work required to close that gap.

## 1. Objective

Rebuild the interface around a readable information hierarchy and one authoritative layout system. The player should always be able to answer four questions quickly:

1. What screen or phase am I in?
2. What can I do right now?
3. What will happen if I confirm this action?
4. Why did the game reject or change an action?

The overhaul must prevent text and controls from escaping their panels, prevent accidental clicks caused by overlapping hitboxes, and remain usable at the existing 1280×720 logical resolution, larger UI scales, keyboard/controller input, and the headless validation workflow.

## 2. Current-state findings

The current renderer inventory confirms that this is a structural migration rather than a small spacing pass: `src/main.cpp` currently contains roughly 105 direct `drawText` calls, 158 `drawTextFitInBox` calls, 16 wrapped-text calls, and 17 remaining direct hit-test sites. These counts are a baseline for the migration. They should trend toward one scene-building path and a small number of renderer primitives; any remaining direct call must either be registered in the scene or be explicitly marked as world/decorative output.

The renderer already has valuable foundations: a fixed logical viewport, shared `UiRect` constants, half-open hit testing, `drawTextFitInBox`, wrapped text helpers, a runtime `TextLayoutAudit`, accessibility palettes, contextual HUD strips, and UI-layout tests. The skill browser is already separated from the fixed five-slot strip and has search, filtering, scrolling, and a details panel.

The main structural problems are:

- Screen render functions still contain many literal coordinates and compose unrelated regions independently. A change near the top of a screen can silently collide with a later panel.
- A rectangle can be technically inside the viewport while its text is too dense to read. One-line truncation hides important rules, especially costs, restrictions, modifiers, and daily challenge details.
- The audit is primarily draw-time and text-focused. It does not yet validate the complete scene graph, z-order, semantic ownership, minimum spacing, tooltip placement, or whether every interactive rectangle has exactly one action.
- UI state is spread across global variables and event branches. Rendering and input handling can therefore drift apart even when both use the same nominal constants.
- The current pixel font has no general-purpose glyph fallback and limited sizing options. Dense all-caps status strings are efficient but become difficult to scan when several systems report at once.
- Contextual information can compete with permanent information. The game has many class-specific meters, target previews, daily rules, and upgrade descriptions, so showing all of them simultaneously is not sustainable.

## 3. Information architecture

Introduce a consistent shell for every screen:

```text
┌─────────────────────────────────────────────────────────────┐
│ Screen title                 navigation / currency summary  │  Header
├─────────────────────────────────────────────────────────────┤
│                                                             │
│                     primary work area                      │  Content
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ contextual explanation / validation / next action           │  Guidance
├─────────────────────────────────────────────────────────────┤
│ back / cancel                          confirm / continue   │  Footer
└─────────────────────────────────────────────────────────────┘
```

Permanent information belongs in the header or a stable side panel. Details that apply only to the focused object belong in the contextual panel. Every screen gets one primary action, one predictable back/cancel action, and a short help line. Secondary actions must be visually and semantically subordinate.

Gameplay should use a separate shell:

- top: tower health, wave, score, pause, and one compact objective/status line;
- center: unobstructed arena and target/placement preview;
- bottom-left: only the active contextual explanation or resource meter;
- bottom-center: five skills with icon, cooldown, charges, resource cost, and target mode;
- bottom-right: ultimate with a clearly longer cooldown treatment;
- transient events: anchored callouts with a queue and severity priority, never arbitrary text over the arena.

Only the highest-priority contextual class panel is visible at once. Other class state is available through the focused skill, details overlay, or a compact expandable summary.

## 4. Authoritative UI model

Create a UI scene/layout layer between game state and SDL drawing. It should produce a frame model containing:

- `UiPanel` nodes with stable IDs, parent IDs, bounds, padding, z-order, and layout policy;
- `UiText` nodes with text, semantic role, scale, wrapping policy, minimum readable scale, and overflow policy;
- `UiControl` nodes with bounds, action ID, enabled state, focus order, tooltip/details ID, and input modality;
- `UiDecoration` nodes that are explicitly non-interactive;
- clip regions for panels, scroll viewports, and modal overlays.

The same frame model must drive rendering, mouse hit testing, keyboard/controller focus, tooltip anchoring, and automated validation. Render functions should request named slots such as `header.title`, `content.left`, or `footer.primary`, rather than inventing coordinates locally.

Use a small set of layout primitives:

- `stack` for vertical and horizontal groups;
- `columns` for equal or weighted panels;
- `inset` for padding;
- `flow` for wrapping cards and tags;
- `overlay` for modal and tooltip layers;
- `scrollViewport` for catalogs and long details;
- `anchor` for controls attached to a stable edge.

Keep the 1280×720 logical canvas, but calculate all layout from viewport insets and a UI-scale policy. At smaller physical windows, SDL logical scaling remains responsible for pixel mapping; the layout validator operates in logical coordinates.

## 5. Text and readability contract

Replace ad-hoc strings with typed presentation data:

- `title`: one short line, never truncated;
- `label`: one line, ellipsis allowed only when a tooltip/details view exists;
- `body`: wrapped text with measured line count;
- `stat`: label/value pair with a reserved value column;
- `warning`: short reason plus optional details;
- `instruction`: one action and one input hint.

Every text node must declare its available box. The validator must measure the actual string after localization, dynamic values, and UI scale are applied. It must report:

- viewport overflow;
- panel overflow;
- clipped or hidden lines;
- text intersecting another text/control node;
- unreadable scale below the configured minimum;
- truncation without a details affordance.

Long descriptions should wrap into a measured body region or open a details panel. Do not solve important overflow by silently shrinking text. Use abbreviations only for repeated telemetry, with a legend or tooltip.

Decorative lines must render in a decoration layer below text and controls. No decoration may receive input. All panels should have consistent title, body, and footer padding so text never sits on borders.

## 6. Interaction and click safety

Introduce semantic action IDs, for example `Loadout.OpenSkillBrowser`, `Workshop.BuyNode`, and `Gameplay.CastSkill(2)`. Input should resolve the topmost enabled control from the frame model, not repeat screen-specific rectangle checks.

Rules:

- interactive controls cannot overlap unless the higher-z control is an intentional modal child;
- disabled controls remain visible but cannot capture input;
- a modal captures all input until closed or confirmed;
- clicking blank space never activates a neighboring control;
- hover, focus, pressed, disabled, and selected states are distinct;
- mouse coordinates are converted once from window space to logical space;
- tooltips choose a collision-free candidate position and clamp to the viewport;
- keyboard/controller focus follows the same ordered controls as mouse navigation;
- destructive, expensive, or loadout-replacing actions require a confirmation surface.

The validator should test every control's edge coordinates, neighboring gaps, modal capture behavior, and mouse/focus action agreement. This directly addresses the previous “wrong button or no button” behavior.

## 7. Screen-by-screen redesign

### Main menu and run selection

Use one centered primary menu column, a compact profile summary in the header, and a separate run-type comparison panel. Keep daily briefing content in a scrollable/detail overlay rather than stacking all modifiers into the run-selection screen. Make the selected run type visually dominant and show one clear `CONTINUE` action.

### Loadout

Split the screen into: selected five-skill strip, tower/run configuration, and a right-side summary of the resulting class identity. Open the skill browser as a modal with its own header, search/filter row, scrollable results, and details pane. The modal must never alter the equipped loadout until `EQUIP` is confirmed.

### Workshop

Use tabs or a left navigation rail for Tower, Weapons, Support, Ultimates, and Skills. Each tab gets one currency summary and one content grid. Skill trees should use a zoomable/scrollable node viewport with a fixed details panel; nodes must not be squeezed into a single dense row. Purchase confirmation must show cost, current balance, balance after purchase, and exact gameplay effect.

### Collection

Use category navigation plus a large details view. Keep the list/grid and selected description in separate regions. Long mechanics, strengths, weaknesses, and tags use wrapped content with internal scrolling, never fixed one-line strings.

### Settings

Group settings into Audio, Accessibility, Controls, and Display. Each row is a label, current value, and one control. Keep remapping feedback beside the active row and reserve a footer for reset/back actions.

### Gameplay

Treat the arena as protected space. Place all persistent HUD panels outside the playfield boundary. Skill cards share one grid geometry, while targeting previews and explanations use reserved contextual slots. Keep damage numbers, event feed, and skill effects on separate visual layers with caps and aggregation rules.

### Screen contract matrix

Before migrating a screen, record its contract in one manifest. This prevents a panel from being considered “safe” merely because its own renderer fits:

| Screen/state | Protected regions | Required persistent content | Primary action | Overflow strategy |
|---|---|---|---|---|
| Main menu | header and profile summary | title, profile currencies, navigation | Start run | fixed menu column; no free-floating labels |
| Run type | run cards, footer actions | rules, daily summary, selected state | Choose run | daily details modal/scroll view |
| Loadout | five-slot strip, configuration panels | selected setup, restrictions, class summary | Start run | skill browser modal with scroll + details |
| Workshop | currency header, active tab, details panel | balance, upgrade effect, cost, ownership | Purchase/equip | node viewport scrolls; description remains fixed |
| Collection | category rail, selected details | name, description, mechanics, strengths/weaknesses | Inspect | details region scrolls; list never shares its bounds |
| Settings | row list, footer actions | setting name, value, input hint | Apply/reset | grouped pages; never compress all groups into one panel |
| Gameplay | arena rectangle, bottom skill bar | tower state, wave state, skills, ultimate | Cast/target/pause | event queue and contextual slots, with aggregation |
| Modal/tooltip | modal bounds and focus trap | title, body, close/confirm action | Close or confirm | candidate placement + viewport clamp + collision test |

Each manifest entry should name every panel, text slot, control, clip region, and allowed overlap. A screen is not ready for visual migration until the manifest and its baseline audit both exist.

## 8. Validation and test tooling

Extend `ui_layout.hpp` from rectangle helpers into a reusable layout validator:

1. Build a complete scene manifest for every screen and modal.
2. Assert all nodes are inside the logical viewport or their declared scroll/clip region.
3. Assert permitted parent-child containment and reject unexpected sibling intersections.
4. Assert every text node fits after wrapping, scale, and dynamic substitution.
5. Assert controls have unique action IDs and no ambiguous hit regions.
6. Assert focus order reaches every enabled control and modal focus cannot escape.
7. Assert tooltip candidates fit and do not cover the focused control or required status.
8. Emit machine-readable diagnostics plus a human-readable `text_layout.log`.

Add headless tests for every screen at default, enlarged, high-contrast, and alternate palette settings. Add property-style tests over representative long labels, maximum currency values, daily descriptions, all class names, all skill names, and every localized/dynamic status format.

Add a `--ui-layout-audit` command that builds each screen's scene without presenting graphics and exits nonzero on errors. Keep `--render-smoke` separate: it may exercise SDL's dummy video driver but must never open a user-visible window in CI.

When practical, add deterministic frame snapshots containing only layout metadata (IDs, rectangles, text metrics, z-order), not pixel screenshots. This makes layout regressions reviewable without making tests dependent on renderer output.

The audit should run in this order so failures point to the responsible layer:

1. Resolve the logical viewport and UI scale.
2. Build the screen scene from current state and dynamic content.
3. Validate IDs, parent containment, clip containment, z-order, and permitted overlaps.
4. Measure every text slot after substitutions; validate wrapping, line count, minimum scale, and truncation policy.
5. Validate controls and focus order, including modal capture and edge pixels.
6. Validate tooltip candidates against the focused control, required status, and viewport.
7. Emit stable diagnostics and a metadata snapshot, then fail the test on any error-level issue.

Warnings may be retained for intentionally dense telemetry, but only with an explicit manifest annotation and a test that proves the text remains readable at the supported scale.

## 9. Implementation order

### Concrete code mapping

The current codebase gives a useful migration seam. Keep the simulation and content APIs unchanged while replacing the presentation boundary in this order:

| Migration unit | Current entry points | New responsibility | Gate before moving on |
|---|---|---|---|
| Shared text/layout primitives | `ui_text.hpp`, `drawTextFitInBox`, `drawWrappedTextInBox` | return measured text layouts, explicit overflow policy, and clip bounds | unit tests cover wrapping, long tokens, empty text, scale, and newline cases |
| Frontend shell | `drawMainMenu`, `drawRunTypeSelect`, `drawSettings` | build one header/content/guidance/footer scene | all controls and text slots have IDs; no literal input rectangles remain |
| Loadout/catalog | `drawLoadout`, `drawSkillBrowser`, `drawLoadoutTooltip` | shared card grid, scroll viewport, details pane, tooltip candidates | max-length names/descriptions and modal focus pass at every supported scale |
| Workshop/modals | `drawWorkshopScreen`, `drawWorkshopClassOverview`, `drawWorkshopSkillTree`, `drawWorkshopConfirmation` | tabbed content, fixed details pane, node viewport, confirmation layer | costs, descriptions, disabled states, and modal capture are validated together |
| Collection/daily | `drawCollectionScreen`, `drawDailyBriefingOverlay` | list/detail split and scrollable briefing content | roster, skull, modifier, and reward combinations fit without truncation of required facts |
| Gameplay HUD | `drawHud`, `drawSkillTargetPreview`, `drawWorld` | protected arena plus bottom skill/ultimate bar and contextual slots | all HUD modes, targeting states, and transient queues have non-overlapping scene snapshots |

Do not migrate an entire screen by changing coordinates inside its draw function. First create its scene builder, then make both drawing and input consume the scene, and only then tune the visual spacing. This keeps a visual improvement from reintroducing the original click-target drift.

### Phase 1 — contract and inventory

- Inventory every current draw call, hit test, tooltip, modal, and screen state.
- Convert the current layout constants into named regions and record intentional exceptions.
- Add scene manifests and baseline audit output without changing appearance.

### Phase 2 — shared primitives

- Implement layout nodes, stack/flow/columns, clipping, text measurement, semantic controls, focus order, and logical coordinate conversion.
- Make rendering and input consume the same frame model.
- Port the existing UI tests to the new validator.

### Phase 3 — shell migration

- Migrate main menu, run selection, loadout, and settings.
- Preserve behavior while reducing literal coordinates.
- Add full-screen and enlarged-scale headless audits.

### Phase 4 — complex panels

- Migrate workshop, collection, daily briefing, confirmations, and skill browser.
- Add scrollable details and collision-free tooltips.
- Validate dynamic descriptions and currency/cost states.

### Phase 5 — gameplay HUD

- Reserve protected arena bounds.
- Migrate skill cards, ultimate, resources, target previews, event feed, and class-specific contextual panels.
- Add priority/aggregation rules for transient information.

### Phase 6 — visual tuning and verification

- Tune spacing, contrast, hierarchy, icon scale, and animation only after layout contracts pass.
- Run headless unit/layout/content/replay/balance checks and the UI layout audit.
- Run render smoke with SDL dummy drivers if available; do not launch the game interactively during automated work.

## 10. Definition of done

The overhaul is complete when:

- every screen and modal is generated from the shared layout model;
- rendering, mouse hit testing, hover, keyboard focus, and controller focus agree on the same control bounds;
- no unexpected panel/control/text overlaps are reported;
- no text escapes its box or the logical viewport at supported UI scales;
- important descriptions wrap or open details instead of silently disappearing;
- tooltips and transient messages remain inside the viewport and do not cover required controls;
- gameplay HUD panels never cover protected arena space or one another;
- long dynamic values, daily modifiers, class names, skill names, and maximum resource values pass the same automated audit;
- the headless layout audit, simulation, content, replay, UI, and balance tests pass;
- the game has not been launched as part of verification unless the user explicitly requests a graphics check.
