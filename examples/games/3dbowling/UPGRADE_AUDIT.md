# Neon Lanes release-upgrade audit

This document maps the commercial-quality upgrade plan to the implementation
and records defects corrected while completing it. Viper/runtime defects are
tracked separately in
[`known_viper_issues/README.md`](known_viper_issues/README.md); no Viper source
is changed by this demo work.

## Plan implementation

| # | Upgrade | Implemented release evidence |
|---:|---|---|
| 1 | Distinct release identity | **Neon Lanes Championship** title treatment, version 2.0.0, original key art, and consistent in-game/window branding in `menu.zia`, `config.zia`, and `viper.project`. |
| 2 | Commercial front end | Home hub for Quick Game, Local Match, Championship, Challenges, Practice, Cosmic Party, Ball Locker, Settings, Statistics, Help, Credits, and Quit in `menu.zia`. |
| 3 | Responsive menu/HUD layouts | Full 1280x720 and compact layouts, responsive pause/options/results panels, compact scorecard, and release-resolution captures in `release_visual_probe.zia` and `release_menu_probe.zia`. |
| 4 | Settings and accessibility | Oil pattern, graphics tier, local seats, three volume buses, release assist, left-handed controls/avatar placement, camera-shake strength, replay toggle, and high-contrast pins. |
| 5 | Persistent save migration | Version-three preferences, progression, challenge medals, achievements, equipment, and career statistics with bounded/defaulted migration in `menu.zia`. |
| 6 | Dressed bowling venue | Three visible lanes, regulation boards, gutters, pinsetter, deck marquee, scoring displays, ball returns, lounge furniture, spectators, neon strips, and adjacent racks in `lane/lane.zia`. |
| 7 | Release lighting/material pipeline | PBR materials, glossy oil-aware boards, cubemap reflections, five-light venue rig, cosmic theme, cascaded shadows, SSR/TAA capability fallbacks, bloom, tonemap, grade, and Performance/Cinematic tiers. |
| 8 | Animated player presence | Procedural bowler with stance tracking, charge, release, footwork, celebration, player colors, and handedness in `presentation/bowler.zia`. |
| 9 | Broadcast camera direction | Approach, ball-follow, pin-deck settle, overhead inspection, title orbit, strike cinematic, and configurable impact shake in `engine/camera.zia`. |
| 10 | Highlight replay | Bounded shot-path recorder, post-pinsetter ghost-ball replay, trail, replay banner, skip control, and replay preference in `presentation/replay.zia` and `engine/game_scoring.zia`. |
| 11 | VFX and celebrations | Crash sparks, dust, skid marks, gutter splash, fireworks, light flashes, strike/spare/turkey/gutter/perfect sequences, and queued achievement toasts. |
| 12 | Complete audio pass | Original procedural menu/game music, venue ambience, spatial roll/crash/gutter sounds, crowd reactions, UI navigation feedback, release/strike/spare/perfect cues, and result stings. |
| 13 | Skill-based release mechanic | Oscillating power and centered accuracy timing, three assist levels, release error, keyboard/gamepad input, and live HUD timing strip in `gameplay/release_meter.zia`. |
| 14 | Aiming and shot education | Board-level line, predicted hook curve, pocket target, live aim/curve meters, speed/release/entry-angle telemetry, and pocket-quality feedback. |
| 15 | Lane-condition gameplay | House/Sport/Dry patterns, real gutters, backend hook, lateral friction, nine-zone oil breakdown/carry-down, live wear percentage, and authored-palette oil map. |
| 16 | Tactical equipment | Six data-driven balls with mass, speed, hook, control, oil fit, family, color, descriptions, score unlocks, per-player selection, and Ball Locker comparison bars. |
| 17 | Regulation scoring and broadcast HUD | Ten-frame scoring including correct tenth-frame bonus racks, four-seat score state, named leave analysis, full scorecard, standings, and mode objectives. |
| 18 | Local multiplayer | Two-to-four-player hot seat, named/color-coded profiles, per-seat ball choice, round-robin handoff, completed-player skipping, ties, standings, and faithful rematches. |
| 19 | Championship tour | Physics-driven AI on the same throw/scoring path, Riley Chen/Mateo Cruz/Nova King tiers, visible tour-point thresholds, difficulty growth, win bonus, and Tour Victory achievement. |
| 20 | Challenge tour | Pocket Master, Spare School, and Clutch Tenth with distinct targets, persistent bronze/silver/gold upgrades, results messaging, and medal achievement. |
| 21 | Practice Lab | Free bowling plus repeatable 7-10, 10-pin, and 3-6-10 racks, conversion feedback, split replay, automatic pinsetter reset, and no forced session end. |
| 22 | Cosmic Party variants | Cosmic lighting plus Strike Rush, gutter-penalized Low Ball with ascending ranking, and Target Bowling scored from pins, release precision, and pocket angle. |
| 23 | Progression and statistics | Best game, games, pinfall, strikes, spares, gutters, tour points, six ball unlocks, eight inspectable achievements, medal state, and safe career reset confirmation. |
| 24 | Onboarding and player guidance | Contextual first-game tutorial, persistent mode-objective ribbon, full How to Play page, control tables, oil/hook coaching, Credits, and README controls. |
| 25 | Release packaging and evidence | Package name/identifier/icon/assets/license metadata, original 1280 key art/1024 lane texture/512 icon, README, native/package checks, deterministic system probes, and visual captures. |

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
  corruption recorded as NL-VIPER-003, so the shipping path keeps the same
  glossy response on the boards and visualizes oil through the stable HUD map.
- Championship tiers existed but were invisible before launch; a dedicated
  tour page now shows the current rival, points, and next threshold.
- Achievements were only a numeric count; the career page now exposes every
  achievement and its locked/unlocked state.
- Menu navigation lacked feedback and results had no dedicated sting; UI and
  outcome audio now close those gaps.

## Verification constraints

All validation for this upgrade uses the existing Viper binaries. Per the
explicit repository-work constraint, this work does not rebuild Viper and does
not run the Viper CTest suite.

## Final verification

Validated on 2026-07-10 with the existing macOS arm64/Metal Viper binary:

- `viper check examples/games/3dbowling --diagnostic-format=json` completed
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

The issue reproductions under `known_viper_issues/` are evidence probes rather
than release gates: the two confirmed defects reproduce as documented, the
control case passes, and the intermittent material/overlay harness currently
renders all four expected regions.
