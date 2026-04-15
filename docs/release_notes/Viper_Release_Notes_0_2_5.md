# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### Overview

v0.2.5 opens the post-v0.2.4 cycle with a focused pass on runtime-surface hygiene and a top-to-bottom overhaul of the flagship 2D game demo. Themes so far:

- **Runtime surface hardening** — `rtgen` now depends on runtime headers and `RuntimeSurfacePolicy`; ad-hoc local `extern rt_*` forward declarations are replaced with the owning runtime headers across game and graphics runtime files; `AchievementTracker` uses `rt_string` handles end-to-end with consistent retain/release and real graphics-string ABI draw calls.
- **Review-readiness documentation** — new codemaps for the bytecode VM and the graphics-disabled runtime stubs, plus clarifications to the optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language so docs no longer overclaim parity beyond available evidence.

---

### Runtime Surface Hardening

**AchievementTracker string ABI**

- `Viper.Game.AchievementTracker` now threads runtime string handles end-to-end instead of raw C strings. The tracker retains and releases `rt_string` values consistently and draws notifications through the real graphics string ABI rather than transient C-string conversions.
- Contract tests in `src/tests/runtime/RTAchievementTests.cpp` extended to cover the handle lifecycle.
- New AArch64 native smoke probe for achievement drawing at `src/tests/e2e/achievement_draw_native_probe.zia` + `test_achievement_draw_native.sh`, wired into ctest.

**Runtime header discipline**

- `rtgen` picks up runtime headers and `RuntimeSurfacePolicy` as CMake dependencies, so changes to the authoritative runtime surface invalidate the generated bindings.
- Canvas3D internal helper declarations moved out of ad-hoc local externs and into a new owning `rt_canvas3d_internal.h`. Callers (`rt_canvas3d.c`, `rt_canvas3d_overlay.c`, `rt_model3d.c`, `rt_scene3d.c`, `rt_scene3d_vscn.c`, `rt_fbx_loader.c`, `rt_skeleton3d.c`, `rt_morphtarget3d.c`, `rt_animcontroller3d.c`) include the owning header instead of redeclaring prototypes locally.
- Stray `extern rt_*` forward declarations across `rt_canvas.c`, `rt_countdown.c`, `rt_gui_app.c`, `rt_input.c`, `rt_sprite.c`, and the game runtime (`rt_config.c`, `rt_debugoverlay.c`, `rt_entity.c`, `rt_leveldata.c`, `rt_lighting2d.c`, `rt_raycast2d.c`, `rt_scenemanager.c`, `rt_animstate.c`) replaced with includes of the owning runtime headers.

These two moves together close a long-standing layering smell: runtime callers no longer advertise their own C ABI for functions they don't own, and `rtgen` can no longer drift silently when the runtime surface changes.

---

### Demos

- **Crackman** (`examples/games/pacman/` → binary `crackman`): the former Pac-Man demo was renamed, split from a monolithic `game.zia` into `session` / `progression` / `frontend` / `settings` modules with per-concern sprite files (`sprite_maze`, `sprite_items`, `sprite_entities`, `sprite_support`), declarative audio banks (`audio/music_tracks.zia`, `audio/sfx_bank.zia`), a deterministic headless `smoke_probe.zia` run by the new `zia_smoke_crackman` ctest, a shared `viper_8x8.bdf` bitmap font, responsive layout/theme/widgets modules, persistent XP/rank `SaveData`, rotating contracts with rewards, run grades, and richer profile/run-summary rendering. Chess picks up the same UI decomposition (`ui/layout.zia`, `ui/theme.zia`, `ui/widgets.zia`) and the shared bitmap font. Built binaries are renamed `pacman-zia` → `crackman` across `build_demos_{linux,mac,o2}.sh` and `build_demos_win.cmd`.

### Documentation

- New `docs/codemap/bytecode-vm.md` and `docs/codemap/runtime-graphics-stubs.md`; `docs/review-readiness.md` clarifies optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language; `docs/viperlib/graphics/pixels.md` documents that `Viper.Graphics.Color.RGBA()` returns `0xAARRGGBB` (Canvas-friendly) which does **not** match `Pixels.Set`/`Pixels.Get`'s `0xRRGGBBAA` layout, and recommends `SetRGB()` for opaque pixels or packed `0xRRGGBBAA` literals for explicit alpha; minor updates to `docs/faq.md`, `docs/gameengine/**`, `docs/il-passes.md`, `docs/tools.md`, and `docs/zia-getting-started.md` for the Pac-Man → Crackman rename and review-readiness wording.

---

### Build

- `src/buildmeta/VERSION` bumped from `0.2.4-dev` → `0.2.5-snapshot` to close out v0.2.4 and open the v0.2.5 development cycle.

---

### Tests

- `zia_smoke_crackman` — runs `examples/games/pacman/smoke_probe.zia`, expects `RESULT: ok`, 30 s timeout, labels `zia;smoke`.
- `test_achievement_draw_native` — AArch64 native smoke probe exercising the runtime-string-handle draw path for achievement notifications.
- Extended `RTAchievementTests.cpp` contract coverage for the `rt_string` retain/release lifecycle.

---

### Commits Included

| Commit | Date | Summary |
|---|---|---|
| `d58df4f98` | 2026-04-14 | `chore(demos,build,docs)`: pacman → crackman binary rename, chess + crackman UI polish, VERSION → 0.2.5-snapshot |
| `74f4ec4c7` | 2026-04-14 | `feat(crackman)`: rename Pac-Man demo to Crackman, split into session/progression/frontend, add smoke probe and audio banks |
| `8126432f6` | 2026-04-15 | Harden runtime surface and Crackman progression |

<!-- END DRAFT -->
