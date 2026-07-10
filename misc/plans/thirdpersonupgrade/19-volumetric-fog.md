# Plan 19 — Analytic Height Fog + Sun Inscattering

## 1. Objective & scope

Replace "distance fog only" with atmosphere: exponential **height fog** (dense valleys, clear peaks) with a directional **sun inscattering** term (silver lining looking toward the sun), evaluated analytically per pixel in all four backends. This is the highest mood-per-cost rendering feature for outdoor adventure scenes and the natural output target for plan 16's time-of-day fog curves.

**In scope:** (a) `Canvas3D.SetHeightFog(density, height, falloff, r, g, b)`; (b) analytic transmittance along the view ray; (c) sun inscattering tint; (d) application to scene draws, skybox blend, and water/terrain/vegetation consistency.
**Out of scope:** froxel/volumetric light shafts (the `sun_shafts` post-FX remains the shaft look), local fog volumes, fog shadows.

**Zero external dependencies — absolute.** Standard exponential-height-fog closed form.

## 2. Current state (verified anchors)

- **Distance fog exists:** `EnvHandle.withFog(near, far)` and canvas fog state (`game3d.md` §Environment); shading applies a distance-based blend to a fog color — per-backend implementation in the four shader sources.
- **No height component** (grep `height_fog|heightfog` — none).
- **Sun shafts are post-FX:** `rt_postfx3d_add_sun_shafts` (`rt_postfx3d.h`) — screen-space radial shafts; unrelated to atmospheric density.
- **Per-frame constants:** scene-level uniforms (camera, lights, fog) upload once per frame (per-frame constant block established by the 3d_overhaul plan 04 work) — height-fog params ride the same block.
- **Sun direction:** primary directional light from the canvas light slots (shadow-caster selection logic already identifies "the sun", `rendering3d.md` §Lighting Helpers).
- **Time-of-day driver:** plan 16 fog curves call this API when present (soft dependency, either order works).

## 3. Design

### 3.1 Model

Exponential height density `ρ(y) = density × exp(−falloff × (y − height))`, transmittance along camera→fragment integrates in closed form:

```
fogAmount = 1 − exp(−density/falloff × exp(−falloff×(camY−height)) × (1 − exp(−falloff×Δy)) / (falloff×Δy_unit) × dist)
```

(the standard closed-form with the Δy→0 series guard). Final color = `lerp(sceneColor, fogColor', fogAmount)` where `fogColor' = lerp(fogColor, sunTint, sunAmount × pow(max(dot(viewDir, sunDir),0), sunPower))` — inscattering brightens fog toward the sun. Params: `density` (≥0, 0 disables), `height` (world Y of the fog base), `falloff` (>0), fog RGB; sun term: `SetHeightFogSun(r, g, b, power, amount)` defaulting to sun-light color, power 8, amount 0.6.

### 3.2 Application points (×4 shader sources)

- **Scene draws:** replace/augment the existing distance-fog blend at the end of the fragment path — when height fog is enabled it supersedes distance fog (one fog model at a time; `withFog` distance mode remains for scenes that never opt in). Uses fragment world Y (already available for lighting) and view distance.
- **Skybox:** blend the horizon band with `fogAmount` evaluated at a far-distance proxy so terrain meets sky without a seam (skybox fragment gets the same closed form with clamped Δy).
- **Transparents/particles/water/vegetation:** same fog term in their shading paths (the existing distance-fog application points enumerate them; height fog swaps in at each).
- **Floating origin:** `height` is world-space — the camera-relative upload path must add `worldOrigin.y` when rebased (fog params adjusted CPU-side per frame; no shader cost).

### 3.3 API surface

Canvas3D: `SetHeightFog(density, height, falloff, r, g, b)`, `ClearHeightFog()`, `SetHeightFogSun(...)`, getter props for tooling. Game3D: `EnvHandle.withHeightFog(density, height, falloff)` preset chain + plan-16 fog-curve output switches to height fog when enabled.

## 4. Implementation steps

1. Params in the per-frame constant block + SW-raster closed-form implementation (scene draws + skybox); SW goldens (valley scene: dense low band, clear peaks; sun-side brightening).
2. Series-guard numeric edge cases (camera inside/above/below fog, horizontal rays) — unit-test the closed form as pure math against numeric integration (1e-3 agreement).
3. Metal port + local visual verify; GLSL/HLSL ports (waiver) — batch with plans 14/15/16 shader work.
4. Transparent/water/vegetation/particle application points.
5. Floating-origin Y compensation + rebase test.
6. `EnvHandle.withHeightFog` + plan-16 curve hookup.
7. runtime.def + audits + docs (`rendering3d.md` fog section rewrite) + ADR note (canvas API addition).

## 5. Public API changes (runtime.def)

`Viper.Graphics3D.Canvas3D` additions:

```
RT_METHOD("SetHeightFog","void(obj,f64,f64,f64,f64,f64,f64)",…)
RT_METHOD("ClearHeightFog","void(obj)",…)
RT_METHOD("SetHeightFogSun","void(obj,f64,f64,f64,f64,f64)",…)
RT_PROP("HeightFogEnabled","i1",get)
```

Plus `Viper.Game3D.EnvHandle.withHeightFog(f64,f64,f64)` (fluent). No new classes/files. ADR `00xx-height-fog.md` (or fold into plan 16's ADR if landed together).

## 6. Tests

- **Closed form (C unit):** analytic vs 1024-step numeric integration across camera-above/inside/below-fog and near-horizontal rays — ≤1e-3 relative error; Δy→0 guard exact (fail-before: no API).
- **SW golden:** valley fixture — low fragments fog-dominant, peaks clear (row-band luminance asserts) plus committed baseline; sun-facing half brighter than anti-sun half at equal distance.
- **Skybox seam:** horizon row continuity between terrain-covered and sky pixels (adjacent-pixel delta bound).
- **Supersede rule:** enabling height fog with distance fog set uses height fog; `ClearHeightFog` restores distance fog byte-identically (SW).
- **Rebase:** 50 km floating-origin rebase renders identically to the near-origin scene (existing rebase-parity harness extended with fog on).
- **Compat:** fog-disabled scenes bit-identical SW.

## 7. Verification gates

Full build + ctest; SW goldens + rebase parity; Metal verify, GL/D3D11 waiver; `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **Numeric stability** is the whole risk: the closed form has two exponential terms that cancel badly at small `falloff` — the series guard and the pure-math unit test are non-negotiable first steps.
- **One-fog-model rule** avoids double-fogging; document loudly since `Environment` presets set distance fog today.
- **Four-source consistency:** scalar-identical implementations; goldens compare SW↔Metal within tolerance.
- Perf: closed form is a handful of ALU ops per fragment — no measurable cost expected; confirm via `FrameGpuTimeUs` on the perf probe before/after.
