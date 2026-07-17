---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0107: Unify GUI Theme, Accessibility, Input, and Rendering State

Date: 2026-07-16

## Status

Accepted. This ADR governs review recommendations 5-8, 10-11, 15-20, 23, 32-33,
and 38.

## Context

Per-app themes are rebuilt for theme, backing-scale, and user-scale changes, but
the root tree is not necessarily invalidated. Many widgets also copy colors,
fonts, and metrics at construction, preventing a repaint from fully adopting a
new theme. The scale pass omits several spatial tokens, and positive default
font sizes bypass theme scaling while the graphics coordinate scale remains one.

Widget hover, press, and focus animations advance only during paint using a
synthetic 16 ms step. Paint then clears dirty state, so an animation may advance
once and freeze. Damage bounds allow only a one-pixel anti-alias margin despite
shadows and focus glows extending farther, while any visible overlay disables
partial paint entirely.

The built-in accent/foreground choices include normal-text contrast below the
runtime's own 4.5 threshold, and CodeEditor syntax defaults are hard-coded for a
dark palette. Accessibility exposes contrast math but no semantic tree. Text
input supports substantial lower-level editing features but public input is
codepoint-oriented and has no explicit composition/preedit lifecycle. Control
edge events are inconsistent and consuming one snapshot can make multi-observer
code fragile.

These are public API, runtime ABI, platform input/accessibility, and rendering
contract changes.

## Decision

### Theme and inherited styles

- Every widget style value records whether it is inherited or explicitly set.
  Widget vtables receive an `apply_theme` operation. Theme changes walk the tree,
  update inherited values, preserve overrides, recompute effective fonts, mark
  layout, mark paint, and advance a per-app theme revision.
- Base themes store logical metrics. Per-app theme instances scale every spatial
  value, including radii, borders, elevation offsets/blur, focus glow, spacing,
  control metrics, typography, and scrollbar metrics. Motion durations are not
  scaled.
- Font sizes are logical points. Regular, bold, and mono roles are distinct and
  resolved through a zero-dependency platform font adapter with deterministic
  embedded fallback. Editor/code controls use mono; application chrome uses
  regular/bold roles.
- `ThemeMode` supports dark, light, system, and custom. `ThemePalette` is an
  opaque managed palette with named color/metric access, validation, cloning,
  font roles, and motion state. Existing `SetDark` and `SetLight` forward to
  `SetMode`.
- Shipped token/state combinations used for normal text meet at least 4.5:1.
  Controls choose an accessible foreground for the resolved background rather
  than forcing white. CodeEditor defaults use active theme syntax tokens unless
  explicitly overridden.

### Time, animation, and damage

- Animation state advances in an app-owned scheduler using measured or injected
  elapsed time, independently of painting. Animated widgets remain scheduled
  until they reach their target; reduced-motion mode applies the target
  immediately.
- Widgets report visual overflow on every side. Damage unions previous and
  current arranged bounds expanded by overflow. Full and partial software
  framebuffer results must be pixel-identical.
- Normal content and overlays maintain distinct damage and paint generations.
  A visible overlay does not force root repaint; only overlay changes damage the
  overlay layer. Previous and current overlay bounds are unioned across show,
  move, animation, dismissal, and replacement. Overlay drawing receives the
  same damage clip as normal content, so a compact tooltip or toast cannot
  write outside the retained partial-paint region. Composition remains
  deterministic in the software reference.
- `App.GetNextDeadlineMs`, `RunFrame`, and `RunFrameWithDelta` centralize timer,
  animation, tooltip, toast, cursor, progress, dialog, minimap, and video wakeups.
  Tooltip managers report pending-show, delayed-hide, and finite-duration
  deadlines; notification managers report initialization, entrance/exit ticks,
  and finite dwell expiry. `PollWait` never sleeps past the nearest deadline.
  Deterministic frame injection advances one app-owned timer clock by the full
  caller delta while widget motion uses the established 250 ms safety cap, so
  long test frames expire timers accurately without destabilizing animation.

### Accessibility

- Every widget contains a toolkit-owned semantic record: role, name,
  description, value, state flags, label target, live-region mode, logical
  bounds, and semantic revision.
- The semantic tree mirrors visible widget ownership but may omit purely
  decorative widgets and may expose logical virtual children. It is available
  through a deterministic snapshot for tests and automation.
- Platform adapters project the same tree through macOS accessibility, Windows
  UI Automation, and Linux AT-SPI-compatible facilities using only platform
  APIs. Adapter unavailability never removes the headless tree.
- High contrast and reduced motion may be explicitly overridden or inherited
  from platform capability state. Live-region announcements carry polite or
  assertive semantics.

### Text and control events

- Text cursor and selection public indices are Unicode extended grapheme-cluster
  indices. The implementation includes a dependency-free grapheme segmenter for
  the Unicode rules and data version documented beside its generated table.
- Platform events add composition start/update/commit/cancel with selection and
  replacement range. Preedit is rendered separately from committed text;
  committing creates one undo record, and cancellation restores prior state.
- Every stateful control maintains a monotonically increasing revision and
  independent edge counters for change, submission, activation, selection,
  reordering, editing, and other relevant event kinds. Consuming one edge never
  consumes another; revision getters are non-consuming.
- Lower callbacks feed this common state before optional control-specific
  behavior, so pointer, keyboard, automation, and programmatic mutation follow
  one documented path.

## Consequences

- Theme and scale changes become immediately visible and respect explicit
  overrides.
- Fonts, geometry, shadows, and focus visuals scale together.
- Animations complete with real time and idle apps wake only when needed.
- Partial painting remains correct for shadows and overlays while recovering
  useful performance.
- All controls expose consistent observable state, text editing respects user-
  perceived characters, and IME users receive real preedit behavior.
- Native accessibility requires three platform adapters and ongoing conformance
  tests, but their semantic source is one cross-platform tree.

## Alternatives Considered

- Recreate every widget on theme change. Rejected because it destroys focus,
  selection, model identity, and application-held handles.
- Scale the entire graphics coordinate system instead of boundary conversion.
  Rejected because existing GUI storage and physical framebuffer APIs would be
  silently reinterpreted.
- Continue advancing animations from paint. Rejected because damage-driven
  rendering intentionally skips clean frames.
- Add only committed IME text. Rejected because users need visible preedit,
  selection, cancellation, and one coherent undo record.
- Use an external Unicode or accessibility library. Rejected by the project's
  zero-dependency requirement.
