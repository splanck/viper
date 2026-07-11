# ADR 0085: GPU Skinning Capability, Routing Override, and Telemetry

Date: 2026-07-10

## Status

Accepted

## Context

GPU vertex-shader skinning was already implemented across the production
backends — `vgfx3d_backend_prefers_gpu_skinning` routes palette-fitting
skinned draws to shader skinning (MSL/HLSL/GLSL consume `cmd->bone_palette`,
including per-instance palette pages packed into the 256-slot limit) while
software CPU-skins as the reference. What was missing was the *contract*
around it: no capability string (the plan reserved `"gpu-skinning"`), no way
to force the CPU path for bisection, and no observability to prove which path
a frame actually took — the exact gap that makes GPU/CPU visual differences
hard to attribute.

## Decision

- **`BackendSupports("gpu-skinning")`** reports true on metal/opengl/d3d11
  and false on software. It also reports false while the force-CPU override
  is active, so capability checks and actual routing can never disagree.
- **`Canvas3D.SetForceCpuSkinning(enabled)`** routes every skinned draw
  (single and instanced) through the CPU path for bisection and
  determinism debugging; the capability report follows it.
- **Telemetry:** `GpuSkinnedDrawCount` (lifetime skinned draws routed to
  shader skinning) and `SkinningUploadBytes` (lifetime palette bytes handed
  to the backend, doubled when the previous-frame palette rides along for
  motion vectors). Palette uploads are deliberately not folded into texture
  upload counters.
- **Routing boundaries stay as built:** extra-influence meshes, per-submesh
  bone-remapped instanced draws, and CPU-morphed vertex streams remain on the
  CPU path. Morph-then-skin ordering is preserved by construction — morphs
  are applied before the routing decision, and GPU morph application stays
  out of scope per the plan.

## Consequences

- The software backend remains the bit-exact skinning baseline; GPU parity is
  tolerance-based, exercised by the openworld GPU smoke lane which now
  asserts a capable backend routes the skinned agent through the palette path
  (`GpuSkinnedDrawCount > 0`, upload bytes accounted) and that the force-CPU
  override flips both the capability report and the routing live.
- The software probe (`test.zia`) pins the negative: capability false and
  zero GPU-routed skinned draws.
- GLSL/HLSL shader paths pre-exist; behavior on Linux/Windows is verified by
  hand per the standard cross-platform waiver for shader work.
