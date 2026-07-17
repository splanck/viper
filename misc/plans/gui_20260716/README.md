# Viper GUI Modernization Program

Date: 2026-07-16

## 1. Summary and objective

This program implements the forty accepted improvements from the 2026-07-16
review of the `Viper.GUI.*` runtime surface, its C/C++ embedding layer, and the
lower C GUI toolkit. The objective is a safe, complete, accessible,
cross-platform GUI runtime whose public contracts match its implementation and
whose retained renderer remains deterministic and efficient.

The work is additive and compatibility preserving. Existing public classes,
methods, synchronous conveniences, and legacy sentinel-returning operations
remain available. Unsafe implementations are repaired behind their existing
names; new `Option`, `Result`, asynchronous, typed-constant, and model-aware
operations are added alongside older forms.

The implementation is governed by ADRs 0106 through 0109. Exact public names
and signatures are recorded in [01-api-surface.md](01-api-surface.md), and the
required error, performance, and test contracts are recorded in
[02-validation-matrix.md](02-validation-matrix.md).

## 2. Scope

### In scope

- All current classes under `Viper.GUI.*`, including `App`, `Widget`, controls,
  layout containers, dialogs, models, editor/navigation helpers, images, and
  video.
- The canonical runtime registry and its generated Zia/BASIC/API-dump/docs
  consumers.
- The internal C embedding ABI in `src/runtime/graphics/gui`.
- The lower C GUI toolkit in `src/lib/gui` and ViperGFX event/render adapters.
- Graphics-disabled behavior, software/mock rendering, and macOS, Windows, and
  Linux platform adapters.
- Unit, runtime, language integration, visual framebuffer, contract-manifest,
  lifecycle, and performance tests.
- Generated runtime reference and authored GUI guides.

### Out of scope

- A new IL opcode, grammar production, verifier rule, or language-level event
  syntax.
- Replacing the software retained renderer with a third-party renderer.
- External packages, font libraries, image libraries, accessibility libraries,
  or dialog libraries.
- Removing an existing public API. Legacy surfaces may be documented as such
  only after a compatible replacement is implemented and tested.
- Changing `Viper.GUI.App.GetWidth`, `GetHeight`, `Widget.GetWidth`, or related
  legacy physical-unit getter semantics. New logical getters provide the
  normalized contract without silently breaking callers.

## 3. Feature toggles

No user-facing feature toggle is required. Correct lifetime handling, complete
theme invalidation, logical sizing, accessibility metadata, and deterministic
event delivery are correctness contracts and are enabled by default.

Existing build capabilities remain authoritative:

- `VIPER_ENABLE_GRAPHICS=0` selects deliberate graphics-disabled stubs.
- Existing ViperGFX backend selection chooses software, mock, Metal, D3D11, or
  OpenGL behavior.
- `App.SetPartialPaint` remains the diagnostic/performance switch for retained
  damage rendering.

Compatibility wrappers are not controlled by a toggle. They remain callable
and forward to the new implementation.

## 4. Configuration

There are no environment variables or configuration files added by this
program. Runtime configuration is explicit through public APIs:

- theme mode, palette, high contrast, and reduced motion;
- per-app UI scale, wheel speed, and timer scheduling;
- per-widget cursor, accessibility, layout, and event state;
- dialog options and asynchronous state;
- image filtering and video playback state.

Defaults are deterministic: dark theme unless system mode is selected,
high-contrast and reduced-motion disabled unless the system adapter reports
them, UI scale `1.0`, nearest image filtering for legacy images, bilinear
filtering only when explicitly requested, and synchronous wrappers retaining
their prior behavior.

## 5. Architecture contract

### 5.1 Public boundary

The canonical public boundary is the registry rooted at
`src/il/runtime/runtime.def`. Every new class has authored `@summary` and
`@details` documentation. Every new operation has a declared C symbol,
graphics-disabled twin when applicable, contract metadata, and generated API
documentation.

The C functions are the Viper internal embedding ABI. Runtime objects remain
opaque. Public headers may expose POD descriptors and integer constants, but
never private widget, model, subhandle, font, compositor, or platform-object
layouts.

### 5.2 Ownership and failure

- Widget constructors are managed, fallible object results. Compatibility
  constructors retain nullable behavior; `Try*` variants return `Viper.Result`.
- Child widgets owned by containers, roots, tabs, menu items, and tree nodes are
  borrowed managed handles. Destroying the owner invalidates the wrapper.
- Lookup and selection absence uses `Viper.Option` in new APIs. Legacy nullable
  or sentinel operations remain as compatibility forms.
- Removed subobjects remain valid inert handles until the last runtime wrapper
  is released. Explicit prune calls may request reclamation but cannot make a
  live wrapper unsafe.
- Asynchronous dialog/media results are owned by their controller until read;
  returned strings/sequences are ordinary managed runtime values.

### 5.3 Units

- Public layout setters and all newly added bounds getters use logical units.
- Legacy getters documented as physical remain physical.
- The toolkit stores arranged geometry in physical framebuffer units. The
  runtime converts exactly once at the app boundary using effective scale
  `window_scale * user_ui_scale`.
- Font sizes are logical points and are converted to effective pixels whenever
  the app scale changes.
- Theme spatial metrics are logical values in base themes and effective pixels
  in per-app theme instances. Motion durations are milliseconds and never DPI
  scaled.

### 5.4 Events and time

- Every stateful control has a monotonically increasing 64-bit revision.
- Compatibility `Was*` methods consume only their own edge counter. Reading one
  event kind never clears a different event kind.
- New revision getters are non-consuming and support multiple observers.
- `App.Poll` ingests platform events. `App.RunFrame` advances the deterministic
  clock, animations, timers, dialogs, video, layout, paint, and presentation.
- `App.PollWait` wakes no later than the next registered GUI deadline.

### 5.5 Rendering

- The software framebuffer is the semantic reference.
- Every widget reports visual overflow beyond arranged bounds.
- Normal content and overlay layers maintain separate damage.
- Animation advancement is independent of whether a widget happened to paint.
- Image mutation is atomic: allocation or validation failure preserves the
  previous image and returns failure.

### 5.6 Accessibility

Every widget owns a semantic record containing role, name, description, value,
enabled/checked/selected/expanded state, label relationship, and live-region
mode. The tree is available headlessly for tests and is projected through
platform adapters on macOS, Windows, and Linux. Unsupported native projection
must not remove the headless semantic tree.

## 6. Recommendation-to-workstream map

| Review item | Required outcome | Primary workstream |
|---:|---|---|
| 1 | Safe retained-node/tab pruning | Lifetime and contracts |
| 2 | Bounded live-widget hash probing | Lifetime and contracts |
| 3 | Exact nullability and ownership metadata | Lifetime and contracts |
| 4 | GUI capability and fallible construction | Lifetime and contracts |
| 5 | Theme/UI-scale invalidation | Theme and rendering |
| 6 | Inherited versus explicit styling | Theme and rendering |
| 7 | Complete spatial token scaling | Theme and rendering |
| 8 | Logical font scaling | Theme and rendering |
| 9 | Consistent logical public layout units | Theme and rendering |
| 10 | Real-time animation scheduler | Scheduling |
| 11 | Visual-overflow-aware damage | Theme and rendering |
| 12 | Atomic status-returning image upload | Images and media |
| 13 | Non-reentrant asynchronous dialogs | Dialogs |
| 14 | Per-app activation/services/cursors | App services |
| 15 | Accessible built-in contrast | Theme and accessibility |
| 16 | Theme-derived editor syntax | Theme and editor |
| 17 | Semantic accessibility tree and bridges | Accessibility |
| 18 | Complete text-input editing API | Input and controls |
| 19 | Grapheme and IME composition editing | Input and controls |
| 20 | Uniform revisions and edge events | Input and controls |
| 21 | Complete base Widget API | Controls and layout |
| 22 | Flex/grid/dock and box alignment | Controls and layout |
| 23 | Custom/system/high-contrast themes | Theme and accessibility |
| 24 | Cross-platform complete file dialogs | Dialogs |
| 25 | Semantic/localizable message boxes | Dialogs |
| 26 | Interactive virtualized data grid | Controls and models |
| 27 | Visual virtual-list/tree integration | Controls and models |
| 28 | Complete tree-view API | Controls and models |
| 29 | Complete tabs/split/radio/label APIs | Controls and layout |
| 30 | Finished public color controls | Controls and layout |
| 31 | App-scheduled efficient video | Images and media |
| 32 | Proportional/bold/mono typography roles | Theme and rendering |
| 33 | Damage-tracked overlay composition | Theme and rendering |
| 34 | Indexed subhandles and automatic reclamation | Lifetime and contracts |
| 35 | Reference-counted/generational fonts | Lifetime and contracts |
| 36 | Shared efficient image decode/render path | Images and media |
| 37 | Revision-aware cached minimap | Editor and models |
| 38 | Central timer/deadline scheduler | Scheduling |
| 39 | Real deterministic GUI automation/capture | Testing |
| 40 | Typed constants/records and debug stats | Public API governance |

## 7. Delivery phases

Each phase is a coherent green increment and changes fewer than fifty files.

1. **Foundation:** ADRs, specification, baseline dumps, lifetime fixes, hash
   fixes, contract manifest, capability API, and lifecycle tests.
2. **Theme and geometry:** invalidation, inherited styles, complete scale,
   logical font/geometry APIs, animation scheduling, overflow damage, contrast,
   syntax colors, and framebuffer tests.
3. **Input and accessibility:** semantic tree, platform adapters, text editing,
   graphemes, composition events, control revisions, and accessibility tests.
4. **Layout and controls:** Widget additions, box/flex/grid/dock layouts, theme
   palette, tree/tabs/split/radio/label, color controls, and interactive grid.
5. **Models and services:** virtual list/tree integration, dialogs, app services,
   image pipeline, video scheduling, typography roles, and minimap cache.
6. **Composition and tooling:** overlay layers, indexed subhandles, font
   retirement, deadline scheduler, real TestHarness, visual matrix, API cleanup,
   generated docs, examples, and full platform validation.

## 8. Documentation policy

- All new C/C++ source and header files use the full Viper source header.
- Every new public or internal function declared in a header has detailed
  Doxygen covering parameters, return value, ownership, units, failure, and
  invalid-handle behavior.
- Existing headers modified by this program receive Doxygen for every new
  declaration, even when neighboring legacy declarations are terse.
- Runtime registry class documentation is the canonical generated reference.
- Authored guides explain lifecycle, units, events, accessibility, theming,
  dialogs, virtualization, and testing without duplicating generated signatures.

## 9. Completion criteria

The program is complete only when every row in the recommendation map is
implemented, documented, and covered; the live API dump contains the intended
surface and exact contracts; graphics-enabled and graphics-disabled builds link;
all existing and new tests pass; full macOS/Linux/Windows policy checks pass;
and the canonical platform build script completes without skips or warnings.

## 10. Implementation record

The following record was verified against the live runtime registry and source
tree on 2026-07-17. “Complete” means the behavior is implemented additively,
registered where public, documented, and exercised by at least one of the test
layers in [02-validation-matrix.md](02-validation-matrix.md). No legacy GUI API
was removed.

| Review item | State | Delivered implementation evidence |
|---:|---|---|
| 1 | Complete | Retained tree/tab wrappers use invalidation-safe removal and explicit prune paths in `rt_gui_subhandle.c`. |
| 2 | Complete | Live-widget registration uses bounded probing, tombstone reuse, and growth/compaction tests in `vg_widget.c`. |
| 3 | Complete | Exact ownership/nullability contracts are registry metadata and are fingerprinted by `test_gui_runtime_manifest`. |
| 4 | Complete | `System.IsAvailable`, the unavailable reason, and fallible `App.TryNew` work in enabled and disabled builds. |
| 5 | Complete | Theme and effective-scale changes invalidate layout, font metrics, paint, and overlays through the app theme pipeline. |
| 6 | Complete | Widget style state distinguishes inherited theme values from explicit caller overrides across theme changes. |
| 7 | Complete | The full spatial token set scales once into each per-app effective theme, including radii, shadows, gaps, and focus glow. |
| 8 | Complete | Logical font points are re-resolved at effective-scale changes with regular, bold, and monospaced roles. |
| 9 | Complete | New public geometry and layout APIs use logical units with one app-boundary conversion and retain legacy physical getters. |
| 10 | Complete | Animation advancement is deadline-driven and independent of whether a widget happened to repaint. |
| 11 | Complete | Widgets report visual overflow and damage includes both previous and current overflow bounds under partial paint. |
| 12 | Complete | Image upload/update validates and allocates before commit, returns status, and preserves old pixels on failure. |
| 13 | Complete | File and message dialogs have frame-driven asynchronous controllers; synchronous compatibility methods remain. |
| 14 | Complete | Current-app activation, cursor, wheel, clipboard, timer, and dialog services are isolated per application. |
| 15 | Complete | Built-in light/dark/high-contrast palettes enforce accessible state/text contrast and reduced-motion behavior. |
| 16 | Complete | Editor syntax colors inherit named theme tokens unless a caller installs an explicit override. |
| 17 | Complete | Every widget has a headless semantic record plus macOS, Windows, and Linux native accessibility adapters. |
| 18 | Complete | TextInput exposes selection, insertion/deletion, clipboard, undo/redo, limits, password, read-only, and multiline operations. |
| 19 | Complete | Unicode 17 extended-grapheme editing and atomic IME preedit/commit/cancel handling are implemented and conformance-tested. |
| 20 | Complete | Stateful controls expose monotonic revisions while compatibility edge consumers remain independent by event kind. |
| 21 | Complete | Base Widget now covers logical bounds, visibility/enabled state, focus/cursor, style, semantics, parentage, and revision. |
| 22 | Complete | VBox/HBox alignment and justification plus Flex, Grid, Dock, wrapping, tracks, spans, gaps, and padding are public. |
| 23 | Complete | Dark, light, system-following, custom-palette, high-contrast, and reduced-motion theme modes are complete. |
| 24 | Complete | File-dialog options, filters, async status/results, directory/save/open modes, and platform adapters are implemented. |
| 25 | Complete | Message boxes use stable IDs and semantic default/cancel/destructive roles instead of localized label matching. |
| 26 | Complete | DataGrid supports virtual visible cells, stable selection, sorting, editing, keyboard/pointer input, and revisions. |
| 27 | Complete | ListBox and TreeView can bind 100,000-row virtual models without materializing retained child objects. |
| 28 | Complete | Tree nodes expose stable IDs, text/icons, lazy/loading state, activation, load requests, scrolling, and revisions. |
| 29 | Complete | Tabs, split panes, radio groups/buttons, and labels have the missing state, movement, collapse, selection, and typography APIs. |
| 30 | Complete | ColorSwatch, ColorPalette, and ColorPicker have complete construction, selection, alpha/channel, edge, and revision APIs. |
| 31 | Complete | Video decoding/upload is app-scheduled, uses reusable frame storage, coalesces manual/automatic updates, and emits revisions. |
| 32 | Complete | Proportional regular/bold and monospaced typography roles resolve through the theme with deterministic embedded fallbacks. |
| 33 | Complete | Normal content and overlays have separate damage tracking and overlay-only frames avoid repainting the normal root. |
| 34 | Complete | Subhandles use indexed identity/generation lookup, inert stale wrappers, and automatic reclamation after the last wrapper. |
| 35 | Complete | Fonts are reference-counted, generation-validated, retired safely, and released only after all users detach. |
| 36 | Complete | Runtime and lower-toolkit images share validated storage, fast opaque copies, and nearest/bilinear render paths. |
| 37 | Complete | Minimap rendering caches revision-aware line work and invalidates only affected visible/cache regions. |
| 38 | Complete | One app deadline scheduler coordinates timers, animation, dialogs, video, polling waits, and deterministic frames. |
| 39 | Complete | TestHarness binds a real app, posts normal input events, advances deterministic frames, captures/compares pixels, and snapshots semantics. |
| 40 | Complete | Twelve typed constant classes publish 77 stable getters; CodeEditor returns a complete versioned performance-stat Map. |

## 11. Verification record

- The canonical macOS build script completed with all 1,902 non-slow CTests,
  runtime audits, lint, and cross-platform smoke checks passing.
- The post-format GUI label passed 20/20 tests. The focused GUI suites also
  passed 20/20 under both ASan and UBSan.
- Strict runtime-surface synchronization passed with 7,428 functions, 524
  classes, and 8,684 matching header declarations; the GUI manifest contains
  1,108 functions, 79 classes, 110 properties, and 999 methods.
- Documentation generation/checking, source-header checks, formatter checks,
  platform-policy lint, Zia coverage, BASIC coverage, native ViperIDE linking,
  and the graphics-disabled GUI availability contract all pass.
- The whole-repository graphics-disabled link remains independently blocked by
  unrelated in-progress 3D symbols in the shared worktree; the disabled GUI
  runtime archive links and its availability/schema test passes directly.
- The broad sanitizer wrapper and runtime-sweep fixtures still expose existing
  non-GUI installer, timeout, sanitizer-import, language, network, and graphics
  baseline failures. The focused GUI sanitizer and canonical suites are clean.
