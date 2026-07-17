---
status: active
audience: contributors
last-verified: 2026-07-16
---

# ADR 0108: Complete the GUI Control, Layout, and Virtual Model Surface

Date: 2026-07-16

## Status

Accepted. This ADR governs review recommendations 18, 20-22, and 26-30.

## Context

The lower C toolkit implements substantially more behavior than the public
runtime exposes. Text input omits selection, password, read-only, multiline,
undo, and events. Widgets omit minimum size, per-edge spacing, tree traversal,
identity, logical/screen bounds, and explicit invalidation. Box layout omits
alignment/justification, while flex, CSS-style grid, and dock containers are not
public.

Tree, tab, split-pane, radio, and label classes expose only a subset of existing
toolkit state. Color controls exist below the runtime but ColorPicker painting
is incomplete and no color class is registered publicly. `Viper.GUI.Grid` is a
non-interactive display table. `VirtualList` and `VirtualTree` are detached data
models; list selection lookup scans rows, visible tree materialization is not
viewport-bounded, and the models do not drive visual controls.

Adding classes and methods changes the runtime C embedding surface and registry,
so an ADR is required.

## Decision

- Add the exact additive callable surface in
  `misc/plans/gui_20260716/01-api-surface.md` sections 3, 5-9. Existing members
  and class names remain compatible.
- `Widget` gains logical minimum/padding/margin geometry, parent/child access,
  removal/clear, stable name/ID lookup, hit testing, screen bounds, and explicit
  invalidation. Parent/child getters return `Option` and borrowed handles.
- `VBox`/`HBox` gain alignment and justification. Public `Flex`, `LayoutGrid`,
  and `DockPanel` classes wrap the existing layout engine. `LayoutGrid` is the
  CSS-style child layout and remains distinct from the tabular `Grid` established
  by ADR 0022.
- TextInput exposes its complete edit/state surface and the common revision/edge
  contract from ADR 0107.
- Tree nodes gain text, icon, stable ID, lazy-child, loading, activation, and
  scroll APIs. Tab, split, radio, and label classes expose their complete model,
  layout, and event state without giving callers ownership of internal payloads.
- Finish toolkit `ColorSwatch`, `ColorPalette`, and `ColorPicker` rendering,
  keyboard interaction, focus visuals, high-contrast behavior, semantic roles,
  and revision events; then expose managed public classes.
- Evolve `Viper.GUI.Grid` in place into a viewport-aware interactive table with
  selection, keyboard navigation, stable row/column state, sorting, resizing,
  editing, and scrolling. Display-only callers retain prior behavior until they
  enable interaction.
- Bind `VirtualList`/`VirtualTree` to `ListBox`/`TreeView` through non-owning
  model links invalidated by either side's destruction. Models keep a hash from
  stable unique ID to index/node and materialize only requested viewport slices.
  Duplicate IDs reject mutation atomically.
- New integer domains are documented through static constant classes rather
  than unlabelled literals. Runtime signatures remain `i64` for ABI and language
  compatibility.

## Consequences

- Viper applications can use the toolkit's real layout and editing capability
  without dropping into C or hand-rolling containers.
- `Grid`, list, and tree controls support large data sets without full model
  scans or full materialization.
- Existing simple applications remain compatible and do not acquire interactive
  behavior unless configured.
- The public surface grows substantially and therefore requires generated docs,
  BASIC/Zia smoke coverage, graphics-disabled twins, and a reviewed surface
  fingerprint.
- Model bindings introduce lifecycle edges, covered by owner-destroy-first,
  model-destroy-first, and rebind tests.

## Alternatives Considered

- Create a second interactive table class. Rejected because it would duplicate
  the existing `Grid` cell/column model and strand display-only callers on a
  permanently limited class.
- Expose toolkit pointers directly. Rejected because private layouts are not a
  supported public ABI and stale handles need runtime-managed validation.
- Keep virtual models detached and ask applications to copy visible rows into
  controls. Rejected because it defeats virtualization and creates synchronization
  bugs at every call site.
- Name the layout container `Grid`. Rejected because ADR 0022 already assigns
  that public name to the data table; `LayoutGrid` is unambiguous.

