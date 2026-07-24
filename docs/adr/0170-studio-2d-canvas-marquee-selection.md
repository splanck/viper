---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0170: Add Modifier and Marquee Selection to the Studio 2D Canvas

## Status

Accepted (2026-07-23)

## Context

Zanna Studio's 2D object hierarchy supports stable multi-selection, but the
canvas does not. A canvas click either preserves an already-selected group for
dragging, replaces it with one cell hit, or clears it. Shift,
Control/Command, and blank-space drags have no selection meaning. Dense levels
therefore require repeated hierarchy navigation even when the desired objects
are visibly adjacent.

The canvas also uses a non-focusable image as its presentation surface. Its
selection gestures must keep using the active mode button as the keyboard focus
proxy, must not steal text-field shortcuts, and must not turn presentation
state into canonical `.scene` history.

## Decision

Select mode resolves a left-button press in this order:

1. the topmost object occupying the pointed authored cell; or
2. a blank-space marquee beginning at that cell.

A point hit follows these rules:

- an unmodified hit replaces the selection, except that pressing an already
  selected object preserves the complete group so it can be dragged together;
- Shift adds the hit;
- Control or Super toggles the hit; and
- a modified point click never begins an object move.

A marquee is an inclusive authored-cell rectangle. On release, objects are
collected in canonical draw order by the cell containing their authored point:

- an unmodified marquee replaces the selection;
- Shift unions its hits with the prior selection;
- Control or Super toggles every hit against the prior selection;
- an unmodified empty marquee clears; and
- a modified empty marquee preserves the prior selection.

The most recently hit topmost object is primary. If a toggle removes it, the
previous primary remains primary when still selected; otherwise the last
remaining selected object becomes primary.

The marquee has a visible outlined overlay and status description while active.
It captures the pointer so release cannot leave Studio in a stuck gesture.
Escape cancels the marquee, releases capture, and preserves the pre-gesture
selection.

Point and marquee selection update only the active document's 2D workspace
selection. They do not change scene JSON, editor revision, dirty state, or
undo/redo history. Existing paint, erase, object placement, and group-drag
transactions remain canonical editing gestures.

## Consequences

- Dense 2D scenes can be selected spatially without reconstructing the same set
  through the hierarchy.
- Canvas and hierarchy selection use the same conventional modifiers on macOS,
  Windows, and Linux through the public Control and Super key constants.
- Objects remain point-based editor entities. The marquee selects the authored
  cell containing each point; future sprite/collider bounds may add a richer
  hit contract without changing modifier semantics.
- A focused production-input probe must cover point replacement, additive and
  toggle helpers, forward and reverse marquee rectangles, blank behavior,
  visible overlay state, pointer release, cancellation, and complete canonical
  document/history isolation.

## Alternatives Considered

- **Require hierarchy multi-selection.** Rejected because it makes spatial
  authoring needlessly indirect in large levels.
- **Select while the marquee moves.** Rejected because it makes modifier
  toggles depend on frame sampling and complicates cancellation. Selection is
  applied once on release.
- **Treat object positions as pixel-sized hit boxes.** Rejected for now because
  the current scene format exposes authored points, not sprite or collider
  bounds. Inclusive cell membership is deterministic and matches existing
  point selection and drag behavior.
- **Commit selection to scene JSON.** Rejected because selection is per-tab
  workspace state, not game data.
