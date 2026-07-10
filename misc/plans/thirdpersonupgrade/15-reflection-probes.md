# Plan 15 — Parallax-Corrected Local Reflection Probes

## 1. Objective & scope

Interiors currently reflect the sky: specular ambient comes only from the global skybox IBL (plus per-material SSR where visible). Add local reflection probes — captured cubemaps with parallax-corrected box projection, slot-registered on the canvas, blended by proximity — completing the reflection fallback chain **SSR → local probe → skybox IBL**.

**In scope:** (a) `Viper.Graphics3D.ReflectionProbe3D` capture + prefilter; (b) canvas probe slots + per-draw selection/blending; (c) parallax box projection in all four backends; (d) SSR fallback integration.
**Out of scope:** real-time per-frame probe re-capture (manual/scripted capture only; time-of-day recapture hooks in plan 16), sphere-projected probes (box first), probe baking into streamed cell sidecars (follow-up).

**Zero external dependencies — absolute.** Box-projection math from the published derivation; prefiltering reuses the IBL GGX chain.

## 2. Current state (verified anchors)

- **No local probes exist:** the only "reflection probe" mention is a render-target doc comment (`rt_scene3d_api.inc:283`); specular ambient = skybox IBL prefiltered chain (`rendering3d.md` §Lighting Helpers) or legacy `SetEnvMap` per material.
- **Capture path exists:** `RenderTarget3D` off-screen rendering (`rt_rendertarget3d.c`, HDR16F support `:290`), `CubeMap3D` (`rt_cubemap3d.c`), and Water3D already captures planar reflections per frame — proving scene re-render capture is supported.
- **Prefilter machinery:** IBL builds a GGX-prefiltered roughness mip chain + SH-9 diffuse from a cubemap, computed once and reused (`rendering3d.md` IBL paragraph) — probes call the same builder on their captured cubemap.
- **Slot pattern:** `Canvas3D.SetLight(index, light)` retained slots with trap-on-invalid (`rendering3d.md` §Lighting Helpers) — probes mirror it (`SetReflectionProbe(index, probe)`), fixed budget (8 slots).
- **Per-draw constants:** the per-draw upload path already carries ambient/IBL parameters; probe selection adds a probe index + box parameters per draw.
- **Material opt-out:** materials with explicit `SetEnvMap` keep legacy behavior (documented IBL rule) — same precedence applies to probes.

## 3. Design

### 3.1 Probe object

`ReflectionProbe3D.New(position, boxMin, boxMax)` — capture point + parallax proxy volume (also the influence volume; `SetInfluenceScale(s)` grows influence past the proxy for blending). `Capture(canvas, scene, resolution)`: renders 6 faces (90° perspective) from `position` into a cubemap via `RenderTarget3D` (sky + static geometry; capture excludes dynamic-flagged nodes by reusing plan 14's `SceneNode.Static` hint — falls back to everything when unflagged), then runs the IBL prefilter (GGX mips + SH-9). `CaptureDirty` flag for scripted re-capture (plan 16 sets it on time-of-day changes).

### 3.2 Canvas slots and selection

- `Canvas3D.SetReflectionProbe(index, probe)` (0..7), `ClearReflectionProbes()`; software + GPU backends all consume them (SW is the baseline implementation).
- Per opaque/transparent PBR draw: pick the two highest-weight probes whose influence boxes contain the draw's bounds center; weight = distance-to-face falloff. Upload probe indices + weights + box min/max/position per draw. Blend probe specular by weight; remaining weight falls to skybox IBL.
- **Parallax correction (shader, ×4 sources):** intersect the reflection ray with the probe box in world space; sample the prefiltered cubemap in the corrected direction (`dir = hitPoint − probePos`); standard box-projection derivation. Diffuse ambient keeps plan-14 probes/lightmap precedence — reflection probes contribute **specular only** (their SH is used only when no light-probe grid is present).

### 3.3 SSR integration

Where the SSR effect resolves a valid screen-space hit, SSR wins; SSR miss (ray leaves screen/occluded) falls back to the blended probe term instead of skybox — the SSR shader gains the same probe constants (batch this shader edit with the probe draw change).

## 4. Implementation steps

1. Probe class: capture (6-face RT render) + prefilter reuse + retention; C unit test (capture a red-walled box room; cubemap face means are red-dominant).
2. Canvas slots + per-draw selection/weights (CPU side) + SW-raster parallax sampling — SW golden: chrome sphere in a box room reflects walls, not sky.
3. Metal shader port (local verify); GLSL/HLSL ports (waiver).
4. SSR fallback wiring.
5. `Game3D` sugar: `World3D.addReflectionProbe(pos, min, max)` auto-registers to a free slot and captures at next frame end.
6. runtime.def + audits + ADR + docs (`rendering3d.md` §Reflection Probes).
7. Golden fixtures: `baked_room` (plan 14 fixture) gains a probe; committed SW baseline.

## 5. Public API changes (runtime.def)

```
RT_FUNC(G3dReflProbeNew, rt_reflectionprobe3d_new, "Viper.Graphics3D.ReflectionProbe3D.New",
        "obj(obj<Viper.Math.Vec3>,obj<Viper.Math.Vec3>,obj<Viper.Math.Vec3>)")
RT_CLASS_BEGIN("Viper.Graphics3D.ReflectionProbe3D", G3dReflectionProbe3D, "obj", G3dReflProbeNew)
    RT_PROP("Position","obj<Viper.Math.Vec3>",…) RT_PROP("InfluenceScale","f64",…)
    RT_PROP("Resolution","i64",…) RT_PROP("CaptureDirty","i1",…)
    RT_METHOD("Capture","i1(obj,obj,obj)",…)    /* canvas, scene */
RT_CLASS_END()
```

Plus `Canvas3D.SetReflectionProbe(i64, obj)/ClearReflectionProbes()`, `World3D.addReflectionProbe(...)`. Leaf `ReflectionProbe3D` unique. New file `render/rt_reflectionprobe3d.c` → source-health bump; ADR `00xx-reflection-probes.md`.

## 6. Tests

- **Capture (C unit):** box room with colored walls ⇒ per-face dominant colors match wall layout (fail-before: no API).
- **Parallax:** SW golden — mirror sphere off-center in the room reflects the *near* wall larger than the far wall (box projection working); naive infinite-cubemap sampling comparison image documents the difference.
- **Blending:** draw centered between two probes samples both (weight assert via debug counters); outside all probes ⇒ skybox term (capture compare with probes cleared).
- **SSR fallback:** SSR-miss region (object reflecting off-screen content) shows probe color, not sky (pixel assert in a constructed scene).
- **Legacy precedence:** material with `SetEnvMap` renders unchanged (bit-exact SW).
- **Budget:** 9th probe registration traps with a clear diagnostic (slot pattern).

## 7. Verification gates

Full build + ctest; SW goldens (new fixtures + existing bit-exact when feature unused); Metal local visual verify, GL/D3D11 waiver; `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **Capture cost:** 6 scene renders + prefilter per capture — explicitly manual/scripted; docs warn against per-frame captures (Water3D remains the planar real-time path).
- **Seams between probes:** two-probe blend hides most; document that authors place probes per room with overlapping influence.
- **Per-draw constant growth:** 2 probes × (pos + boxmin + boxmax + weight) ≈ 26 floats — fits the per-draw block; verify against the D3D11 cbuffer layout before landing.
- Specular-only contract keeps diffuse ownership with plan 14 — prevents double-counting bounce light.
