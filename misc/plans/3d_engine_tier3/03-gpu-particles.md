# Plan 03 — GPU Particle Simulation

Move Particles3D simulate+sort onto the GPU for 100K+ particle budgets while
keeping the CPU path (current `rt_particles3d.c` Euler integrator) as both the
fallback and the determinism reference. Emission stays CPU-side (gameplay
reads emitter state); simulation, sorting, and billboard expansion move.

## Scope

- Opt-in per emitter: `Particles3D.SetGpuSimulation(enabled: Boolean)` — def
  row in `extras.def`, world stubs, manifest bump. Default OFF; VM==native
  determinism contract only applies to the CPU path (document that in
  `docs/viperlib/graphics3d/particles.md` alongside the existing `Seed` note).
- Capability-gated: new backend bit `gpu_particles` beside `gpu_skinning` in
  `vgfx3d_backend_t`; software backend never sets it (CPU path is its
  implementation).

## Design

1. **Particle state buffer**: SoA GPU buffer (pos.xyz+age, vel.xyz+life,
   seed+flags) sized to emitter capacity; double-buffered (read prev, write
   curr) so simulate is one dispatch with no sync hazards.
2. **Emission**: CPU packs newly spawned particles (the existing spawn logic,
   including the per-emitter deterministic `Seed`) into a small upload span
   appended after the live range; a compact pass folds them in.
3. **Simulate kernel**: Euler + gravity + drag + the current curl-noise
   turbulence (port the CPU noise exactly — same constants, so a single-frame
   CPU-vs-GPU diff is a meaningful test). Dead particles swap-compact via
   atomic tail counter.
4. **Sort**: view-depth bitonic sort over live range (Metal/D3D11 compute;
   GL 4.3 compute or skip sort below 4.3 — additive-blend emitters don't
   need it, alpha emitters fall back to CPU sim on old GL).
5. **Draw**: existing billboard path reads the GPU buffer directly via a
   per-backend vertex pull (no CPU readback). Trail/ribbon emitters stay CPU
   (they already emit multi-point control geometry per the trail fix).
6. **Readback-free gameplay queries**: `GetAliveCount` returns the CPU-side
   estimate (spawned − expired-by-age); document the divergence from exact
   GPU liveness.

## Backend notes

- Metal: MTLComputePipelineState pair (simulate, sort step); shared event or
  fence between simulate and draw encoder — reuse the pattern from the bloom
  mip chain (encoder ordering, no waits).
- D3D11: CS + UAV on the state buffer; draw binds it as SRV (flip
  bind flags per pass, same as the SSR depth SRV/DSV dance).
- OpenGL: GL 4.3 compute + SSBO; below 4.3 the capability bit stays 0.

## Tests / acceptance

- Unit: CPU-vs-GPU one-step parity on a fixed seed within 1e-5 (Metal, via a
  4-particle readback in `test_rt_canvas3d_gpu_paths.cpp`-style harness with
  a real device — gate on backend availability).
- Conformance: `"particles"` scene mode with a deterministic additive
  fountain; SW golden vs Metal tolerance loosened for sort-order blending
  (additive blending is order-independent — keep the conformance emitter
  additive so tolerance stays tight).
- Soak: extend `soak_scene.zia` churn with a GPU emitter; degradation
  counters + RSS must stay flat (buffer growth must be capacity-bounded).
- Perf gate: 100K particles simulate+sort ≤ 1.0 ms GPU on the dev Mac.
