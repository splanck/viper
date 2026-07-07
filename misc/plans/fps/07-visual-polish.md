# 07 — Engine: Visual Polish — Auto-Exposure, LUT Grading, Height Fog & Sun Shafts, Lens Flares, Particle Stretch/Trails, AA Overlay Text

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track E · **2-session chunk**
> (session A: E37 auto-exposure + E38 LUT + E39 fog/shafts; session B: E40 flares +
> E41 particles + E42 overlay text/9-slice). This is the "beautiful" doc: the six features
> that separate a tech demo from an art-directed game. Verified missing at baseline:
> no auto-exposure or color LUT (`rt_postfx3d.c` — only fixed `exposure` param at `:90,478`
> and an internal gamma table at `:344-366`); fog is linear-distance only
> (`rt_canvas3d.h:676-680`); zero hits for volumetric/godray/lens-flare in the 3D tree;
> `Particles3D` has no stretch/trails (runtime.def Particles3D block); overlay text is an
> integer-scaled bitmap font (`DrawText2DScaled`, runtime.def:13358).

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
Viper.Graphics3D.PostFX3D.AddColorLUT(obj,f64)           void(obj,obj,f64)
    — (lutPixels, blend): 16³ LUT baked as a 256×16 PNG strip (Pixels), trilinear sampled,
      blend 0..1 with ungraded. Identity strip generator: PostFX3D.MakeIdentityLUT() obj().
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
