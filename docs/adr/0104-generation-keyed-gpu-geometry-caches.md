# ADR 0104: Generation-keyed GPU geometry caches

## Status

Accepted

## Context

Canvas3D GPU backends cache immutable mesh uploads across frames. The cacheable draw-command
contract historically exposed only an object pointer and geometry revision. A freed mesh and a
later mesh allocated at the same address could therefore match a stale backend entry when their
revision and element counts also matched.

Mesh objects already receive a nonzero process-local allocation generation (`identity_serial`) for
temporal-history isolation. MorphTarget3D now receives the same kind of identity. These generations
remain stable for an object's lifetime and change when an allocator address is reused.

## Decision

The internal `vgfx3d_draw_cmd_t` contract carries `geometry_identity` alongside `geometry_key` and
`geometry_revision`. Canvas3D supplies the mesh allocation generation for cacheable submissions and
zero for transient geometry. GPU caches must include the generation in lookup and replacement
decisions; the pointer remains available for ownership/debugging and for backends migrating their
cache implementation incrementally.

The morph payload seam follows the same rule with `morph_identity`, `morph_key`, and
`morph_revision`. The identity accessor is backend-internal and is not part of the runtime C ABI.

This field is internal to the runtime renderer/backend seam and does not alter the public runtime C
ABI.

## Consequences

- Allocator address reuse cannot inherit stale OpenGL mesh buffers.
- Morph-target address reuse cannot inherit stale OpenGL texture-buffer payloads.
- Transient and generated geometry continues through streaming buffers.
- Other GPU backends may adopt the generation key without changing Canvas3D again.
- Cache tests must cover identical addresses/revisions paired with different generations.
