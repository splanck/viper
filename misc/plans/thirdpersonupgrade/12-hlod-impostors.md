# Plan 12 — Cell-Level HLOD Proxies + Automated Impostors

## 1. Objective & scope

Make distant world cells nearly free: a merged low-triangle **proxy mesh** per cell rendered while the cell is not resident, and automated **impostor** generation so the farthest ring degrades to textured quads. Per-node LOD/impostor machinery exists (`AddLOD`, `GenerateLODs`, `SetImpostor`, `BackendSupports("hlod")` reserved) but nothing operates at cell granularity, so an unloaded cell today simply vanishes.

**In scope:** (a) `proxy` manifest field + proxy-resident stream state; (b) `bakeCellProxy(i)` offline/editor bake producing a proxy `.vscn` sidecar; (c) `generateImpostors(distance)` automation; (d) draw integration + telemetry.
**Out of scope:** async plumbing (plan 11 lands first), GPU-generated impostors (CPU/RT capture only), nested HLOD hierarchies.

**Zero external dependencies — absolute.** Simplification and atlas packing reuse in-tree code.

## 2. Current state (verified anchors)

- **Node-level pieces exist:** `SceneNode.AddLOD/GenerateLODs` (quadric simplification via `Mesh3D.Simplify`, `rt_mesh_simplify.c`), `SetAutoLOD(screen-error)`, `SetImpostor(distance, pixels)` building an unlit textured quad proxy on the normal draw path (`rendering3d.md` §SceneNode LOD).
- **Stream residency:** cells are all-or-nothing — `game3d_world_stream_load_cell/unload_cell` (`rt_game3d_streaming.inc:1284-1305`); unloaded cells report manifest `bytes` only (`game3d.md` §WorldStream3D). Manifest cells already carry `name/path/center/radius/bytes` + metadata.
- **Capture path for impostors:** `RenderTarget3D` off-screen render + readback (`rt_rendertarget3d.c`, HDR/LDR CPU mirror), camera ortho support (`rt_camera3d_new_ortho`).
- **Atlas:** `TextureAtlas3D` exists (`rt_texatlas3d.c`) for packing impostor views.
- **Capability key:** `BackendSupports("hlod")` documented as reserved for runtime-authored LOD/impostor proxies (`rendering3d.md` §Visibility Controls).
- **VSCN save:** `rt_scene3d_vscn_save.c` serializes scene subtrees with interned assets — the proxy bake output format.

## 3. Design

### 3.1 Manifest + stream state

Cell entries gain optional `"proxy": "cells/town_00_proxy.vscn"` (+ `"proxyBytes"`). Stream states extend plan 11's machine: `Unloaded → ProxyResident → (Staging → …) → Resident`, where `ProxyResident` holds only the tiny proxy subtree. Radii: proxies load inside a new `proxyRadius` (default 4× load radius, `setProxyRadius`); full residency swaps proxy→full at the existing load radius (proxy unloads after the full cell commits — no gap frame). Proxies count toward `residentBytes` with their own `proxyResidentBytes` telemetry split.

### 3.2 Proxy bake (`bakeCellProxy`)

Editor/offline hook (runs on a loaded cell, main thread, not per-frame):

1. Load the cell subtree (blocking path), gather drawable meshes with world transforms.
2. Merge into one buffer (transform-baked), weld, then `Mesh3D.Simplify` to a triangle budget (`proxyTriangleBudget`, default 800/cell).
3. Material: bake per-face albedo into a small atlas (sample each source material's color/albedo map at face centroids — flat-shaded proxy, unlit-with-fog shading model so proxies match distance haze).
4. Serialize `{mesh, atlas material}` as a proxy `.vscn` via the existing saver; write next to the cell file; report bytes for the manifest field (author updates manifest, or `bakeCellProxy` patches a manifest copy).

### 3.3 Impostor automation

`generateImpostors(distance)`: for each cell with a proxy, render the proxy from 8 yaw angles (ortho, transparent background) into a `TextureAtlas3D` page via `RenderTarget3D`; install a camera-facing quad selecting the nearest-angle frame (extend the existing impostor quad path with an 8-frame variant — `SetImpostorFrames(distance, pixels, frames)`), used beyond `distance`. This gives three rings: full cell → proxy mesh → impostor quad.

### 3.4 Draw + budget integration

Proxy subtrees attach under the stream root like cells do; normal frustum/occlusion culling applies. `ProxyResident` cells are excluded from collider/nav spawning (render-only). Telemetry: `proxyResidentCount`, `proxyResidentBytes`, draws counted in existing `drawCount`.

## 4. Implementation steps

1. Manifest `proxy/proxyBytes` parse + typed inspection getters (`getCellProxy(i)` etc.) + tests.
2. `ProxyResident` state + radii + swap ordering (proxy unload after full commit) on top of plan 11's machine; unit tests on the state timeline.
3. `bakeCellProxy`: merge/weld/simplify + centroid-albedo atlas + VSCN emit; C unit test on a two-mesh fixture cell (triangle budget respected, atlas non-empty).
4. Unlit-with-fog proxy shading flag (tiny material bit; all four shader sources touch — batch with other shader plans).
5. Impostor automation: 8-view capture + atlas + multi-frame quad; visual probe.
6. Telemetry + docs (`game3d.md` §WorldStream3D HLOD section) + ADR.
7. Dense-fixture evidence: extend `g3d_openworld_slice_visibility_dense_probe` with proxy ring enabled; record draw/fill reduction.

## 5. Public API changes (runtime.def)

`WorldStream3D`: `setProxyRadius(f64)`, `bakeCellProxy(i64) -> i1`, `generateImpostors(f64) -> i64` (count), `getCellProxy(i64) -> str`, props `proxyResidentCount/proxyResidentBytes`.
`SceneNode`: `RT_METHOD("SetImpostorFrames","void(obj,f64,obj,i64)")` (multi-frame variant beside `SetImpostor`).
No new classes; ADR `00xx-worldstream-hlod.md`. Backends advertising the multi-frame impostor keep `BackendSupports("hlod")` truthful.

## 6. Tests

- **State ring:** Given proxyRadius 4× — When the center approaches a far cell — Then states pass Unloaded→ProxyResident→Resident with no frame where neither proxy nor full subtree is attached (fail-before: cells pop from nothing).
- **Bake:** fixture cell (2 meshes, 10k tris) bakes ≤ 800 tris, valid VSCN round-trip (load-back renders), atlas bytes reported.
- **Visual:** software golden of a proxy ring scene vs full-resident scene — silhouette/color roughly consistent (tolerance compare, not exact), fog applied to proxies.
- **Impostors:** beyond impostor distance, cell renders exactly 1 draw (quad); yaw rotation swaps frames (frame-index assert via draw metadata).
- **Budget/teleport:** quadrant-jump traversal with proxies: worst-frame cost below plan-11 bound; resident bytes split proxy/full correctly and return to baseline on unmount.
- **Determinism:** proxy-state timeline identical across worker counts and replays.

## 7. Verification gates

Full build + ctest; streaming lane + dense visibility probe with proxy metrics recorded; Metal + SW visual verification (waiver for GL/D3D11); `-L slow`; `-L graphics3d`; surface audits.

## 8. Risks & constraints

- **Proxy visual mismatch** is inherent (flat albedo); mitigate with fog-dominant distances (`proxyRadius` default keeps proxies far). Tolerance goldens, not exact.
- **Bake is authoring-time:** never run automatically during play; document the workflow (bake after cell edits, like navmesh bakes).
- **Manifest compatibility:** cells without `proxy` behave exactly as today (state skips ProxyResident) — the dense probe runs both ways.
- Impostor atlas memory: 8 frames × cell count at 128² ≈ small, but budget it under `proxyResidentBytes` and evict with the cell ring.
