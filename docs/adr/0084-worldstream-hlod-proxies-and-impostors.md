# ADR 0084: Cell-Level HLOD Proxies and Automated Impostors

Date: 2026-07-10

## Status

Accepted

## Context

WorldStream3D cells were all-or-nothing: outside the load radius a cell simply
vanished, so open worlds showed empty horizons where distant towns should sit.
Node-level LOD machinery existed (`AddLOD`, quadric `Mesh3D.Simplify`,
single-frame `SetImpostor`), but nothing operated at cell granularity, and the
`BackendSupports("hlod")` capability key was documented as reserved for
exactly this feature.

## Decision

Cells gain an optional `proxy`/`proxyBytes` manifest pair and a third
residency ring:

- **ProxyResident ring:** inside `SetProxyRadius` (default 4x the load
  radius, hysteresis width borrowed from the load/unload gap) a proxy-bearing
  cell attaches only its merged low-poly proxy subtree — render-only, no
  colliders or nav sources. Swaps are gap-free in both travel directions: the
  outgoing representation stays attached until the incoming one commits (the
  receding hold keeps the full cell resident and counted until its proxy
  lands). Proxy payloads ride the plan-11 worker staging pipeline as a third
  job kind sharing the cell generation counter. A proxy that fails to
  load/parse disables itself for the session (recoverable, counted in
  `StreamStagingErrors`) rather than blocking full residency.
- **`BakeCellProxy(i)` (authoring-time):** merges the resident cell's
  drawable meshes transform-baked into cell-local space (new internal raw
  mesh readback accessors), simplifies via the existing quadric simplifier to
  an 800-triangle budget, bakes per-source-material diffuse colors into a
  4px-block atlas with block-center UVs, and saves a single-node `.vscn`
  proxy next to the cell. The session's cell record picks the path up
  immediately; manifests adopt it via the `proxy` field. Proxies are unlit
  for now — the fog-aware unlit variant lands with the LIT-phase shader
  batch, as planned.
- **`GenerateImpostors(distance)`:** for each proxy-resident cell, renders
  the proxy mesh from 8 yaw angles through an off-screen `RenderTarget3D`
  into a horizontal strip and installs it with the new
  `SceneNode.SetImpostorFrames(distance, strip, frames)` — per-frame
  UV-windowed quads, draw-path frame selection by camera bearing
  (`GetImpostorFrameIndex` exposes the choice for tests/tools). Farthest ring
  degrades to one textured quad per cell.

## Consequences

- Three rings — full cell → proxy mesh → impostor quad — with proxy bytes
  split out (`ProxyResidentCount`/`ProxyResidentBytes`) and still included in
  `ResidentBytes`, so budget semantics stay one number.
- Cells without `proxy` behave exactly as before; the state machine simply
  skips ProxyResident.
- Proxy visual mismatch is inherent (flat albedo, capture-background
  impostors); the design target is fog-dominant distances, documented.
- Bake is never automatic: it is an editor/authoring hook like navmesh bakes.
- Tests: `test_rt_game3d_hlod` (bake budget + round-trip, gap-free ring in
  blocking and async modes, telemetry split, multi-frame impostor install,
  impostor generation); the dense-visibility probe bakes and holds a proxy
  ring end-to-end in Zia.
