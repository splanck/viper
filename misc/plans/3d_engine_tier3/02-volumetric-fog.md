# Plan 02 — Froxel Volumetric Fog

Light-scattering fog computed on the existing 16×9×24 clustered-lighting grid
(`rt_canvas3d_clusters.c`): inject per-froxel in-scattering from the lights
already binned there, integrate along depth, and composite. Gives god-rays and
light-shaft looks beyond the current screen-space sun shafts, cheap enough for
Balanced quality.

## Public API

- `Canvas3D.SetVolumetricFog(density: Float, anisotropy: Float, intensity: Float)`
  and `Canvas3D.ClearVolumetricFog()`; density 0 disables. Quality profiles:
  off at Fast, on at Balanced+ when a height/distance fog is active.
- Def rows in `rendering.def` + canvas3d stubs + manifest bump (stub-parity
  audit enforces the stub).

## Design

1. **Froxel buffer**: reuse the cluster grid dims (16×9×24, exponential z
   slicing already computed for light binning). Two additional per-frame GPU
   buffers: `froxel_scatter` (rgb in-scatter + a extinction, fp16 where the
   backend supports it, else fp32) and the integrated `froxel_accum`.
2. **Injection pass** (per froxel): ambient + per-cluster lights with the
   standard attenuation, phase = Henyey-Greenstein with `anisotropy` toward
   the view ray; extinction from `density` × the existing height-fog falloff
   so authored fog settings shape the volume.
3. **Integration pass**: front-to-back scan over the 24 z-slices
   (transmittance product), writing accumulated scatter+transmittance per
   froxel — 16×9 threads, trivially parallel.
4. **Composite**: in the scene shader (or postfx when a chain is active),
   trilinear-sample `froxel_accum` at the fragment's froxel coordinate and
   blend: `color = color * transmittance + scatter`. This REPLACES the
   analytic fog term when volumetric fog is enabled (fall back to the current
   analytic path otherwise — and always on the software backend first
   iteration).
5. **Software backend**: CPU version of inject+integrate on the same grid
   (it already walks clusters for lighting); composite in the raster loop.
   SW is the golden reference — land it in the same change as Metal, not
   later.

## Backend mapping

- Metal: two compute kernels + one shared threadgroup integration; buffers in
  `mtl_canvas3d` alongside the cluster light buffers.
- D3D11: CS 5.0 compute shaders, structured buffers (upload path mirrors the
  cluster-light SB in `vgfx3d_backend_d3d11_draw.inc`).
- OpenGL: needs GL 4.3 compute; below that, run inject/integrate on CPU and
  upload `froxel_accum` as a 3D texture (16×9×24 RGBA16F = 27 KB — trivial).
  This CPU fallback is also the correctness reference for the kernels.

## Tests / acceptance

- Unit: froxel index math (exp slicing round-trip), transmittance
  monotonicity, HG phase normalization (`test_rt_canvas3d.cpp`).
- Conformance: `"volfog"` scene mode — one bright point light in fog looking
  down a corridor; SW golden vs Metal within tolerance.
- Perf gate: Balanced 1080p on the dev Mac ≤ 0.4 ms GPU for inject+integrate
  (`Canvas3D.PassCpuMs`/GPU timer counters added in the profiler work).
