---
status: active
audience: contributors
last-verified: 2026-07-24
---

# ADR 0171: Add Bounded Scene Flood Fill and Studio Tile Tools

## Status

Accepted (2026-07-24)

## Context

Zanna Studio's 2D scene canvas supports gap-free freehand paint and erase
strokes, but it cannot paint a rectangle, replace a contiguous region, or
sample an authored tile. Building a large map therefore requires tracing every
area with the pointer and manually looking up tile IDs. The runtime already
exposes `SceneDocument.FillTiles` for rectangles, but it has no bounded
contiguous-fill primitive that Studio can call without implementing a
million-cell work queue in interpreted Zia on the UI thread.

Tile gestures also need the transaction and cancellation guarantees already
used by the stronger 3D transform tools. A rectangle preview must not mutate
the scene before release, an interrupted freehand stroke must not remain
partially applied, and sampling must remain workspace-only.

## Decision

Add this runtime method:

```zia
SceneDocument.FloodFill(layer, x, y, tile) -> Integer
```

The method replaces the four-connected region containing `(x, y)` whose cells
have the starting tile ID. It returns the exact number of changed cells.
Invalid scene/layer/cell input and an already-equal replacement return zero.
Diagonal contact does not connect regions.

`FloodFill` is bounded by the existing `SceneDocument` limit of 1,048,576
cells per layer. It allocates its complete queue and visited storage before the
first mutation. Allocation failure therefore returns zero through the runtime
trap boundary without leaving a partially filled layer. After allocation, each
cell can enter the queue at most once and the operation performs no further
dynamic allocation.

Studio adds three explicit tile tools beside Paint and Erase:

- **Rectangle** captures a blank canvas drag, shows an inclusive cell outline,
  and fills it with the selected tile exactly once on release;
- **Fill** invokes `SceneDocument.FloodFill` once for the clicked active-layer
  cell; and
- **Pick** samples the active-layer cell into the tile field and returns to
  Paint without changing canonical scene content.

Tile ID zero remains the canonical empty tile, so Rectangle and Fill can clear
regions by selecting zero. Forward and reverse rectangle drags have identical
inclusive results. Exact no-ops do not create history.

Tool mode and the selected tile are per-document workspace state. A rectangle's
captured preview is transient active-document state and is discarded safely on
a document switch. A completed Rectangle or Fill operation serializes once and
creates one undo entry. Escape cancels an active rectangle without mutation.
Freehand Paint and Erase now capture the pointer; Escape restores the
pre-stroke canonical snapshot, releases capture, and creates no history.
Release commits one complete changed stroke as before.

Mode changes and document switches cancel any active tile gesture safely.
Tool buttons remain focus proxies for the non-focusable canvas, and the
toolbar may wrap in compact layouts. Pointer-driven tile tools operate only on
the active layer and report the operation and changed-cell count in ordinary
inline status.

## Consequences

- Large contiguous floors, walls, masks, and erase regions no longer require
  manually tracing every cell.
- The runtime primitive is reusable by Zia and BASIC games and keeps the
  expensive bounded traversal in compiled cross-platform code.
- Rectangle preview and tile sampling never dirty a scene; only completed
  mutations enter canonical JSON and history.
- A focused runtime test must pin four-connectivity, boundaries, counts,
  invalid/no-op behavior, and large-region determinism.
- A focused Studio production-input probe must pin forward/reverse rectangles,
  Fill counts and exact undo, Pick isolation, visible captured preview,
  release-once behavior, Escape cancellation, and freehand rollback.

## Alternatives Considered

- **Implement flood fill in `scene_editor_2d.zia`.** Rejected because a valid
  scene layer can contain 1,048,576 cells, making a large interpreted queue an
  avoidable UI stall and memory-management burden.
- **Use rectangular fill only.** Rejected because authored terrain commonly
  has irregular enclosed regions; rectangles do not replace bucket-fill
  workflows.
- **Mutate the rectangle continuously while dragging.** Rejected because
  reverse drags and cancellation would repeatedly rewrite large regions and
  require snapshot restoration merely to render a preview.
- **Infer a tile from the top visible layer.** Rejected because editing is
  active-layer scoped; sampling a different layer would be surprising and
  could select an incompatible atlas frame.
