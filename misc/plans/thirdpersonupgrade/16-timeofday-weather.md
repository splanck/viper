# Plan 16 — Procedural Sky, Time-of-Day, Weather Presets

## 1. Objective & scope

Promote what Ridgebound hand-rolled in Zia into the runtime: an analytic daytime sky, a time-of-day clock that drives the sun (direction/color/intensity), ambient, fog, and IBL recapture, plus weather presets (rain/snow/wind) that coordinate particles, vegetation wind, and material wetness. Today the skybox is a static cubemap and every environment is a fixed lighting rig (`Environment.Outdoor/Sunset/Overcast/Night` presets).

**In scope:** (a) `Sky3D` analytic sky render source; (b) `TimeOfDay3D` clock driving sun/ambient/fog/sky/IBL; (c) `Weather3D` presets (rain, snow, clear, wind); (d) material wetness hook.
**Out of scope:** volumetric clouds (flat gradient + sun disc v1), night sky star field (simple dark gradient + moon disc), froxel volumetrics (plan 19 is analytic).

**Zero external dependencies — absolute.** Sky model implemented from the published Preetham analytic formulation (or a simplified gradient+scattering approximation if Preetham exceeds budget — decision recorded in §8).

## 2. Current state (verified anchors)

- **No sky/time-of-day system** (grep `atmosphere|time_of_day|skydome` — none). Skybox = static `CubeMap3D` via `World3D.SetSkybox`/canvas skybox (`rendering3d.md`), drawn after opaques.
- **Environment presets are one-shot:** `Environment.Outdoor(world)` applies clear color/ambient/lights/fog once (`game3d.md` §Environment); `EnvHandle.withFog(near, far)` sets distance fog.
- **IBL recapture is cheap by design:** "The payload is computed once when IBL is enabled or the skybox changes; frames pay nothing extra" (`game3d.md` §Environment) — a sky-driven skybox update can re-trigger it on a throttle.
- **Sun light:** `rt_light3d_new_directional` + set_direction/color/intensity (`rt_canvas3d.h` light API) — the clock rewrites one retained directional slot.
- **Wind consumers exist:** `Vegetation3D.SetWindParams(speed, strength, turbulence)` (`rendering3d.md` §Vegetation3D); plan 27 cloth consumes the same wind vector.
- **Wetness hook:** `Material3D.SetCustomParam` exists (runtime.def Material3D block) — a named param the PBR shader can consume (roughness/darkening modulation).
- **Particles:** `Particles3D` emitters + `EffectRegistry3D` world-owned registry with floating-origin rebase handling (`game3d.md` §Effects3D) — weather emitters register there (camera-attached emitter volume).
- **Determinism:** world `elapsed` is the only clock (synthetic-clock test path exists — `runFramesOnly`), so time-of-day driven by elapsed stays deterministic.

## 3. Design

### 3.1 `Sky3D` (render source)

New C `src/runtime/graphics/3d/world/rt_sky3d.c`. Given sun elevation/azimuth: compute an analytic sky radiance function; **implementation choice: render to the existing cubemap path** — a low-res cubemap (default 64², configurable) rasterized on CPU per update (6×64² texels × simple analytic = sub-millisecond) and installed as the canvas skybox. This reuses the entire skybox + IBL machinery with zero backend shader work, keeps all four backends identical, and makes IBL recapture "free" (skybox-change triggers the existing lazy rebuild). Sun/moon discs splatted into the cubemap. `Sky3D.New()`, `SetSunDirection(v)`, `SetTurbidity(t)`, `SetGroundAlbedo(rgb)`, `Update(canvas)` (regenerate + install if dirty).

### 3.2 `TimeOfDay3D` (the clock)

`World3D`-owned optional driver (`World3D.setTimeOfDay(tod)`), ticked in `stepSimulation`:

- `hours` [0,24) prop; `dayLengthSeconds` (0 = paused clock, drive `hours` manually); advances by scaled dt (plan 08).
- Sun direction from hour angle (+ `latitudeDegrees` tilt); drives: retained sun `Light3D` slot (direction; color/intensity from an elevation-keyed curve table — warm low sun, white noon, off below horizon with a moon light swap), ambient color curve, fog color/density curve (feeds plan 19's height fog when present, else distance fog), and `Sky3D` sun direction.
- IBL/sky refresh throttle: regenerate the sky cubemap when sun direction moves > `refreshDegrees` (default 2°) — bounds recapture cost; plan 15 probes get `CaptureDirty` set on the same trigger.
- Curve tables are built-in defaults with override setters (`setSunColorKey(elevationDeg, r, g, b, intensity)` etc., piecewise-linear).

### 3.3 `Weather3D`

World-owned: `World3D.setWeather(kind, intensity, transitionSeconds)` with kinds Clear/Rain/Snow (constants class `Game3D.WeatherKind`). Effects, crossfaded over the transition:

- **Particles:** a camera-following box emitter (registered in the effect registry; respects floating-origin rebase) with per-kind presets (rain streaks: fast, stretched billboards; snow: slow, drifting).
- **Wind:** world wind vector `setWind(dir, speed, turbulence)` (also settable standalone) pushed to all registered `Vegetation3D` instances and readable by plan 27 (`World3D.get_wind`). Weather kinds set default wind.
- **Wetness:** global material param `wetness` [0,1] pushed via the per-frame constants as a scene-level uniform; PBR shader darkens albedo (×0.8 at 1.0) and drops roughness (×0.6) on up-facing surfaces (normal.y-weighted) — one small shader change (×4 sources, batch with 14/15/19).
- **Audio hook:** none built-in; docs show pairing with plan 24 ambient beds.

## 4. Implementation steps

1. `Sky3D` analytic model + CPU cubemap generation + skybox install; C unit test (noon zenith brighter than horizon-opposite-sun; sunset horizon redder than zenith — ratio asserts).
2. `TimeOfDay3D` clock + sun-slot driving + curves + throttled sky/IBL refresh; unit tests over a 24 h sweep.
3. Weather particles presets + camera-follow emitter + crossfade.
4. Wind plumbing (world vector → vegetation registry; getter for cloth).
5. Wetness scene uniform + shader term (×4 sources; Metal+SW verify, waiver others).
6. Plan-15 probe dirty hook; `Environment3D` preset compatibility (presets now set a paused clock at a fixed hour).
7. runtime.def + audits + ADR + docs (`game3d.md` §Environment rewrite) + golden probes (dawn/noon/dusk SW captures of a fixed scene).

## 5. Public API changes (runtime.def)

```
"Viper.Graphics3D.Sky3D": New; props Turbidity, GroundAlbedo…; SetSunDirection; Update(canvas)
"Viper.Game3D.TimeOfDay3D": New(world); props hours, dayLengthSeconds, latitudeDegrees, refreshDegrees;
    setSunColorKey/setAmbientKey/setFogKey (fluent)
"Viper.Game3D.Weather3D" (world-owned, accessed via World3D):
World3D additions: setTimeOfDay(obj), setWeather(i64,f64,f64), setWind(obj<Vec3>,f64,f64), get_wind,
    get_wetness/set_wetness
"Viper.Game3D.WeatherKind": Clear=0, Rain=1, Snow=2 (constants class, leaf unique)
```

Leaves `Sky3D`/`TimeOfDay3D`/`WeatherKind` unique (`Weather3D` only if surfaced as a class — v1 keeps weather on World3D, so no class). New file → source-health; ADR `00xx-sky-timeofday-weather.md`.

## 6. Tests

- **Sky model (C unit):** elevation 60° ⇒ zenith/horizon luminance ratio in expected band; sunset ⇒ sun-side horizon red ratio > opposite side (fail-before: no API).
- **Clock:** 24 h sweep at `dayLength=24` s ⇒ sun elevation sinusoid; light slot direction matches; below-horizon intensity 0; deterministic replay ×2.
- **Throttle:** slow clock triggers ≤ expected sky regenerations per simulated hour (counter assert).
- **Weather crossfade:** Rain→Clear over 2 s ⇒ emitter spawn rate ramps monotonically to 0; wind returns to preset.
- **Wetness:** golden pair (dry vs wet 1.0) — up-facing floor darkens/sharpens, wall nearly unchanged.
- **Compat:** scenes never touching the system render bit-identical (SW) — presets unchanged until `setTimeOfDay` installed.

## 7. Verification gates

Full build + ctest; SW goldens (dawn/noon/dusk + wet/dry); Metal verify + waiver; determinism gate (ticks in stepSimulation); `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **Model budget decision:** implement Preetham directly from the paper's coefficient tables; if numeric behavior at extreme turbidity/elevation resists validation within budget, fall back to the documented two-band gradient + Mie/Rayleigh-inspired sun halo approximation — record the choice in this file when made. Either way the API is unchanged.
- **CPU cubemap regen** must stay off the steady-state frame (throttle); 64² default keeps worst case sub-ms — measured in the perf probe.
- **IBL churn:** recapture invalidates the cached environment payload; throttle degrees is the knob — document interaction for scenes with plan-15 probes (probe recapture is *manual* per `CaptureDirty` unless the game opts in).
- **Night is a lighting problem, not a sky problem:** moon light slot + darker curves; do not attempt stars v1.
