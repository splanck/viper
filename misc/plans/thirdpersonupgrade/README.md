# Third-Person Upgrade — Implementation Plan Index

Goal: bring the Viper runtime to the standard of shipping a **commercially viable third-person 3D adventure game** — cinematics, combat, robust environment collision, and properly sized open-world maps. This plan set came out of a deep review (2026-07-10) of the 3D runtime (`src/runtime/graphics/3d/`), the game layer (`src/runtime/game/`), the public API (`src/il/runtime/runtime.def`, ~100 3D classes), and the flagship demos (ASHFALL, Xenoscape, openworld_slice). The review's conclusion: rendering, physics, animation, nav, and streaming plumbing are production-grade; what's missing is the *genre glue* one layer above — and nearly every plan below composes existing primitives rather than building new subsystems.

Every plan documents the *current* code shape with verified file references before proposing changes. If a cited anchor has drifted by the time a plan is executed, re-verify the surrounding code first — the design intent still holds, but line numbers move.

## Plans (tiered by priority)

### Tier 1 — Third-person core (blockers)

| # | Plan | Axis | Effort |
|---|------|------|--------|
| [01](01-thirdperson-controller.md) | `ThirdPersonController`: collision-aware spring-arm camera + character drive | Platform API | L |
| [02](02-target-lockon.md) | `TargetLock3D`: target acquisition, cycling, lock-on camera framing | Platform API | M |
| [03](03-character-controller-upgrades.md) | Character3D: crouch, dynamic-body blocking/push, moving platforms | Physics | L |
| [04](04-traversal-probes.md) | Ledge/vault/clearance shape-cast probes for mantle/climb gameplay | Physics | M |
| [05](05-hitbox-hurtbox.md) | Bone-attached hitbox/hurtbox volumes with animation-window activation | Gameplay | L |
| [06](06-health-damage.md) | `Health3D` + damage/death event layer on Entity3D | Gameplay | M |
| [07](07-ragdoll.md) | `Ragdoll3D`: skeleton→bodies+joints builder, anim handoff, powered blend | Physics | L |
| [08](08-time-control.md) | World3D time scale, hit-stop, pause | Platform API | M |
| [09](09-cutscene-sequencer.md) | `Timeline3D` cutscene sequencer (camera/anim/audio/subtitle/event tracks) | Cinematics | L |
| [10](10-camera-rails.md) | `RailCamera3D` spline camera + DOF focus drive | Cinematics | M |

### Tier 2 — Open-world scale

| # | Plan | Axis | Effort |
|---|------|------|--------|
| [11](11-async-streaming.md) | Worker-backed cell/tile streaming with prefetch (kill the load hitch) | Performance | L |
| [12](12-hlod-impostors.md) | Cell-level HLOD proxies + automated impostors | Performance | L |
| [13](13-gpu-skinning.md) | GPU vertex-shader skinning (CPU path stays the SW baseline) | Performance | L |
| [14](14-baked-gi.md) | Lightmap baker + SH light-probe grid | Visuals | L |
| [15](15-reflection-probes.md) | Parallax-corrected local reflection probes | Visuals | M |
| [16](16-timeofday-weather.md) | Procedural sky, time-of-day sun drive, weather presets | Visuals | L |
| [17](17-world-persistence.md) | Streamed-world entity-state persistence + save slots | Gameplay | M |
| [18](18-terrain-upgrades.md) | Terrain holes, 8 splat layers, slope/height auto-blend | World | M |
| [19](19-volumetric-fog.md) | Analytic height fog + sun inscattering | Visuals | M |
| [20](20-physics-materials.md) | Per-collider physics materials, surface tags, body user data | Physics | M |

### Tier 3 — Adventure-genre systems

| # | Plan | Axis | Effort |
|---|------|------|--------|
| [21](21-interaction-system.md) | `Interactable3D`/`Interactor3D` focus-and-use system | Gameplay | M |
| [22](22-ai-perception-bt.md) | `Perception3D` (sight/hearing) + `BehaviorTree3D` runtime | Gameplay | L |
| [23](23-footstep-surface-events.md) | Footstep/surface event tables (audio/VFX/decal per surface) | Gameplay | M |
| [24](24-audio-immersion.md) | Reverb zones, geometry occlusion, ambient beds, dialogue ducking | Audio | L |
| [25](25-dialogue-3d.md) | `Dialogue3D`: speaker anchors, subtitles, localization, choices | Gameplay | M |
| [26](26-facial-lipsync.md) | `LipSync3D` amplitude visemes + blink/gaze layer | Anim | M |
| [27](27-cloth.md) | `Cloth3D` verlet chains/patches (capes, banners, hair tails) | Physics | L |
| [28](28-minimap-markers.md) | `Minimap3D` + compass + world markers | UI | M |
| [29](29-quest-tracker.md) | `Viper.Game.Quests` objective tracker with SaveData integration | Gameplay | M |
| [30](30-profiling-depth.md) | Per-pass GPU timings, streaming hitch tracer, overlay breakdown | Tooling | M |

Effort: M ≈ days, L ≈ a week+ of focused work (AI-assisted velocity; each plan is decomposed into commit-sized steps).

## Dependency graph

```
01 third-person controller ──► 02 lock-on (camera framing hooks)
10 camera rails ─────────────► 09 sequencer (shared spline evaluator)
08 time control ─────────────► 09 sequencer (pause/skip semantics)
09 sequencer ────────────────► 25 dialogue (subtitle track, soft)
20 physics materials ────────► 23 footstep/surface events
24 audio immersion ──────────► 25 dialogue (auto-duck), 26 lip-sync (voice level tap)
16 time-of-day/weather ──────► 27 cloth (wind), 19 height fog (param drive, soft)
11 async streaming ──────────► 12 HLOD (proxy-resident state machine lands first)
05 hitbox/hurtbox ◄──────────► 06 health/damage (one C file pair, land together)
13/14/15/16/19 share the four-backend shader replication cost — batch them
```

## Sequencing

- **Phase TP (playable third-person):** 01 → 02 → 03 → 04 — the camera and body of the game.
- **Phase CBT (combat):** 05+06 → 07 → 08 — hit detection, damage, ragdoll, feel.
- **Phase CIN (cinematics):** 10 → 09 → 25 → 26 — rails first (shared evaluator), then the sequencer, then dialogue/facial.
- **Phase OW (world scale):** 11 → 12, then 13, 18, 19 in any order — streaming is the structural fix, the rest are throughput.
- **Phase LIT (lighting):** 14 → 15 → 16 — shader-heavy; amortize the four-shader-source replication cost.
- **Phase SYS (systems):** 20 → 23, then 21, 22, 24, 27 in any order.
- **Phase PROD (production):** 17, 28, 29, 30 in any order.

Each phase ends with a full no-skip-flags `./scripts/build_viper_unix.sh` run.

## Cross-cutting constraints (apply to every plan)

1. **Zero external dependencies — no exceptions, no libraries, ever.** Viper is a 100% from-scratch project. Every algorithm in these plans (spring-arm sweeps, ragdoll joint fitting, path tracing/radiosity, SH projection, sky models, verlet cloth, RMS envelopes, behavior trees) is implemented in-tree from published specifications or math — never vendored, never linked. If a piece can't be confidently implemented from scratch within budget, it degrades to a graceful recoverable "unsupported" — it is never solved with a dependency. Precedent: the from-scratch BC7/ETC2/ASTC decoders (`assets/rt_textureasset3d_codecs.inc`), the in-repo DEFLATE inflate (`src/runtime/io/rt_compress.c`).
2. **Four shader sources.** Any shading change lands in all of: GLSL (`backend/vgfx3d_backend_opengl_shaders.inc`), HLSL (`backend/vgfx3d_backend_d3d11_shaders.inc`), MSL (`backend/vgfx3d_backend_metal_shaders.inc`), and the CPU rasterizer (`backend/vgfx3d_backend_sw_raster.inc`). On macOS only the Metal + software backends compile and run; implement all four, verify Metal + SW locally, and record a Windows/Linux verification waiver for the other two (established waiver pattern from `misc/plans/game/3dnextlevel2/`). Affects plans 13, 14, 15, 16, 19 (and 12's impostor shading trivially).
3. **100% cross-platform.** Platform checks go through `src/common/PlatformCapabilities.hpp` / `src/runtime/rt_platform.h`, never raw `_WIN32`/`__APPLE__` outside approved adapters. Run `./scripts/lint_platform_policy.sh` for platform-sensitive work (30's timestamp queries, 24's mixer taps).
4. **runtime.def discipline.** New public surface requires: `RT_FUNC` for every `RT_METHOD`/`RT_PROP`, `RT_CLASS_BEGIN`/`RT_CLASS_END` blocks, globally unique class leaf names (`check_runtime_class_leaf_names`), `./scripts/check_runtime_completeness.sh`, surface-audit tests green, `scripts/source_health_baseline.tsv` bumps for new `rt_*.c` files, and `RuntimeSurfacePolicy.inc` entries for internal `rt_*` headers. Runtime C ABI surface changes follow the ADR process (`docs/adr/`), one ADR per plan that adds public classes.
5. **Events by polling, not callbacks.** New event surfaces (hits, damage, perception, interaction, quest updates) use the buffered polling pattern of `World3D.collisionEventCount/collisionEvent` and `AnimTimeline.eventsFiredCount/eventFiredId`. VM callback trampolines stay restricted to the existing `World3D.Run*` family until the trampoline policy changes.
6. **Determinism.** VM and native outputs must match; worker-count must not affect simulation results. Anything worker-staged commits on the main thread in deterministic order (the async-asset precedent). Simulation-touching plans (03, 05, 07, 08, 11, 27) rerun the Game3D determinism gate: worker-count replay tests, `test_rt_game3d`, `test_codegen_env_is_native`, native Zia promise tests, `test_crosslayer_arith`.
7. **Testing.** Every plan ships fail-before/pass-after tests: C unit tests (`viper_add_test` + `viper_add_ctest` pattern in `src/tests/CMakeLists.txt`), Zia probes in the `g3d_*` convention, and golden final-frame baselines (`examples/3d/baselines/` pattern) for visual features.
8. **Gate gotcha.** The build script's ctest run uses `-LE slow` — explicitly run `ctest --test-dir build -L slow --output-on-failure` before declaring any phase done, plus `-L graphics3d` and the surface audits after runtime.def changes.

## Verification policy

Per plan: the plan's own §7 gates. Per phase: full build + full ctest + `-L slow` + platform lint, all local by-hand checks. Perf plans (11, 12, 13, 30) record before/after numbers from the existing telemetry (`World3D` counters, `Canvas3D.FrameGpuTimeUs`, `WorldStream3D.residentBytes`, the `g3d_openworld_slice_perf_harness` metrics). Visual plans (14, 15, 16, 19) land with golden probes rendered on both Metal and software backends. Combat/traversal plans land with deterministic replay probes proving VM/native parity.
