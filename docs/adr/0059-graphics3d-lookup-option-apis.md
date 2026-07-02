# ADR 0059: Graphics3D Lookup Option APIs

Date: 2026-07-02
Status: Accepted

## Context

Several preview Graphics3D and Game3D lookup APIs expose absence through `null`
or `-1` sentinels:

- `SceneGraph.Find()` and `SceneNode.Find()` return `null`.
- `SceneAsset.FindNode()` returns `null`.
- `World3D.FindNode()` and `World3D.FindEntity()` return `null`.
- `NavMesh3D.FindPath()` returns `null` when no path exists.
- `Skeleton3D.FindBone()` returns `-1`.

Those shapes are source-compatible with existing examples, but they are not the
modern runtime failure vocabulary. Absence is normal for these lookups and does
not require extra diagnostics, so `Option` is the clearest public contract.

## Decision

Add Option-returning lookup variants:

- `SceneGraph.FindOption(name) -> Option[SceneNode]`
- `SceneNode.FindOption(name) -> Option[SceneNode]`
- `SceneAsset.FindNodeOption(name) -> Option[SceneNode]`
- `World3D.FindNodeOption(name) -> Option[SceneNode]`
- `World3D.FindEntityOption(name) -> Option[Entity3D]`
- `NavMesh3D.FindPathOption(start, goal) -> Option[Path3D]`
- `Skeleton3D.FindBoneOption(name) -> Option[Integer]`

The existing sentinel APIs remain registered for compatibility. Runtime API
metadata marks those rows as legacy and points callers to the Option variants.

## Consequences

- New 3D code can use the same `Option` absence model as collections, text,
  XML, 2D scenes, GUI lookup helpers, and audio groups.
- Existing code that checks `null` or `-1` continues to work.
- API dumps and generated docs can recommend one consistent lookup style without
  hiding compatibility rows.
