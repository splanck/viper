# GUI Modernization Validation Matrix

## 1. Exact error contract

New `Result` and asynchronous APIs use the following exact messages. Existing
compatibility APIs retain their prior sentinel behavior.

| Condition | Exact message |
|---|---|
| Graphics disabled | `GUI support is not available in this build` |
| Application state allocation failed | `GUI application state could not be allocated` |
| Backend/window creation failed | `GUI application window could not be created` |
| Root widget allocation failed | `GUI root widget could not be allocated` |
| Invalid parent handle | `GUI parent widget is invalid` |
| Image dimensions invalid | `Image dimensions must be positive and fit in memory` |
| Image allocation failed | `Image pixel storage could not be allocated` |
| Unknown theme color token | `Unknown GUI theme color token: <name>` |
| Unknown theme metric token | `Unknown GUI theme metric token: <name>` |
| Invalid theme value | `GUI theme token <name> has an invalid value` |
| Duplicate virtual-model ID | `GUI model ID must be unique: <id>` |
| Duplicate dialog button ID | `Message box button ID must be unique: <id>` |
| Dialog already open | `GUI dialog is already open` |
| No active GUI app | `No active GUI application is available` |
| Unsupported file-dialog operation | `The requested file dialog operation is not available` |
| Video open failed | `Video could not be opened: <path>` |
| Invalid accessibility relationship | `Accessibility label target must belong to the same application` |

Errors are stored in the operation/controller and never written only to stderr.
Invalid handles return the established empty/status value without dereferencing
freed memory. Allocation failure is atomic: previous public state is unchanged.

## 2. Performance budgets

| Path | Budget after warm-up |
|---|---|
| Idle `App.RunFrame` | zero heap allocations; no present when undamaged |
| Widget animation frame | zero heap allocations; one tree scheduling pass |
| Subhandle lookup | expected O(1); no full global scan |
| Stale-handle validation | bounded O(1) lookup; no freed-pointer read |
| Text cursor movement | O(grapheme length crossed), no full-document copy |
| Virtual-list selection lookup | expected O(1) by stable ID |
| Virtual-tree viewport | O(visible slice + expanded ancestors), not all nodes |
| Grid paint | O(visible rows × visible columns) |
| Minimap steady paint | O(dirty visible/cache lines), not full document |
| 1:1 opaque image paint | row-copy fast path, no per-pixel float mapping |
| Video frame upload | at most one reusable conversion/upload buffer |
| Overlay-only frame | damage-limited; must not repaint normal root content |
| Theme/UI-scale change | allocation permitted once per explicit change |

Representative stress sizes are 100,000 virtual-list rows, 100,000 virtual-tree
nodes with a 100-row viewport, a 10,000-row × 20-column grid, a 50,000-line
editor/minimap, and 1,000,000 widget create/destroy cycles for liveness probing.

## 3. Given/When/Then tests

### Lifetime and contracts

- **Given** retained tree/tab wrappers, **when** remove, prune, and stale wrapper
  calls are interleaved, **then** no freed memory is read and stale operations
  return empty/status values.
- **Given** at least one million create/destroy cycles, **when** a missing widget
  is queried, **then** lookup terminates and live counts return to baseline.
- **Given** the live API dump, **when** every GUI object return is inspected,
  **then** constructor, lookup, child, and owned-result contracts match the
  reviewed manifest fingerprint.
- **Given** graphics-disabled compilation, **when** capability and `TryNew` are
  called, **then** they return false/failure with the exact capability message.

### Theme, scale, and rendering

- **Given** a clean rendered tree, **when** theme or UI scale changes, **then**
  layout and paint occur on the next frame without unrelated invalidation.
- **Given** inherited and explicit colors, **when** the theme changes, **then**
  inherited values change and explicit values do not.
- **Given** 1×, 1.5×, and 2× effective scale, **when** controls render, **then**
  geometry, font metrics, radii, shadows, and focus glow scale consistently.
- **Given** a moving elevated/focused widget under partial paint, **when** old and
  new frames are compared to full repaint, **then** every pixel matches.
- **Given** every built-in theme text/state pairing, **when** contrast is checked,
  **then** normal text is at least 4.5:1 and large/decorative exceptions are
  explicitly listed.
- **Given** light and dark editor themes, **when** syntax is rendered, **then**
  the active theme tokens appear unless a user override is set.

### Input, events, and accessibility

- **Given** combining marks, emoji ZWJ sequences, regional indicators, and
  variation selectors, **when** cursor/delete/selection operations run, **then**
  they never split a grapheme cluster.
- **Given** composition start/update/commit/cancel platform events, **when** a
  text input is focused, **then** preedit is visible, commit creates one undo
  record, and cancel restores prior text.
- **Given** two observers using revision and edge APIs, **when** a control
  changes, **then** revision remains observable and consuming one edge kind does
  not clear another.
- **Given** an accessibility-labelled widget tree, **when** snapshot and native
  adapter queries run, **then** role/name/value/state/relationships/bounds agree.
- **Given** reduced motion, **when** state changes, **then** final visual states
  apply immediately and no animation deadlines remain.

### Controls and layout

- **Given** each alignment/justification/wrap/dock combination, **when** the same
  logical tree is laid out at multiple scales, **then** logical bounds remain
  invariant within rounding tolerance.
- **Given** invalid spans, indices, duplicate IDs, and detached children,
  **when** layout/model APIs are called, **then** state remains valid and the
  documented false/empty result is returned.
- **Given** a virtualized 100,000-row model, **when** viewport, selection, sort,
  and edit operations run, **then** only visible rows are materialized and the
  stable selected ID survives reorder.
- **Given** keyboard-only input, **when** navigating tree, tabs, grid, colors,
  split panes, and dialogs, **then** focus and activation match pointer behavior.

### Dialogs, media, and services

- **Given** a dialog opened during event dispatch, **when** frames continue,
  **then** no nested poll occurs and completion is reported exactly once.
- **Given** localized button labels, **when** Enter/Escape is pressed, **then**
  explicit semantic roles select the correct button.
- **Given** accept-empty and cancel prompt paths, **when** results are read,
  **then** `Some("")` and `None` are distinguishable.
- **Given** two apps, **when** current app, cursor, wheel, theme, and dialogs are
  changed, **then** state is isolated to the selected app.
- **Given** image OOM injection, **when** upload/update runs, **then** it fails and
  the old pixels remain byte-identical.
- **Given** auto-updated video, **when** manual update also occurs in one frame,
  **then** decoding/upload happens once and events are emitted once.

### Automation and visual testing

- **Given** a bound real app, **when** harness keyboard/mouse events dispatch,
  **then** hit testing, focus, callbacks, and accessibility state change exactly
  as normal polling would.
- **Given** deterministic delta and mock/software framebuffer, **when** a frame
  is captured twice, **then** pixel data and hash are byte-identical.
- **Given** light/dark, 1×/2×, enabled/disabled, hover/press/focus, overlay, and
  partial/full matrices, **when** captures are compared, **then** partial and full
  paths match and approved goldens remain stable.

## 4. Required test layers

1. Lower toolkit unit tests under `src/lib/gui/tests`.
2. Runtime C/C++ tests under `src/tests/runtime`.
3. Registry contract/fingerprint tests and `--dump-runtime-api` diff.
4. Zia runtime fixture covering construction and new methods.
5. BASIC surface/link fixture for representative APIs.
6. Graphics-disabled symbol and semantic tests.
7. Mock/software framebuffer pixel tests.
8. Platform adapter smoke for macOS, Windows, and Linux.
9. ASan/UBSan lifetime run when available.
10. Allocation/performance counters for the budgets above.

## 5. Commands per increment

```sh
VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 \
VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 \
./scripts/build_viper_mac.sh

ctest --test-dir build -L gui --output-on-failure
ctest --test-dir build -R 'test_rt_gui_runtime|test_rt_gui_ide|zia_runtime_test_gui' \
  --output-on-failure
./scripts/check_runtime_completeness.sh
./scripts/audit_runtime_surface.sh
./scripts/lint_platform_policy.sh --changed-only
```

At program completion run the unskipped platform build script,
`./scripts/run_cross_platform_smoke.sh`, the full CTest suite, documentation
generation/link checks, and every new GUI visual/contract/performance test.
