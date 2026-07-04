# Plan 11 — Demo Modernization: Facade-First Rewrite Pass

> **Status (2026-07-03): NOT STARTED — queued as its own dedicated pass; the new
> showcase demo landed instead.** `examples/3d/overhaul_showcase/` (Plan 12's
> deliverable, ctest probe `g3d_overhaul_showcase_probe`) now demonstrates the
> modern patterns this plan migrates the older demos toward: GameBase3D/IScene3D
> scene flow, Behavior3D-driven entities, Environment3D + IBL, the new
> shadow/post-FX knobs, and `Viper.Game.UI` widgets drawn directly on Canvas3D
> (the ridgebound-style HUD panel + bars, superseding hand-drawn rects). The
> per-demo migrations below (ridgebound camctl/hud, 3dbowling camera, 3dscene
> skybox/entities, shared mathutil) remain as specified and should run as one
> dedicated pass with each demo re-verified — demo churn without per-demo probes
> is deliberately not tail-ended onto an unrelated session.


## 1. Objective & scope

The 3D game demos are the engine's showcase and its de-facto documentation — and today they under-sell it. The `examples/3d/*` set (starter/showcase/openworld_slice) uses the Game3D facade well, but the games (`examples/games/{3dscene,3dbowling,3dbaseball,ridgebound}`) hand-roll cameras, matrices, physics lifecycles, skyboxes, and math the runtime already provides (~800–1,000 removable lines). Rewrite them facade-first so the demos model best practice, and use the residue as the acceptance proof for plans 02/08.

**In scope:** the four demos above; a shared math util; adoption of existing APIs (no new runtime features required except where flagged as dependencies). **Out of scope:** xenoscape (2D — different stack), gameplay/content changes, visual redesigns (fidelity must be preserved or improved).

**Zero external dependencies:** demos remain pure Zia against the Viper runtime — no external assets pipelines, tools, or libraries introduced by the rewrite.

## 2. Current state (verified anchors)

| Demo | Layer | Hand-rolled today |
|---|---|---|
| ridgebound (~4.3K) | Game3D facade (best-in-class) | 166-line camera rig `camctl.zia:1-166` (manual orbit + FP eye math + `Camera3D.LookAt` per frame) despite `OrbitController`/`FirstPersonController`/`world.SetCameraController` existing; hardcoded key polling (`game.zia:306-326`) |
| 3dbowling (~5.5K) | Raw Graphics3D + Physics3DWorld | 5-mode camera hand-lerping eye/target with a local `lerp()` (`engine/camera.zia:145-166,214-219`) vs `Camera3D.SmoothFollow/SmoothLookAt`; ~42 hand-composed `Mat4` draw sites; manual `Physics3DBody.NewSphere` + 9 setters + `Add/Remove` lifecycle (`lane/ball.zia:163-179,583-611`) vs `BodyDef`/`Entity3D.AttachBody`; already GOOD: `Camera3D.Shake` (`:191`), `Path3D` cinematic (`:119-187`), `Viper.Input.Action` action map (`game_flow.zia:110-204`) |
| 3dscene (805) | Raw Graphics3D | Entire scene as `Mat4.Mul(Translate,Scale)` → `DrawMesh` (49 `Mat4.*` calls, `game.zia:281-414`); hand orbit camera (`:266-279`) vs `Camera3D.Orbit`; ~100-line procedural skybox (`:473-584`) |
| 3dbaseball (442) | Raw Graphics3D (viewer) | Near-identical ~100-line skybox copy (`game.zia:213-323`); hardcoded key polling (`:100-108`); already good: `SceneAsset` + `Camera3D.FPSUpdate` |
| all four | — | 6+ duplicate copies of clamp/lerp/pack (`3dscene:651-670`, `3dbaseball:325-349`, `ridgebound/mathutil.zia`, `3dbowling/util/mathutil.zia`); per-demo main loop + dt clamp; menus/state machines (`ridgebound/menu3d.zia` 469 lines, `3dbowling/menu.zia` 353); raw HUD (169 `DrawRect2D/DrawText2D` calls) |

Reference implementations to imitate: `examples/3d/game3d_starter/main.zia`, `game3d_showcase/showcase.zia`, `openworld_slice/main.zia`. Friction log to close out: `examples/games/ridgebound/RUNTIME_API_BUGS.md` (BUG-002/003 already fixed in runtime — update the doc's status column as part of this pass).

## 3. Design — per-demo work orders

Ordered so each tranche is independently shippable; fidelity gate = before/after captures look identical or better.

### Tranche 1 — no new runtime features needed (doable immediately)

1. **Shared math util**: one `examples/games/lib/mathutil3d.zia` (clamp/lerp/absf/maxf/pack helpers); the four demos import it; delete local copies (~120 lines).
2. **ridgebound camera**: replace `camctl.zia` orbit mode with `OrbitController` + `world.SetCameraController`; first-person mode with `FirstPersonController`. **Keep** the demo-specific ground-clearance/FOV-kick behaviors *if* the built-ins can't express them — in that case shrink `camctl.zia` to only that delta and file the residue as evidence for the "composable camera rig" future item (don't force-fit; record what didn't map).
3. **3dbowling camera smoothing**: swap the hand-lerp for `Camera3D.SmoothFollow`/`SmoothLookAt`; keep mode selection + `Path3D` cinematic untouched.
4. **3dscene camera**: `Camera3D.Orbit`.
5. **Input**: ridgebound + 3dbaseball adopt the `Viper.Input.Action` pattern 3dbowling already proves (named actions, one binding block) — kills hardcoded key polling.

### Tranche 2 — facade ports (larger, still no new features)

6. **3dscene → World3D/Entity3D**: scene objects become spawned entities (`Prefab.BoxXYZ`/`Entity3D.SetPosition/SetScale/SetRotationEuler`); the draw functions collapse into setup code; the animated pieces (orbiting effects) keep per-frame `SetPosition` updates (or adopt `Behavior3D.AddOrbit` once plan 02 lands — flag as optional upgrade). Est. −150 lines + free culling/sorting.
7. **3dbowling physics lifecycle**: ball/pins become `Entity3D` + `BodyDef.Sphere(...).Restitution(...).WithLayer(...)` + `AttachBody`; collision handling moves to `world.CollisionEvent` iteration (pattern: `showcase.zia:239-251`). Rendering keeps material fidelity; ~42 `Mat4` sites collapse into entity transforms. Est. −200–350 lines. Bowling's fine-grained control points (weight-change body rebuild) map to re-`AttachBody` — verify equivalence with the scoring regression (below).
8. **Skyboxes**: both copies replaced. 3dbaseball (viewer, stays raw): needs a low-level helper — **dependency: add `CubeMap3D.Gradient(top, horizon, bottom) -> obj` + optional `WithSun(dir, color, size)`** (small C addition in `render/rt_cubemap3d.c` + def entry; the ONLY new runtime surface this plan requires — spec'd here, ~150 LOC, reuses the existing face-generation internals the demos duplicated). 3dscene (now on the facade): `Environment3D.Outdoor()/Sunset()` preset. −~200 lines total.

### Tranche 3 — dependent on plans 02/08 (sequence after them)

9. **Menus/state machines** → `GameBase3D`/`IScene3D` (plan 02): ridgebound `menu3d.zia` + screen-state switch (`game.zia:328-426`), 3dbowling `game_flow.zia:65-107` + `menu.zia` become scenes; menu rendering moves to `Game.UI` MenuList/Panel on Canvas3D (plan 08).
10. **HUDs** → widget toolkit (plan 08): ridgebound `hud.zia` (148 lines) is plan 08's own acceptance item; 3dbowling `engine/hud.zia` (303 lines) follows the same pattern.

## 4. Implementation steps

Tranche 1 → 2 → (08's `CubeMap3D.Gradient` mini-feature inside tranche 2) → tranche 3 after plans 02/08. Each demo edit lands as its own commit with a before/after screenshot pair and a line-count delta in the commit body. Update `RUNTIME_API_BUGS.md` statuses in the ridgebound tranche.

## 5. Public API changes

Only `Viper.Graphics3D.CubeMap3D`: `RT_METHOD("Gradient","obj(i64,i64,i64)")` static-style factory (+ `GradientWithSun` overload) — handlers in `rt_cubemap3d.c`, def entry near the existing CubeMap3D block (13047), surface audits + completeness after. Everything else is demo-side Zia.

## 6. Tests

- Demos are examples, not ctest units — the gates are: (a) each demo builds and runs via `./scripts/build_demos.sh` (note: demos build at -O2 per the current script — any demo change must pass there); (b) deterministic behavior pins where they exist (3dbowling scoring: run a fixed synthetic-input game before/after the physics port and compare the frame-by-frame score/pin states — write this harness first, it's the tranche-2 safety net); (c) before/after screenshot pairs reviewed.
- `CubeMap3D.Gradient` gets a real unit test (face continuity across edges, gradient monotonicity) in `test_rt_canvas3d.cpp`.
- Zia style rule: all demos keep namespace bindings at top of file (project convention — never inline `Viper.X.Y.method()`).

## 7. Verification gates

`./scripts/build_demos.sh` green; full runtime build + ctest unaffected (only the CubeMap3D addition touches runtime — full gates for that commit); screenshot review per demo; line-count report vs the ~800–1,000 estimate recorded in this file when done.

## 8. Risks & constraints

- **Behavior fidelity is the whole game** for 3dbowling — the physics port changes code paths, not intended behavior; the deterministic scoring harness is mandatory before touching `ball.zia`.
- **Don't force the built-ins**: where a demo's camera/effect genuinely exceeds the built-in controllers, keep the residual custom code small and *document the gap* — that residue is roadmap input (composable camera rig), not failure.
- **Demo -O2 constraint**: demos build at -O2; any optimizer-sensitive pattern regression shows up here first — full demo build is part of every tranche's gate.
- **Zero external dependencies**: no new assets requiring external tools; generated skyboxes replace baked ones.
