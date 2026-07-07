# 28 — Phasing, Verification Lanes, Perf Budgets, Ship Checklist

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · The execution spine: 20 phases,
> each 1–2 Opus sessions, each independently green. Docs referenced as E## (engine) / G-doc
> numbers. **Rule: no phase closes red; no game workaround for an unshipped E item — the
> dependent phase waits.**

## 1. Phase table

| P | Deliverable | Docs | Gate highlights |
|---|---|---|---|
| P0 | Toolchain fixes + input/window/system engine work | 10 (E34/E35), 01 (E1–E4) | codegen regressions, raw-mouse tests, DataDir, diff harness live |
| P1 | Skeleton + core loop + graybox arena + perf harness | 11, 12 | movement-course probe, 60 FPS arena Metal, fullscreen default, baselines recorded |
| P2 | Audio engine + hitscan weapons | 02 (E5–E8), 13 | weapon probes (a–g), pitch/occlusion tests, range hands-on |
| P3 | Enemies I (fallback bodies) + damage + **vertical slice** | 16A, 15 (subset) | arena combat vs 5 archetypes; combat-sim probe; "pistol fun" sign-off |
| P4 | Renderer engine pass + **Windows lane #1** | 05 (E18–E23) | view-model/RT/instancing/PVS tests; D3D11 recorded run (early per churn history) |
| P5 | Animation + asset pipeline proof + rigs | 08 (E26–E29 incl. tiled-nav E28), 18, 26 (§1–§5, Ranger proof) | anim probes, zero-warning retargets, dressed-Ranger end-to-end |
| P6 | AI depth + enemies II | 15 (full), 16B | perception/token/cover/stealth probes, full-roster brawl |
| P7 | Physics engine pass + physics weapons + destructibles | 04 (E13–E16), 14, 22 (§5) | anti-tunnel matrix, projectile probes, chain caps, nav rebuilds |
| P8 | World systems + level tech + arena wave mode | 22 (full), 19 (§1 loader) | manifest round-trips, L1-slice-from-manifests integration run |
| P9 | Shadows/lighting engine pass | 03 (E9–E12) | point-shadow goldens, budget decoupling, telemetry live in F3 |
| P10 | **Act I** (L1–L3) | 19 | act probes, act hands-on, streaming telemetry, lookdev boards v1 |
| P11 | Hub + meta systems | 20 (§1), 25 | economy/medal/difficulty probes, hub loop hands-on |
| P12 | **Act II** (L4–L6) + **Windows lane #2 / Linux lane #1** | 20 | PVS/monitor/Wraith/storm probes, L5 beauty gate, external lane records |
| P13 | Bosses | 17 | phase probes, three fight hands-on, music sync |
| P14 | **Act III** (L7–L9) + ending | 21 | act probes, first full-campaign run, escape-run gate |
| P15 | PostFX parity + visual polish engine pass + level grades | 06 (E25), 07 (E37–E42) | CPU-parity goldens + SSIM, LUT/exposure/shafts/flares/trails live, one-chain-everywhere switch |
| P16 | Audio/music full | 24 | cue coverage, music states, listen pass |
| P17 | Game-feel concentrated pass + UI complete | 27, 23 | checklists scored, motion audit, first-run personas |
| P18 | Content scale: LOD generation + mass dressing + meshopt/ASCII-FBX as needed | 09 (E30–E32), 26 (§mass) | LOD-presence probe, perf re-baselines, empty-assets gate re-run |
| P19 | Packaging + platform closure | 26 (§6), 01 (E3 polish) | VPA single-step build, installers/icons, **Windows lane #3 + Linux lane #2**, waivers filed if hardware-blocked |
| P20 | Ship pass | all | §6 checklist, README+gallery, demo registration, final full-suite |

Engine docs land **inside** their phase (e.g. P9 is 03's implementation), so game sessions
never straddle an engine migration.

## 2. Session sizing guide (Opus-chunk discipline)

Each phase row = 1–2 sessions except acts (2) and 16 (2, split A/B as marked). A session
starts by reading ONLY: README conventions + its own doc (+ the E-doc it consumes, if any).
If a session ends mid-phase: it must end green-buildable with probes for finished parts, and
append a `## HANDOFF` note to its doc (state, next step, open ends) — the next session's
first read.

## 3. Standing per-phase gate (every phase, no exceptions)

1. Incremental build green during work; **full `./scripts/build_viper_unix.sh` (no skips) at
   phase close** — includes ctest with `-L slow` checked explicitly (label gotcha).
2. All prior probes green (regression), new probes fail-before/pass-after documented.
3. `viper check examples/games/ashfall --diagnostic-format=json` clean.
4. VM == native on the smoke/combat probes; **`scripts/native_opt_diff.sh` (`-O0` vs `-O2`)**
   clean (E35).
5. Budget assertions clean (11 §7 telemetry rows); perf within §4 (regression ≤ −10 % vs
   baseline or investigated).
6. Engine phases add: runtime completeness + surface audits + platform-policy lint + ADR when
   surface changed + `docs/viperlib` updated + goldens committed.
7. Phase banner updated in its doc: STATUS → IMPLEMENTED (date, SHA, test counts, perf notes).

## 4. Perf budgets & baselines (recorded at P1, re-measured every phase)

Scenes: arena-stress (wave-10 scenario), L3-streaming-ride, L4-monitor-hall, L5-cavern-full,
L6-storm-peak, L9-phase3. Targets:
| Backend/tier | Scene set | Target |
|---|---|---|
| Metal / Balanced 1600×900 | all | ≥ 60 FPS, hitch ≤ 8 ms over budget |
| Metal / Cinematic 1600×900 | all | ≥ 45 FPS |
| Software / Performance 960×540 | arena-stress, L2-slice | ≥ 30 FPS (the floor gate) |
| D3D11 / Balanced (external lane) | arena-stress + one act level | ≥ 60 FPS recorded |
| GL / Balanced (external lane) | arena-stress | recorded; waiver W-ASH-GL if no hardware |
Sim budgets per tick (M-class): AI ≤ 1.8 ms, anim+skin ≤ 2.2 ms, physics ≤ 2.5 ms worst.
All numbers live in `probes/perf_probe.zia` output format (`fps= frame_ms= draws= ...`,
baseline-file precedent `perf_macos_apple_m4_max.md`) and get committed to
`misc/plans/fps/baselines/`.

## 5. Platform verification lanes (no CI — local + recorded + waivers)

- **macOS (host)**: every phase (Metal + software).
- **Windows/D3D11**: P4 (early — most-churned backend per git history), P12, P19. Recorded
  runs (`.\scripts\build_viper_win.cmd` + demo script + perf probe) into
  `misc/plans/fps/baselines/perf_windows_*.md`. Failures become engine fix tasks before the
  next act phase.
- **Linux/GL**: P12, P19 (software lane minimum on WSL2; GPU lane when hardware available;
  else waiver `W-ASH-GL` filed per the 3dnextlevel3 runbook pattern with exact deferred
  claims).
- Mock/synthetic backend: every probe run (headless determinism).

## 6. Ship checklist (P20)

- [ ] Full campaign hands-on ×2 (Soldier KB/M fullscreen; Scout pad windowed) — zero traps,
      zero soft-locks, checkpoint audit clean.
- [ ] Empty-`assets_src` fallback campaign-smoke green (asset-optional gate).
- [ ] All probes green: unit/golden/e2e + every level/system probe, VM+native, `-O0/-O2`.
- [ ] Full test suite incl. `-L slow`, `-L graphics3d`; runtime completeness; surface audits;
      platform lint.
- [ ] Perf tables complete (§4) incl. external lanes/waivers.
- [ ] Track E closure: every E-item IMPLEMENTED or explicitly re-scoped with user sign-off
      (no silent drops); `docs/viperlib` reflects all new surface.
- [ ] `examples/games/ashfall/README.md` (features, controls, gallery from lookdev,
      perf notes, license section) + CREDITS.md complete.
- [ ] Demo registration: `viper.project` final (pack directives + package metadata),
      `ZIA_DEMOS` arrays in `build_demos_mac.sh` / `_linux.sh` / `_win.cmd`, smoke-timeout
      case entry, `./scripts/build_demos.sh` green end-to-end.
- [ ] Plan-suite banners all IMPLEMENTED; this doc's banner records final SLOC, test counts,
      and the gallery.

## 7. Risk register (standing; owners = the doc that retires each)

| Risk | Watch | Mitigation doc |
|---|---|---|
| D3D11 regressions surface late | churn history | P4 early lane; 05/03/06/07 shader parity probes |
| GL never gets hardware | W2-002/003 open | software-floor design; waiver W-ASH-GL; SSIM parity vs CPU reference (06) |
| CPU post-FX too slow on floor | 06 §3 budgets | per-effect tier defaults; Performance tier keeps FXAA-only |
| Codegen miscompile class recurs | BUG-006 lineage | E35 diff every phase; file+fix pattern (10) |
| Scope creep in acts | 00 §9 non-goals | phase gates + line budgets ±20 % (11 §9) |
| Downloaded packs shift/vanish | CC0 hosts | packs committed to repo at P5; CREDITS versioning |
