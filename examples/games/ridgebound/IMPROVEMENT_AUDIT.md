# Ridgebound 20-point improvement audit

This matrix records the completed implementation behind the Ridgebound demo
upgrade. It is release evidence, not a backlog. Runtime/API history remains in
`RUNTIME_API_BUGS.md`.

| # | Improvement | Shipping implementation and acceptance evidence |
|---:|---|---|
| 1 | Uneven-terrain locomotion | Horizontal character sweeps now attempt the existing bounded step sequence for walkable heightfield contacts as well as walls. `test_character_crosses_uneven_walkable_heightfield` covers sustained shallow undulations; the demo uses a 0.48 m step and 52-degree slope limit. |
| 2 | Route-first terrain | `terrain.zia` grades one shared spawn/beacon network before flattening six arrival pads. `topology_probe.zia` measures all five legs at normal-Y 0.922 or better and sample transitions no larger than 0.576 m. |
| 3 | No-jump traversal | `traversal_probe.zia` drives production `Input3D`/`CharacterController3D` forward input without ever pressing Space. All seven dry route legs progress 25.3-55.2 m with zero stalled frames, and a separate off-route leg enters swimming without stalling. |
| 4 | Deliberate lake topology | A genuinely submerged 2.15 m basin replaces the world-sized water sheet; one bounded reflective surface and broad dry causeways define the playable lake. The topology gate finds 37/37 off-route inner-basin samples at 2.119-2.179 m. |
| 5 | Safe off-route water | Leaving a causeway enters explicit surface swimming at reduced speed with splash feedback instead of walking unseen along the lake floor. Forest and grass placement exclude the basin, and the no-jump replay includes a deliberate shoreline/swim leg. |
| 6 | Useful daylight | Active gameplay owns a 900-second day cycle beginning at phase 0.12. Title and pause never advance it, and first dusk is measured at 342 seconds. |
| 7 | Navigable night | Moonlight, a phase-weighted scale that compensates for the low-energy night IBL coefficients, softer night shadows, and a night-scaled 27 m player light retain terrain and water structure without spending shadow slots on fill lights. The smoke capture measures 30.961 mean scene luminance and 85.499% lit-scene coverage versus the configured minimums of 14 and 20%. |
| 8 | Slow, fair weather | A 600-second calm-to-storm front starts deep in its dry phase. Restart and quit-to-title reset precipitation, cloud, wetness, decals, audio, sky, time, and player state; `state_probe.zia` verifies the clear opening and both deterministic lifecycle paths. |
| 9 | Focused core loop | Five named sites, hold-to-link scanning, return-home completion, route compass, and distance readout form one legible loop. Survival and physics toys are opt-in and disabled by default. |
| 10 | Beacon identity and feedback | Windbreak Spire, Mirror Shore, Sunstep Watch, Ember Grove, and Home Relay have distinct nearby silhouettes plus activation beams, orbit motes, pulses, light ramps, scorch marks, camera response, and layered spatial chimes. |
| 11 | Grounded art direction | Terrain, stone, metal, archive, beacon, critter, foliage, and player materials use a restrained teal/green/stone/gold palette with low emissive values and calibrated roughness instead of saturated debug colors. |
| 12 | Readable forest | The deterministic forest is capped at 72 smaller maples, clears water/routes/spawn/beacons, uses grouped home-grove placement, muted split materials, and a procedural no-dependency fallback. |
| 13 | Player presence | A backpacked explorer assembled from torso, head, pack, lens, arms, and legs replaces the capsule/sphere debug pawn. Direction, gait, swimming, idle breathing, and contact shadow all derive from real motion. |
| 14 | Collision-safe camera | The damped orbit boom sphere-sweeps against world geometry, shortens around terrain/trunks/landmarks, retains a ground clamp, supports first person, and opens on a high lake/ridge composition. |
| 15 | Responsive front end | The scenic title card, menu rows, pause, options, controls, objective card, stamina bar, and minimap derive their sizes from the viewport. The smoke gate drives the real menu state machine and captures title, gameplay, pause, options, and controls at 480x270 as well as the 960x540 release view. |
| 16 | Navigation clarity | The HUD leads with signal count and next site name/cardinal direction/distance; the minimap renders the exact authored trail graph and live beacon states. Context-only scan progress and first-run controls avoid permanent clutter. |
| 17 | Lighting and post-FX restraint | Morning/twilight/night/dawn sky keyframes, sparse stars, directional sun/moon, softened vignette, moderate exposure/contrast/saturation, restrained bloom, and capability-gated depth effects preserve scene structure. Day mean luminance is 77.149 and neutral-white coverage is 0%. |
| 18 | Bounded VFX and audio | Perpetual billboard fireflies/mist are removed. Remaining snow/rain, sprint dust, localized beacon pulses, and decals are bounded and quality-scaled; grass/trail/stone steps, splash, beacon resonance, victory layers, and quiet lake ambience are procedural and spatial. |
| 19 | Scalable performance | Three presets change vegetation LOD, water, weather, shadows/cascades, SSAO, and occlusion while keeping the authored grade. The final July 2026 backend-audit run measured the warmed Metal Cinematic frame at 10.285 ms against a 50 ms 960x540 budget. |
| 20 | Executable release evidence | Project-wide structured checking plus topology, no-jump traversal, state, and visual/performance probes are wrapped by cross-platform existing-binary runners. Diagnostics, partial HUD/minimap composites, exposure loss, bright blobs, stalls, bad grades, early darkness, and budget regressions fail explicitly. |

## Verification record

Revalidated on 2026-07-21 using a clean, isolated macOS arm64/Metal build:

- `zanna check examples/games/ridgebound --diagnostic-format=json` completed
  with no diagnostics.
- `topology_probe.zia`, `traversal_probe.zia`, `state_probe.zia`, and
  `smoke_probe.zia` all reported `RESULT: ok`.
- No-jump movement produced zero blocked frames on every authored leg and the
  off-route swim leg, with
  worst observed dry ground separation of 0.19 m.
- All route/pad/lake topology thresholds passed; the nearest initial target is
  `Home Relay`, south of spawn.
- Full day, deep night, and compact captures retained scene structure, HUD, and
  minimap content with no Game3D diagnostic degradation.
- The complete canonical CTest invocation passed all 1,924 runnable tests; the
  focused Graphics3D run passed all 146 tests, including the uneven-heightfield
  regression and the Ridgebound Metal and software smoke paths.
- ASan and UBSan full-suite runs, runtime-surface audit, platform-policy lint,
  cross-platform smoke, and the isolated install completed successfully.
