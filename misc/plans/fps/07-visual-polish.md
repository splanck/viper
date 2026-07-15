# 07 — Engine: Visual Polish — Auto-Exposure, LUT Grading, Height Fog & Sun Shafts, Lens Flares, Particle Stretch/Trails, AA Overlay Text

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped all six features. **E37 auto-exposure**: `PostFX3D.AddAutoExposure(minEv,maxEv,
> adaptSpeed)` — geometric-mean luminance (bounded 4096-pixel sampling) → target EV centering
> middle gray, clamped, smoothed at a fixed deterministic 1/60 step with 2.5× faster downward
> adaptation; state persists on the chain. **E38 LUT grading**: `AddColorLUT(pixels, blend)`
> (256×16 strip, 16³ trilinear, chain retains the Pixels) + `MakeIdentityLUT()`; the
> screenshot-grade-crop authoring workflow is documented in the API doc comment. **E39**:
> `Canvas3D.SetHeightFog(base, falloff, density, blend)` — exponential height term combined
> with distance fog via combined transmittance, implemented in ALL FOUR shader sources
> (MSL/HLSL/GLSL/SW; PerScene additions position-mirrored per the BUG-E7 alignment contract;
> `ClearFog` clears both) — plus `PostFX3D.AddSunShafts(intensity, decay, samples)`: CPU
> radial sky-mask accumulation toward the primary directional light's projected position
> (sun resolved canvas-side into the post-FX scene inputs; auto-fades off-screen/behind).
> **E40 lens flares**: new `rt_lensflare3d.c` — `LensFlare3D.New(light)` +
> `AddElement(axisOffset, size, color, rotation)` (≤16 pre-tinted procedural radial-disc
> ghosts) + `Canvas3D.DrawLensFlare` in overlay space; occlusion = 3×3 CPU-depth probe
> (software zbuf / RT depth; GPU windows draw unoccluded — documented until GPU readback
> occlusion lands; the 120 ms fade is likewise deferred for determinism — visibility scales
> ghost size instantly). **E41**: `Particles3D.SetStretch(k)` (velocity-projected quad axes,
> winding-correct, length ∝ 1+k·|v| capped 64×) and `SetTrail(lifetimeSec, segments≤16)` —
> per-particle position rings in parallel arrays kept in sync with the pool's swap-remove
> kills via `particles3d_swap_kill`; ribbons emit as tapered, alpha-fading camera-facing
> quads appended to the emitter's single draw. **E42**: `Canvas3D.DrawText2DAA(x,y,text,
> color,scale)` — 8×8 font supersampled 4×4 per output pixel (coverage→alpha) into a
> frame-lifetime temp Pixels, one image blit; `MeasureText2DAA`; `DrawImage2DNineSlice`
> (corners unscaled, edges axis-stretched, insets sanitized). GPU postfx pipelines skip the
> new CPU-side chain effects gracefully (they key on known type values); native GPU
> implementations of E37/E38/shafts are the recorded follow-up. Also fixed in passing:
> `log2f` missing from the native linker's dynamic-symbol allow-list (exp2f was present).
> Coverage: `tests/runtime/test_canvas3d_visual_polish.zia` (software-forced ctest) —
> auto-exposure ≥2× dark-frame lift, identity-LUT no-op + inverting-LUT inversion, height
> fog pooling + ClearFog restore, stretch/trail coverage growth over a plain-billboard
> baseline, AA text visibility + monotonic measure, nine-slice corner/center classes, and
> lens-flare brightness delta. `-L graphics3d` 100/100; completeness + source-health green
> (contract files 806). Docs: covered by the runtime API doc comments; rendering3d.md
> "Cinematic look" section is folded into the postfx/fog rewrites from docs 03/06.
## 0. TL;DR

Six self-contained upgrades, all four-backend (GPU shader + CPU reference each, keeping the
06-parity contract): eye-adaptation **auto-exposure**; **3D LUT color grading** authored as
plain PNG strips; **height fog** + **screen-space sun shafts**; occlusion-aware **lens
flares**; **velocity-stretched particles and ribbon trails**; and **anti-aliased overlay text
at arbitrary scale** plus 9-slice image draws for HUD chrome.

## 1. New API surface (runtime.def)

```text
Viper.Graphics3D.PostFX3D.AddAutoExposure(f64,f64,f64)   void(obj,f64,f64,f64)
    — (minEv, maxEv, adaptSpeed): log-luminance histogram → target exposure, smoothed.
      Composes with AddTonemap (auto term multiplies the fixed exposure).
Viper.Graphics3D.PostFX3D.AddColorLut(obj,f64)           void(obj,obj,f64)
    — (lutPixels, blend): 16³ LUT baked as a 256×16 PNG strip (Pixels), trilinear sampled,
      blend 0..1 with ungraded. Identity strip generator: PostFX3D.MakeIdentityLut() obj().
Viper.Graphics3D.Canvas3D.SetHeightFog(f64,f64,f64,i64,f64) void(obj,f64,f64,f64,i64,f64)
    — (baseHeight, falloff, density, colorRGBA, blendWithDistanceFog 0..1). Exponential-height
      term multiplies the existing linear distance fog; ClearFog clears both.
Viper.Graphics3D.PostFX3D.AddSunShafts(f64,f64,i64)      void(obj,f64,f64,i64)
    — (intensity, decay, samples≤48): screen-space radial blur from the primary directional
      light's screen position, occluded by depth (bright-pass on sky/emissive), additive.
      Auto-fades when the sun is off-screen or behind camera.
Viper.Graphics3D.LensFlare3D.New(obj)                    obj(obj)     — bound to a Light3D
Viper.Graphics3D.LensFlare3D.AddElement(f64,f64,i64,f64) void(obj,f64,f64,i64,f64)
    — (axisOffset −1..2, size, colorRGBA, rotation): classic ghost chain along the
      light→center axis. Occlusion: depth-tested at the light's screen pos (raycast fallback
      on SW) with 120 ms fade. Draw via Canvas3D.DrawLensFlare(obj) inside overlay phase.
Viper.Graphics3D.Particles3D.SetStretch(f64)             void(obj,f64)
    — velocity-aligned billboard stretching (0 = camera-facing quads, k = length ∝ |v|·k).
Viper.Graphics3D.Particles3D.SetTrail(f64,f64,i64)       void(obj,f64,f64,i64)
    — (lifetimeSec, widthStart→widthEnd implicit via SetSize, maxSegments≤16): per-particle
      ribbon of last-N positions, camera-facing strip, inherits color/alpha ramp.
Viper.Graphics3D.Canvas3D.DrawText2DAA(i64,i64,str,i64,f64) void(obj,i64,i64,str,i64,f64)
    — arbitrary-scale text: glyphs rasterized from the built-in font at 4× and box-filtered
      (cached per (glyph,scale-bucket)), sub-pixel positioned. MeasureText2DAA(str,f64) i64.
Viper.Graphics3D.Canvas3D.DrawImage2DNineSlice(i64,i64,i64,i64,obj,i64,i64,i64,i64) void(...)
    — (x,y,w,h, pixels, insetL,T,R,B): HUD panels/buttons from one texture.
```

## 2. Implementation notes (per feature)

- **E37 Auto-exposure**: 64-bin log-luminance histogram of the tonemap input (GPU: existing
  post pipeline adds a reduction pass; CPU: direct). Target = exposure that centers the 50th
  percentile at middle gray, clamped to [minEv,maxEv], exponentially smoothed by `adaptSpeed`
  (separate up/down rates ×1 / ×0.4 for cinematic feel). Deterministic: histogram from the
  deterministic frame, smoothing uses fixed dt from the canvas clock.
- **E38 LUT**: strip layout matches industry convention (16 tiles of 16×16 horizontally);
  authored by screenshotting the identity strip composited over a reference frame and grading
  it in any editor — document this workflow in `docs/viperlib` (it is how the game's per-level
  grades get authored, 27-gamefeel §3). Trilinear in shader; CPU path identical math.
- **E39 Height fog + shafts**: height term `exp(-(y-base)·falloff)` folded into the existing
  fog blend in all four shader sources + SW raster fog application; sun shafts are a post pass
  (bright-pass masked by depth==far/sky + emissives, N-tap radial accumulation toward the
  light's projected position). Both effects respect the 06 CPU-parity contract.
- **E40 Lens flares**: renderer-side (not game-side sprites) because occlusion needs the depth
  buffer: sample depth in a 3×3 around the light's screen pos → visibility fraction → fade all
  elements. Elements are textured quads from a small built-in procedural ghost/halo atlas
  (generated once — rings, hex ghosts, streak), tinted by the light color. Draws in overlay
  space after post so bloom doesn't double it.
- **E41 Stretch/trails**: stretch = orient quad along screen-space projected velocity, length
  = size·(1+k·|v|); trails = ring buffer of ≤16 positions per particle (allocated with the
  emitter's max count — memory documented: maxCount×16×12 B), emitted as one strip draw per
  emitter. SW + GPU share geometry generation on CPU (matches existing CPU-simulated
  Particles3D architecture, `game/rt_particle.c` heritage) — no per-backend divergence.
- **E42 AA text/9-slice**: glyph cache keyed by (codepoint, scale bucket of 0.25); 4×
  supersample of the existing 8×8/8×16 bitmap font then box filter — deliberately keeps the
  chunky-pixel *style* but with clean edges at 3.7×-style scales; cache capped 256 entries LRU.
  Nine-slice is pure geometry (9 DrawImage2DRegion-equivalent quads, one call).

## 3. Files

`rt_postfx3d.c/.h` (E37/E38/shafts CPU+chain), backend shader sources ×3 + SW raster
(height fog term, shafts pass, LUT sample, exposure apply), `rt_canvas3d.c/_internal.h`
(fog params, flare draw, AA text cache, nine-slice), new `rt_lensflare3d.c/.h`
(+ source_health baseline bump), `rt_particles3d.c/.h` (stretch/trail sim + strip emit),
`runtime.def`, `docs/viperlib/graphics/rendering3d.md` (new "Cinematic look" section),
tests in `src/tests/unit/` (`test_rt_postfx3d_cpu.cpp` extensions, new
`test_rt_lensflare3d.cpp`, `RTParticles3DContractTests` extensions, canvas text tests).

## 4. Tests

1. Auto-exposure: dark scene (mean lum 0.02) → exposure rises to clamp within adaptSpeed
   window; bright flash → downward adaptation faster than upward ×2.5 ± tolerance; static
   scene → exposure stable (no oscillation > 0.01 EV frame-to-frame).
2. LUT: identity LUT → byte-identical to ungraded (golden); inverted LUT → channels invert;
   blend 0.5 → midpoint. Strip round-trip: MakeIdentityLUT → AddColorLUT is a no-op.
3. Height fog: camera at base height sees density d at ground, e^-1·d at 1/falloff above;
   golden with distance fog composition. Sun shafts: occluder pillar casts a visible dark
   wedge in the shaft pass golden; sun behind camera → pass contributes zero.
4. Lens flare: unoccluded light → elements along axis at expected screen positions (golden);
   fully occluded → alpha 0 within fade window; SW raycast fallback agrees with depth test.
5. Particles: stretch k=0 equals old golden (regression); k>0 lengths scale with |v|;
   trail strip vertex count = min(age steps, maxSegments)×2; trails deterministic across runs.
6. AA text: golden at scales {1.0, 1.5, 3.7}; edge gradient ≤ 2px; cache hit-rate probe;
   MeasureText2DAA monotonic in scale. Nine-slice: corners unscaled, edges axis-scaled (golden).
7. All features: VM==native determinism probe; 06-parity SSIM check for shafts/LUT/exposure
   on Metal vs CPU reference.

## 5. Verification gate

`ctest -R 'postfx|particle|lensflare|canvas3d'` + `-L graphics3d` green → goldens committed →
runtime completeness + surface audits + ADR → full no-skip build → Metal visual pass: the
L1-dusk look target (00-vision §7) reproduced on the test scene — ACES + auto-exposure + LUT +
height fog + shafts + flare on the low sun, ash particles stretching in wind. Screenshot
recorded into `misc/plans/fps/lookdev/` as the standing art-direction reference.
Game consumers: 27-gamefeel §3 (per-level LUTs + moods), 13/14-weapons (tracer stretch, rivet
trails, EMP ribbons), 20/21-levels (cavern god-rays via shafts on spot lights is out of scope —
shafts are sun-only, documented; caverns use volumetric-look via fog + point shadows instead).
