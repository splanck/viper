# ASHFALL — FPS Showcase + Engine Upgrade Program

> **STATUS: PLANNED (2026-07-07)** · Baseline: `3166d1dc2` · Owner docs: this directory.
> Mission: take Viper 3D to a **Unity-tier quality bar** by (Track E) eliminating every validated
> runtime limitation and (Track G) shipping ASHFALL — a ~28–30K-line single-player sci-fi campaign
> FPS at `examples/games/ashfall/` that is a genuine showcase: 9 campaign levels across 3 acts,
> a hub, 11 enemy archetypes + 3 bosses, 10 weapons, stealth/holdout/escort variety, and a
> polish pass (game feel, art direction, accessibility, photo mode) worthy of a storefront demo.

Every numbered doc is a **self-contained implementation chunk sized for one focused session**
(a few are explicitly marked 2-session). Each doc inlines the API signatures it needs (verified
against `src/il/runtime/runtime.def` at the baseline SHA), lists exact files to create/modify,
restates the conventions it depends on, and ends with its own verification gate. A session should
be able to execute its doc without reading the others (this README's conventions excepted).

---

## 1. Document index

### Track E — Engine upgrades (eliminate the constraints)

| Doc | Contents | Size | Unblocks |
|---|---|---|---|
| [01-input-window.md](01-input-window.md) | Raw relative mouse (Win32/macOS/X11/mock), gamepad→`Input3D`, fullscreen-at-creation, `Path.DataDir` | 2-session | P0 |
| [02-audio-upgrades.md](02-audio-upgrades.md) | `Voice.set_Pitch`/`PlayEx2`, `SoundSource3D.set_Pitch`, per-voice occlusion lowpass, group ducking | 1 | P2 |
| [03-shadows-lighting.md](03-shadows-lighting.md) | Point-light (omni) shadows, CSM/slot decoupling + 8-slot atlas, cluster budget config, dropped-light telemetry | 2-session | P9 |
| [04-physics-upgrades.md](04-physics-upgrades.md) | Swept-TOI CCD + anti-tunnel tests, quickhull reduced convex colliders, kinematic mesh colliders, query capacity | 1–2 | P7 |
| [05-renderer-upgrades.md](05-renderer-upgrades.md) | View-model pass, `Sprite3D` additive, zero-copy RT→material, `RenderTarget3D.CopyTo`, SW instancing, PVS portal-frustum clipping | 2-session | P4 |
| [06-postfx-software-parity.md](06-postfx-software-parity.md) | CPU SSAO/DOF/MotionBlur/SSR/TAA so one post-FX chain runs on every backend (and becomes the GPU parity reference) | 2-session | P15 |
| [07-visual-polish.md](07-visual-polish.md) | Auto-exposure, LUT color grading, height fog + sun shafts, lens flares, particle velocity-stretch + trails, AA overlay text + 9-slice | 2-session | P15 |
| [08-anim-nav-upgrades.md](08-anim-nav-upgrades.md) | Barycentric 2D blend trees, retarget bone-map override, always-tiled navmesh rebuilds, agent link-state query | 1 | P5/P7 |
| [09-asset-pipeline-upgrades.md](09-asset-pipeline-upgrades.md) | QEM mesh simplification + `GenerateLODs`, ASCII FBX, EXT_meshopt (stretch) | 2-session | P18 |
| [10-toolchain-fixes.md](10-toolchain-fixes.md) | macOS native aggregate-return miscompile fix, `-O0` vs `-O2` differential harness, papercuts | 1 | P0 |

### Track G — ASHFALL game

| Doc | Contents | Size |
|---|---|---|
| [00-vision.md](00-vision.md) | Concept, pillars, rosters, level arc, scope, the quality bar | — |
| [11-architecture.md](11-architecture.md) | Module map + line budgets, entity registry, fixed timestep, event bus, damage pipeline, perf budgets, probe pattern | 1 |
| [12-core-loop.md](12-core-loop.md) | Game states, player controller, camera rig, input map + rebinds, fullscreen policy, graybox arena | 1–2 |
| [13-weapons-hitscan.md](13-weapons-hitscan.md) | Weapon framework, spread/recoil model, 5 hitscan weapons, tracers/muzzle/impact FX, hitmarkers | 1–2 |
| [14-weapons-physics.md](14-weapons-physics.md) | Projectile pools, 5 physics/energy weapons, 3 grenades, melee, explosions, destructible interplay | 1–2 |
| [15-ai-framework.md](15-ai-framework.md) | Perception (sight/sound/light), awareness, squad director, cover, FSM base, stealth rules | 1–2 |
| [16-enemies.md](16-enemies.md) | 11 archetype specs: behavior, stats, anim sets, tells/counters, spawn rules | 2-session |
| [17-bosses.md](17-bosses.md) | WARDEN Goliath, SHRIKE Prime, HELIX Avatar — phases, arenas, choreography | 1–2 |
| [18-animation.md](18-animation.md) | Rigs, locomotion blend trees, aim layers, root motion, anim events, IK, retarget pipeline, anim LOD | 1–2 |
| [19-levels-act1.md](19-levels-act1.md) | Level loader + JSON manifest schema; L1 Crashsite, L2 Relay Outpost, L3 Ashveil Crossing | 2-session |
| [20-levels-act2.md](20-levels-act2.md) | L4 Habitat Arcology, L5 Hydroponics Caverns, L6 Storm Terraces; The Redoubt hub | 2-session |
| [21-levels-act3.md](21-levels-act3.md) | L7 Foundry, L8 Spire Ascent, L9 Helix Core; ending + credits flow | 2-session |
| [22-world-systems.md](22-world-systems.md) | Streaming, lighting zones/budgets, doors/devices, pickups, objectives/checkpoints, destructibles, environment/weather | 1–2 |
| [23-ui-hud.md](23-ui-hud.md) | HUD, menus (animated 3D title), options (video/audio/controls/accessibility), subtitles, photo mode, loading screens | 2-session |
| [24-audio-music.md](24-audio-music.md) | Mix architecture, spatialization rules, occlusion, music state machine, synth fallback bank, sound-design sheets | 1–2 |
| [25-meta-systems.md](25-meta-systems.md) | Salvage economy + workbench, collectibles/lore, scoring/medals, difficulty + NG+ mutators, stats, speedrun timer | 1 |
| [26-assets-content.md](26-assets-content.md) | CC0 pack list + licenses, GLB conventions, VPA packing, CREDITS.md, procedural-fallback contract | 1 |
| [27-gamefeel-polish.md](27-gamefeel-polish.md) | Hit-feedback stack, weapon feel curves, art-direction bible, UI motion standards, performance polish, first-run experience | 1–2 |
| [28-phasing-verification.md](28-phasing-verification.md) | 20 phases with gates, platform verification lanes, perf budgets per tier, ship checklist | — |

## 2. Dependency graph (coarse)

```
10-toolchain ─┐
01-input ─────┼─→ 12-core-loop ─→ 13-weapons-hitscan ─→ 16-enemies(I) ─→ vertical slice (P3)
              │        │                                    │
02-audio ─────┘        └─→ 05-renderer(E18/19) ─→ 13/14     ├─→ 15-ai-framework ─→ 16/17
04-physics ─→ 14-weapons-physics ─→ 22-destructibles        └─→ 18-animation (08-anim E26/E27)
03-shadows ─→ 20-levels-act2 (L5 caverns showcase)
05-renderer(E20/E23) ─→ 20-levels-act2 (L4 monitors/PVS)
08-nav(E28) ─→ 22-world-systems (destructible barricades)
06-postfx + 07-visual-polish ─→ P15 look pass (all levels)
09-assets(E30/E31) ─→ P18 content-scale pass
```
Rule of thumb: engine docs land **just before** the first game phase that consumes them
(see §28 phase table). Nothing in Track G may ship a workaround for a Track E item —
if an E item slips, the dependent G phase slips with it.

## 3. Cross-cutting constraints (restated in every doc that touches them)

1. **Zero external dependencies.** All engine work is from scratch (quickhull, QEM decimation,
   meshopt decode, resamplers included). Downloaded game assets are data, not code.
2. **100 % cross-platform.** macOS (Metal), Windows (D3D11), Linux (OpenGL 3.3), plus the
   software backend everywhere. Renderer features obey the **four-shader-source rule**
   (MSL + HLSL + GLSL-330 + software reference). Platform code only in approved adapters
   (`src/common/PlatformCapabilities.hpp`, `src/runtime/rt_platform.h`, `vgfx_platform_*`).
3. **Determinism.** VM == native for every defined program. Gameplay runs on `RunFixed`;
   probes drive synthetic clock + input. New engine features must not break replay determinism.
4. **Runtime surface discipline.** Every new API: `runtime.def` RT_FUNC + RT_METHOD/RT_PROP pair →
   `./scripts/check_runtime_completeness.sh` → surface-audit baselines
   (`scripts/source_health_baseline.tsv` when adding `rt_*.c/.h`) → `docs/viperlib/` docs →
   ADR when the runtime C ABI surface changes → full build + ctest incl. `-L slow` and
   `-L graphics3d`. Class leaf names globally unique.
5. **Capability gating.** Optional features query `Canvas3D.BackendSupports` — but after
   Track E, the **software floor includes the full post-FX chain**; gates remain only where
   physically meaningful (native compressed-texture upload, GPU timers, HDR target precision).
6. **Budgets are asserted, not hoped.** Lighting zones, shadow slots, particle counts, awake
   bodies, draw counts: `config.zia` owns the numbers; the diagnostics overlay and probes
   assert them (`get_DroppedLightCount`, `get_ClusterOverflowCount`, draw/cull telemetry).
7. **No CI.** Verification is local lanes + recorded external runs + the waiver process
   (`misc/plans/game/3dnextlevel3/cross-platform-verification-runbook.md`). D3D11 is the
   most-churned backend — schedule Windows runs early; GL is least verified (W2-002/W2-003).

## 4. Zia + runtime conventions (the landmine list — validated 2026-07-07)

**Language:**
- No `Map[K,V]` → pools + parallel `List`s; slot handles with generation counters.
- `List[T]` fields are non-nullable — initialize `= []`, guard on `.Count`, never `== null`.
- Aggregate (struct) returns miscompile on macOS native until 10-toolchain E34 lands —
  hot-path helpers return via reusable heap instances (`result.set(...)`) meanwhile.
- Integer arithmetic traps on overflow (IAddOvf default) — clamp or `CheckedAdd` counters.
- No `%f` formatting — precompute HUD strings on change events; `toString(Convert.NumToInt(x))`.
- `bind X as Alias` to avoid ambiguous-symbol warnings (`Viper.Math.Lerp` vs `Color.Lerp`);
  always bind namespaces, never inline `Viper.X.Y.method()` calls.
- Unused `for` loop vars emit W001 — use `while` for fixed-count loops.
- `class init()` is field-wise positional; keep field order stable; use named `setup()` methods
  for cross-module construction.
- Modules 500–1,500 lines; split by subsystem from day one.

**Runtime:**
- Property scalars use `set_X` setters — `SetX` method twins may be phantoms (BUG-003 class).
- `PostFX3D.AddColorGrade` brightness is **additive** (~0.0–0.03), not a multiplier.
- HUD draws after `EndScene` inside `BeginOverlay`/`EndOverlay`; `Particles3D.Draw` inside the
  scene pass after opaques; `SpatialAudio3D.SyncBindings` once per frame after physics/anim.
- `AnimController3D.PollEvent` returns one event per call — drain in a loop.
- `World3D` is destroyed last; entities/effects/audio bindings are world-owned.
- `Run*` callback signatures are strict: update `(Float) -> Unit`, overlay `() -> Unit`.
- `Audio.Init` may fail headless — `ready` flag, early-out every play call.
- Magenta/black checker on screen = texture decode failure (not a shader bug).
- Assets resolve CWD-relative; probe `Path.ExeDir()` fallback; **`embed`/`pack` requires the
  single-step `viper build`** — the two-step IL→codegen path silently ships no assets.
- Validate only with `build/src/tools/viper/viper` (never the stale `install-check` binary).

## 5. Verification policy

- Every doc ends with a **gate**: incremental build green → doc-specific unit/golden/probe tests
  (fail-before/pass-after) → full `./scripts/build_viper_unix.sh` (no skips) at phase ends.
- Game probes: headless deterministic (`--smoke`, synthetic clock/input, software backend),
  1-frame render probes per level, scripted combat-sim probes asserting event logs, save/load
  round-trips, and VM-vs-native output equality. `-O0` vs `-O2` native diff (10-toolchain E35)
  runs at every phase end.
- Perf gates (P1 harness, re-run each phase): see 28-phasing-verification §4. Headline targets:
  60 FPS on Apple Silicon Metal at Balanced/1600×900 in the arena stress scene; software backend
  ≥ 30 FPS at 960×540 Performance tier in the same scene.
- Windows lane at P4/P12/P19; Linux lane at P12/P19 (waiver-documented if hardware unavailable).
