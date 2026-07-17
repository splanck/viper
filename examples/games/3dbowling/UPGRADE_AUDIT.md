# Neon Lanes release-upgrade audit

This document maps the commercial-quality upgrade plan to the implementation
and records defects corrected while completing it. Zanna/runtime defects are
tracked separately in
[`known_zanna_issues/README.md`](known_zanna_issues/README.md); no Zanna source
is changed by this demo work.

## Plan implementation

| # | Upgrade | Implemented release evidence |
|---:|---|---|
| 1 | Distinct release identity | **Neon Lanes Championship** title treatment, version 2.0.0, original key art, and consistent in-game/window branding in `menu.zia`, `config.zia`, and `zanna.project`. |
| 2 | Commercial front end | Home hub for Quick Game, Local Match, Championship, Challenges, Practice, Cosmic Party, Ball Locker, Settings, Statistics, Help, Credits, and Quit in `menu.zia`. |
| 3 | Responsive menu/HUD layouts | Full 1280x720 and compact layouts, responsive pause/options/results panels, compact scorecard, and release-resolution captures in `release_visual_probe.zia` and `release_menu_probe.zia`. |
| 4 | Settings and accessibility | Oil pattern, graphics tier, local seats, three volume buses, release assist, difficulty, bumpers, left-handed controls/avatar placement, camera-shake strength, replay, high contrast, large HUD, reduced motion, and reduced flash. |
| 5 | Persistent save migration | Version-four preferences, progression, challenge medals, achievements, equipment, and career statistics with bounded/defaulted migration in `menu.zia`. |
| 6 | Dressed bowling venue | Three visible lanes, regulation boards, gutters, pinsetter, deck marquee, scoring displays, ball returns, lounge furniture, spectators, neon strips, and adjacent racks in `lane/lane.zia`. |
| 7 | Release lighting/material pipeline | PBR materials, glossy oil-aware boards, cubemap reflections, five-light venue rig, cosmic theme, cascaded shadows, SSR/TAA capability fallbacks, bloom, tonemap, grade, and Performance/Cinematic tiers. |
| 8 | Animated player presence | Time-based procedural bowler with stance tracking, charge, release, follow-through, celebration, player colors, handedness, reduced-motion behavior, and a bounded side-of-lane footprint in `presentation/bowler.zia`. |
| 9 | Broadcast camera direction | Approach, ball-follow, pin-deck settle, overhead inspection, title orbit, strike cinematic, and configurable impact shake in `engine/camera.zia`. |
| 10 | Highlight replay | Bounded scene recorder with actual ball and ten-pin positions/quaternions, live-scene hiding, replay banner, skip control, incomplete-data fallback, and replay preference in `presentation/replay.zia` and `engine/game_scoring.zia`. |
| 11 | VFX and celebrations | Crash sparks, dust, skid marks, gutter splash, fireworks, light flashes, strike/spare/turkey/gutter/perfect sequences, and queued achievement toasts. |
| 12 | Complete audio pass | Original procedural menu/game music, venue ambience, spatial roll/crash/gutter sounds, crowd reactions, UI navigation feedback, release/strike/spare/perfect cues, and result stings. |
| 13 | Skill-based release mechanic | Independent power lock and accuracy confirmation, bounded confirmation timeout, three exact assist levels, physical launch error, keyboard/gamepad input, and live HUD timing strip in `gameplay/release_meter.zia`. |
| 14 | Aiming and shot education | Separate stance and target boards, narrow predicted delivery corridor, error-width rails, pocket target, live curve/release meters, speed/release/entry-angle telemetry, and pocket-quality feedback. |
| 15 | Lane-condition gameplay | House/Sport/Dry patterns, real gutters, backend hook, lateral friction, a 13-by-24 oil wear/carry grid, live wear percentage, and authored-palette oil map. |
| 16 | Tactical equipment | Six data-driven balls with mass, speed, hook, control, oil fit, family, color, descriptions, score unlocks, per-player selection, and Ball Locker comparison bars. |
| 17 | Regulation scoring and broadcast HUD | Ten-frame scoring including correct tenth-frame bonus racks, four-seat score state, named leave analysis, full scorecard, standings, and mode objectives. |
| 18 | Local multiplayer | Two-to-four-player hot seat, named/color-coded profiles, per-seat ball choice, round-robin handoff, completed-player skipping, ties, standings, and faithful rematches. |
| 19 | Championship tour | AI plans through a pure difficulty/career model and launches through the same delivery, physics, pinfall, and scoring path as players, with Riley Chen/Mateo Cruz/Nova King tiers, visible tour-point thresholds, win bonus, and Tour Victory achievement. |
| 20 | Challenge tour | Pocket Master, Spare School, and Clutch Tenth with distinct targets, persistent bronze/silver/gold upgrades, results messaging, and medal achievement. |
| 21 | Practice Lab | Free bowling plus repeatable 7-10, 10-pin, and 3-6-10 racks, conversion feedback, split replay, automatic pinsetter reset, and no forced session end. |
| 22 | Cosmic Party variants | Cosmic lighting plus Strike Rush, gutter-penalized Low Ball with ascending ranking, and Target Bowling scored from pins, release precision, and pocket angle. |
| 23 | Progression and statistics | Best game, games, pinfall, strikes, spares, gutters, tour points, six ball unlocks, eight inspectable achievements, medal state, and safe career reset confirmation. |
| 24 | Onboarding and player guidance | Contextual first-game tutorial, persistent mode-objective ribbon, full How to Play page, control tables, oil/hook coaching, Credits, and README controls. |
| 25 | Release packaging and evidence | Package name/identifier/icon/assets/license metadata, original 1280 key art/1024 lane texture/512 icon, README, cross-platform no-build probe runners, deterministic system probes, and visual captures. |

## Long-term challenge and stability pass

The follow-up plan has 34 implementation requirements. This table records the
shipping evidence rather than restating recommendations as future work.

| # | Implemented requirement | Evidence |
|---:|---|---|
| 1 | Two-stage delivery | `ReleaseMeter` locks power, independently cycles accuracy, and times out after 1.10 seconds. |
| 2 | Independent setup controls | A/D or left stick moves stance; arrows or D-pad move target; Q/E or bumpers shape hook. |
| 3 | Shared pure launch solver | `gameplay/delivery_model.zia` feeds physical throws and the visual corridor from identical inputs. |
| 4 | Bounded human assistance | Pro/Standard/Full recover 8%/38%/72%; control adds at most 18%; AI passes zero assist. |
| 5 | Non-automatic center line | The trajectory matrix permits at most one center strike across five seeds. |
| 6 | Physical equipment sidegrades | Speed, hook, control, oil fit, and cover family affect motion; no catalog entry is globally best. |
| 7 | Live two-dimensional oil | A bounded 13-by-24 grid records same-board wear and at most two carry-down cells. |
| 8 | Seeded legal racks | Match/player/frame/rack-derived seeds produce bounded difficulty-specific jitter and repeatable practice racks. |
| 9 | Persistent line setup | Stance, target, hook, and ball survive turn changes; R resets only the setup line. |
| 10 | Spare release | F/gamepad X reduces hook and axis by 82% and announces `SPARE RELEASE`. |
| 11 | Recalibrated opponents and modes | AI bands, pocket line, medal thresholds, unlocks, and party scoring use the new physics path. |
| 12 | Order-independent impacts | `ImpactBatch` scans every event before admitting pin cascades; forward/reverse masks match. |
| 13 | Physical-only pinfall | Collision registration wakes/labels contact; orientation, height, or sustained displacement awards pinfall. |
| 14 | Live pin orientation | Rendering uses each physics body's quaternion, including replay samples. |
| 15 | Angular settling | Both linear and angular quiet must remain below thresholds for the configured dwell. |
| 16 | Power-dependent carry | Smooth launch momentum and bounded response keep minimum-power center shots below seven pins. |
| 17 | Safe maple asset use | Material and HUD decodes are isolated; Metal uses procedural boards while NL-ZANNA-003 remains. |
| 18 | Readable venue hierarchy | Warm lane/deck key, cool audience fill, darker side lanes, readable pins, and restrained bloom. |
| 19 | Bounded bowler presence | The time-based avatar sits beside the line and leaves ball, target, pocket, and meters uncovered. |
| 20 | Readable ball rotation | Every catalog ball keeps finger holes and gains a contrasting rolling stripe. |
| 21 | Live venue scoring displays | Monitor texture changes with player, frame, and score; adjacent activity stays lower contrast. |
| 22 | Narrow prediction ribbon | Dashed center prediction and error-width rails replace the oversized wire sphere. |
| 23 | HUD hierarchy | Score leads; stance/target/release follows; coaching and oil telemetry remain tertiary. |
| 24 | Complete accessibility propagation | High contrast, large HUD, reduced motion, reduced flash, and bumpers affect their real systems. |
| 25 | Scene-accurate replay | Samples contain one ball and ten pin transforms; fresh live bodies are hidden during playback. |
| 26 | Measured visual feedback | Dust and 8/24/48 crash particles scale from delivery/impact energy; fireworks require strike outcomes. |
| 27 | Layered audio feedback | Soft/medium/hard roll and pin layers coexist with distinct gutter, sweep, pinsetter, crowd, and UI cues. |
| 28 | True pause | Physics, meters, replay, effects, bowler, toast, camera, and flashes freeze; a neutral input frame is required. |
| 29 | Terminal shot watchdog | Non-finite, two-second no-progress, and twelve-second shots safely end once with the required toast. |
| 30 | Bounded world lifecycle | Replay/effect lists and world references clear before reconstruction; a 50-transition probe remains bounded. |
| 31 | Defensive schema-four saves | Every stored enum, count, mask, statistic, volume, ball, and career field is validated. |
| 32 | Central asset resolution | Repository, project, executable/package, and direct-relative candidates share one fallback/logger. |
| 33 | Resize and timestep independence | Camera-only aspect rebinds preserve worlds; zero height is safe; 30/60/144 Hz probes agree. |
| 34 | Runtime-defect isolation | NL-ZANNA-001 through -003 stay documented and shipping presentation uses stable geometry/opaque fallbacks. |

## Game defects found and corrected

- Rematch previously conflated a mode identifier with player count; explicit
  mode/count startup now preserves every rematch.
- Tenth-frame strikes and spares kept an empty/partial rack for bonus balls;
  frame completion and fresh-rack requirements are now separate, with an
  actual-physics integration probe.
- The first strike in a short tenth-frame game could also report as a spare
  because outcome helpers inferred state from unrelated previous rolls;
  ScoreKeeper now records explicit last-roll outcome flags.
- Celebration timers ran during sweep/reset and replays froze at their first
  sample; both now begin together in the post-pinsetter highlight phase.
- Celebration streak state was shared across seats, so three different
  players' strikes could announce a turkey; celebrations now consume the
  active scorecard's streak.
- AI/opponent rolls polluted the primary player's career totals, and a Low
  Ball gutter penalty counted as physical pinfall; career stats now record
  only Player 1's actual pins.
- Ordinary impacts ignored the camera-shake setting; all shake sources now
  apply its configured strength.
- The post-processing chain rebuilt every frame while Settings was open;
  quality application is now idempotent when the tier is unchanged.
- Ball reset removed the same physics body twice; body recreation now owns the
  single removal.
- Gamepad ball cycling conflicted with hook bumpers; ball cycling moved to the
  stick buttons.
- The full score status card overlapped the tenth-frame cell; the full layout
  now activates only when it fits and leaves a release-resolution gutter.
- Destructive career reset executed on the first press; it now requires a
  second explicit confirmation and disarms when selection changes.
- One-slot toasts discarded simultaneous achievement notifications; gameplay
  cues can replace immediately while achievements queue in order.
- The oil surface originally set opacity without selecting material blend
  mode. Correcting it to `AlphaMode = 2` exposed the intermittent Metal
  corruption recorded as NL-ZANNA-003, so the shipping path keeps the same
  glossy response on the boards and visualizes oil through the stable HUD map.
- Championship tiers existed but were invisible before launch; a dedicated
  tour page now shows the current rival, points, and next threshold.
- Achievements were only a numeric count; the career page now exposes every
  achievement and its locked/unlocked state.
- Menu navigation lacked feedback and results had no dedicated sting; UI and
  outcome audio now close those gaps.

## Verification constraints

All validation for this upgrade uses the existing Zanna binaries. Per the
explicit repository-work constraint, this work does not rebuild Zanna and does
not run the Zanna CTest suite.

## Prior release baseline

Validated on 2026-07-10 with the existing macOS arm64/Metal Zanna binary:

- `zanna check examples/games/3dbowling --diagnostic-format=json` completed
  with no diagnostics.
- Fourteen deterministic demo probes passed: release upgrade, menu flow,
  match modes, assets, asset rendering, trajectory, base smoke, aiming,
  overlay, title with and without post-processing, scene without
  post-processing, release visuals, and release menus.
- The native demo-only build produced an executable through
  `test_3dbowling_native_build.sh`.
- The tarball package dry run resolved the 2.0.0 metadata, icon, bundle
  identifier, executable, and packaged `assets/` tree successfully.
- The real project entry point opened on Metal and reached the branded title
  loop without a startup diagnostic.
- Scoped `git diff --check`, source-header, placeholder, and raw platform-macro
  scans were clean for `examples/games/3dbowling`.
- Repository-wide platform-policy lint completed in advisory mode; its listed
  findings are outside this demo and were left untouched.

The issue reproductions under `known_zanna_issues/` are evidence probes rather
than release gates: the two confirmed defects reproduce as documented, the
control case passes, and the intermittent material/overlay harness currently
renders all four expected regions.

The long-term challenge pass is validated separately through `run_probes.sh`
or `run_probes.cmd`; both use an existing Zanna executable and cannot build
Zanna or invoke CTest.

## Long-term pass verification

Validated on 2026-07-13 with `/usr/local/bin/zanna` on macOS arm64/Metal,
without rebuilding Zanna and without running CTest:

- All 27 gates selected by `run_probes.sh` passed. The Windows runner selects
  the same list and applies the same exit-status plus exact `RESULT: ok` rule.
- All 70 Zia files under the demo passed individual existing-binary
  `zanna check` validation with no diagnostics.
- Twelve useful-power pocket seeds produced eight strikes and twelve seven-plus
  leaves; five straight center seeds produced one strike; four poor releases
  produced zero strikes; minimum power knocked six pins.
- Repeating one delivery and rack seed reproduced launch X/Z velocity, the
  ten-bit knocked mask, and pin count. Forward and reverse synthetic collision
  batches produced identical impacted and physical-down masks.
- The two-stage meter, oil grid, AI planner, feedback tiers, pause/resume gate,
  watchdog, full-scene replay, 50-transition lifecycle stress, asset resolver,
  corrupt save migration, 30/60/144 Hz cadence, resize layout, accessibility,
  menu/mode flows, and release visual captures all reported `RESULT: ok`.
- Metal visual capture confirmed the NL-ZANNA-003 procedural-board fallback;
  the final frame retained the lane, prediction ribbon, HUD, monitor state,
  pins, and bounded bowler without the material/overlay corruption.
- POSIX runner syntax, full source headers, scoped whitespace/diff checks, and
  strict platform-policy lint for the demo completed cleanly.
