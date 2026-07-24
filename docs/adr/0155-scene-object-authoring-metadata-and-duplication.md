---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0155: Make Scene Object Metadata and Duplication Authorable

## Status

Accepted (2026-07-23)

## Context

`Zanna.Game2D.SceneDocument` exposes object creation, removal, position
mutation, ordering, and typed custom properties, but its reserved `type` and
`id` fields are read-only after creation. Zanna Studio therefore presents an
object inspector whose type and ID controls cannot truthfully apply their
values. Reconstructing an object in Studio would either lose typed properties
or require editor-side JSON surgery outside the canonical `SceneDocument`
model.

Faithful duplication has the same problem. `AddObject` can reproduce the four
reserved fields but cannot clone the source object's typed property map. The
public model also exposes property keys and typed getters without an exact kind
query, and it can read but not create the supported null scalar kind. That makes
a truthful typed property inspector impractical. The missing operations are
public runtime C ABI and registry changes, so ADR 0006 requires an explicit
decision before implementation.

## Decision

`Zanna.Game2D.SceneDocument` gains four additive instance methods:

```text
SetObjectMetadata(index: Integer, type: String, id: String) -> Void
DuplicateObject(index: Integer, id: String) -> Integer
ObjectPropertyKind(index: Integer, key: String) -> String
ObjectSetNull(index: Integer, key: String) -> Void
```

Their C ABI entry points are:

```c
void rt_game_scene_set_object_metadata(
    void *scene, int64_t index, rt_string type, rt_string id);
int64_t rt_game_scene_duplicate_object(
    void *scene, int64_t index, rt_string id);
rt_string rt_game_scene_object_property_kind(
    void *scene, int64_t index, rt_string key);
void rt_game_scene_object_set_null(
    void *scene, int64_t index, rt_string key);
```

`SetObjectMetadata` updates `type` and `id` together so an inspector Apply
action cannot publish a half-updated reserved record. An invalid object index
is a no-op, matching `SetObjectPosition`.

`DuplicateObject` deep-copies the source object's reserved fields and typed
property map, replaces the duplicate's ID with the supplied value, inserts it
immediately after the source, and returns the new index. It returns `-1` for an
invalid source or when the existing object-count limit is reached. Hitting the
limit records the same `scene.edit.rejected` diagnostic as `AddObject`.

`ObjectPropertyKind` returns one of the stable lowercase tokens `null`, `bool`,
`int`, `float`, or `string`. It returns an empty string when the object index or
property key does not exist. `ObjectSetNull` creates or replaces a property with
the null scalar kind and follows the same object-index and property-key bounds
as the other typed setters. Together these methods let authoring tools present
and create every scalar kind without guessing from formatted compatibility
values.

The runtime does not impose ID uniqueness because existing scene documents and
`AddObject` permit duplicate or empty IDs. Authoring tools are responsible for
offering a useful unique default and may validate stricter product rules before
calling the runtime.

## Consequences

- Studio can truthfully edit every reserved object field without replacing the
  canonical scene model.
- Duplicate preserves Boolean, integer, floating-point, string, and null
  property kinds instead of flattening them through compatibility strings.
- Property inspectors can identify and author every supported scalar kind
  without parsing canonical JSON or relying on default-value probes.
- Existing IL modules and scene files remain compatible; the surface is
  additive and the canonical JSON format does not change.
- Runtime registry, generated API documentation, authored SceneDocument
  documentation, and native/Studio tests must cover both operations.

## Alternatives Considered

- **Separate `SetObjectType` and `SetObjectId` methods.** Rejected because the
  editor applies both fields as one user transaction and should not expose a
  partially updated intermediate state.
- **Remove and recreate objects in Studio.** Rejected because the public API
  cannot discover a property's scalar kind reliably enough to reproduce every
  typed value.
- **Patch serialized JSON in Studio and reload it.** Rejected because it creates
  a second scene mutation implementation, weakens validation consistency, and
  makes rollback more fragile.
