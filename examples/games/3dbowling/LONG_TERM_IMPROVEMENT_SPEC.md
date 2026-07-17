# Neon Lanes long-term gameplay and presentation specification

## 1. Summary and objective

This specification replaces the oversized automatic-strike corridor with a
readable, moderately challenging delivery model and brings the live venue,
feedback, replay, accessibility, and failure handling up to the standard set
by the title presentation.

The default experience must reward a repeatable pocket line, useful power, and
good release timing. It must not require simulation-level precision: a player
using Standard assist gets a visible error corridor and enough recovery to
learn from misses. Identical settings, inputs, rack seed, and fixed timesteps
must produce identical results.

## 2. Scope

In scope:

- `examples/games/3dbowling` gameplay, presentation, assets, save migration,
  documentation, and standalone probes.
- Demo-side use of already-published Zanna APIs.
- Keyboard and gamepad controls on macOS, Windows, and Linux.
- Existing-binary validation with `zanna check` and demo probes.

Out of scope:

- Zanna compiler/runtime/API changes, runtime C ABI changes, and IL changes.
- CMake, CTest, workflow, or package-format changes.
- A Zanna rebuild or execution of the Zanna CTest suite.
- Network services, downloaded content, or external dependencies.

## 3. Feature toggles

There is no master toggle. The delivery model is the game, so shipping both
old and new physics would double the tuning and test surface.

Player-facing bounded settings are:

| Setting | Stored type | Default | Values |
|---|---:|---:|---|
| `difficulty` | Integer | `DIFFICULTY_STANDARD` | Casual, Standard, League |
| `assist_level` | Integer | `ASSIST_STANDARD` | Pro, Standard, Full |
| `bumpers` | Boolean | `false` | Off, On |
| `reduced_motion` | Boolean | `false` | Off, On |
| `reduced_flash` | Boolean | `false` | Off, On |
| `large_hud` | Boolean | `false` | Off, On |
| `high_contrast` | Boolean | `false` | Off, On |
| `replay_mode` | Boolean | `true` | Off, On |

`difficulty` sets the base launch-error multiplier, rack variation, preview
width, and AI target bands. `assist_level` only recovers human timing error;
it never changes pin physics or converts a miss into a hit. Bumpers remain an
explicit accessibility option and default off.

## 4. Configuration

The implementation adds or replaces these exact tuning groups in
`config.zia`:

- Difficulty IDs: `DIFFICULTY_CASUAL = 0`, `DIFFICULTY_STANDARD = 1`, and
  `DIFFICULTY_LEAGUE = 2`.
- Separate setup bounds: `STANCE_MAX = 0.30`, `TARGET_MAX = 0.42`,
  `STANCE_SPEED = 0.42`, and `TARGET_SPEED = 0.46`, all in world-X metres or
  metres per second.
- Two-stage release: `POWER_NEEDLE_RATE = 0.78`,
  `ACCURACY_NEEDLE_RATE = 7.35`, `ACCURACY_NEEDLE_PHASE = 1.17`, and
  `RELEASE_CONFIRM_TIMEOUT = 1.10` seconds.
- Launch errors at zero raw accuracy:
  `RELEASE_ANGLE_ERROR_MAX = 0.030` radians,
  `RELEASE_SPEED_ERROR_MAX = 0.22`, and
  `RELEASE_AXIS_ERROR_MAX = 0.16`.
- Difficulty error scales: Casual `0.72`, Standard `1.00`, League `1.22`.
- Effective speed range: `THROW_SPEED_MIN = 2.35` and each ball's catalog
  maximum at full power. Power below `0.44` cannot receive the full carry
  multiplier; the carry peak is `0.68..0.88` normalized power.
- Physical pinfall: `PIN_DOWN_TILT_COS = 0.72`,
  `PIN_DOWN_CENTER_Y = 0.145`, `PIN_DOWN_DISPLACEMENT = 0.105`, and
  `PIN_DOWN_DWELL = 0.10` seconds. Collision impulse may wake and bias a pin,
  but may not directly mark it knocked down.
- Settling: linear speed at most `0.065`, angular speed at most `0.34`, and a
  continuous quiet dwell of `0.28` seconds.
- Rack variation: a deterministic integer seed, maximum position jitter of
  `0.0035` metres on Standard, `0.0020` Casual, and `0.0045` League. Variation
  is symmetric and never changes the legal rack order.
- Oil grid: `OIL_GRID_X = 13`, `OIL_GRID_Z = 24`, with bounded values in
  `[0,1]`. Track wear affects the sampled X/Z cell and carry-down affects at
  most the next two Z cells.
- Watchdogs: `ROLL_WATCHDOG_SECONDS = 12.0`,
  `ROLL_PROGRESS_TIMEOUT = 2.0`, and `ROLL_PROGRESS_EPSILON = 0.035` metres.
- Replay: one scene sample every two fixed steps, at most `REPLAY_MAX = 480`
  scene samples, storing ball position plus ten pin positions/orientations.
- Save schema: `SAVE_VERSION = 4` and new keys `difficulty`, `bumpers`,
  `reduced_motion`, `reduced_flash`, and `large_hud`.

No configuration value may be loaded without range validation. Invalid or
missing values use the defaults in this section and are written back on the
next normal save.

## 5. Technical requirements

### 5.1 Delivery and challenge

1. Power and accuracy use independent oscillators. Releasing the held throw
   control locks power; pressing throw again locks accuracy and launches. A
   `1.10` second timeout launches with the then-current accuracy so a lost
   button-up event cannot deadlock the turn.
2. A/D changes stance; Left/Right changes the target board. D-pad changes the
   target; left-stick buttons continue to cycle balls. Q/E or bumpers change
   hook. Space/gamepad A performs both release stages.
3. The launch solver accepts stance, target, normalized power, hook, raw
   accuracy, assist, difficulty, ball control, and oil fit. It returns launch
   X, lateral/forward speed, rev rate, axis rotation, and an uncertainty width.
   The solver is pure and is used by both the preview and the physical throw.
4. Standard assist recovers 38% of timing error, Full 72%, and Pro 8%.
   Ball control recovers at most a further 18%; neither can make effective
   accuracy equal 1 unless raw accuracy was 1.
5. A head-on center delivery is a visible hit but not a guaranteed strike. A
   pocket line at useful power can strike, while an otherwise identical poor
   release can leave pins or miss the pocket.
6. Ball `control`, `oilFit`, and `family` fields have physical effects.
   Control narrows launch uncertainty, oil fit changes front-lane skid and
   backend response, and family changes axis retention. Catalog entries remain
   sidegrades: no unlock may be best in speed, hook, control, and oil fit at
   once.
7. Oil is sampled in X and Z. Repeated shots on one line increase friction on
   that line and carry a smaller amount of oil down-lane. Resetting or changing
   a match restores the selected authored pattern.
8. Each fresh rack receives a deterministic seed derived from match seed,
   player, frame, and rack number. Replay and practice reset reuse the stored
   rack seed when the same rack is requested.
9. The previous stance, target, hook, and ball remain selected for the next
   turn. Only the release meters reset. A dedicated reset action returns stance,
   target, and hook to their neutral values while aiming.
10. Spare release toggles with F/gamepad X while aiming. It reduces hook and
    axis rotation by 82% for a straighter spare line; the HUD announces
    `SPARE RELEASE` while active.
11. Difficulty, AI lines, challenge thresholds, unlock scores, and party-mode
    bonuses are calibrated against the new physics. AI never calls a separate
    scoring shortcut.

### 5.2 Physical pins and deterministic state

12. Collision processing is two-pass. Pass one establishes whether any
    ball-pin contact occurred; pass two applies all eligible ball-pin and
    pin-pin impacts. Reordering a step's event list cannot change eligibility.
13. `registerImpact` may apply visual/audio strength and wake a body but cannot
    set the knocked flag. Pinfall is determined after physics from body
    orientation, center height, or sustained legal deck displacement.
14. Live pin meshes use the physics body's quaternion. A knocked pin is never
    replaced by an unrelated scripted 90-degree rotation.
15. Settling requires both linear and angular quiet for the configured dwell.
    A pin that re-accelerates resets its quiet timer.
16. Power changes pin carry through ball momentum and a bounded collision
    response; a minimum-power head-on shot may not knock all ten pins.

### 5.3 Appearance and feedback

17. The maple texture is bound only to 3D lane materials. HUD palette sampling
    uses a separately loaded image so the known Metal material/overlay aliasing
    issue is not reintroduced. Procedural boards remain the fallback.
18. The venue uses warm lane/deck key light, cool audience fill, readable pins,
    darker side lanes, and restrained bloom. Cosmic mode may override colors
    but not pin legibility.
19. The bowler is scaled and positioned so it cannot cover the ball, target,
    release meter, or more than 12% of the playfield. Approach, release, and
    follow-through interpolate from elapsed time rather than frame count.
20. Balls have catalog-specific core color plus a contrasting stripe or panel
    that visibly demonstrates rotation. Finger holes remain visible.
21. Venue monitors display current player, frame, and score rather than a
    fixed decorative image. Adjacent lanes and spectators remain lower contrast
    than the active lane.
22. The aim overlay is a narrow board ribbon with a dashed center prediction
    and translucent/error-width side rails. It must not use a giant wireframe
    target sphere.
23. HUD information hierarchy is: player/frame/score, stance/target/release,
    then coaching/telemetry. Large HUD increases gameplay text and meter sizes
    without covering the pocket.
24. High contrast changes pins and all setup indicators, not pins alone.
    Reduced motion disables camera shake and shortens cinematic/replay camera
    travel. Reduced flash replaces full-screen flashes with a border pulse.
25. Replay captures and renders the actual ball and all ten pin transforms.
    Live fresh-rack pins are hidden while replay samples are drawn.
26. Effects scale with measured impact and outcome: light dust for low power,
    crash debris for strong pocket contact, fireworks only for strike/turkey/
    perfect outcomes, and no opaque full-screen alpha fallback.
27. Audio selects soft/medium/hard roll and pin impact layers from launch speed
    and collision strength. Gutter, sweep, pinsetter, crowd, and UI cues remain
    distinct and respect all three volume buses.

### 5.4 Stability and lifecycle

28. Pausing freezes physics, release meter state, replay, celebration, bowler,
    particles, toasts, camera shake, and flash timers. Resuming cannot synthesize
    a release edge; the throw control must be released before charging resumes.
29. Every delivery ends. Non-finite ball state, no Z progress for two seconds,
    or twelve elapsed rolling seconds transitions to settling, records a zero
    if no legal pinfall occurred, and shows exactly `SHOT RESET: ball stalled`.
30. Match/title/restart transitions explicitly clear per-world references and
    bounded capture/effect lists before creating a new world. A 50-transition
    standalone stress probe must complete without a trap or unbounded list.
31. Save migration clamps every enum, number, boolean encoding, player count,
    ball index, medal, statistic, and bit mask. Corrupt settings never prevent
    title startup. Career reset remains double-confirmed.
32. Asset resolution tries repository-root, project-root, executable/package,
    and direct-relative candidates through one helper. A missing optional asset
    logs exactly `Neon Lanes: optional asset missing: <name>; using procedural fallback`
    once per asset and continues.
33. Camera aspect derives from current canvas width/height. Resize and compact
    layouts update without reconstructing the world. Trail sampling, menu
    animation, bowler motion, and oil wear are timestep-based and capped.
34. Runtime defects NL-ZANNA-001 through NL-ZANNA-003 remain isolated under
    `known_zanna_issues`. The shipping demo uses stable opaque/geometry-based
    fallbacks and must not depend on those defects being fixed.

## 6. Error handling

| Scenario | Required behavior/message |
|---|---|
| Missing optional image | Continue procedurally; log `Neon Lanes: optional asset missing: <name>; using procedural fallback` once. |
| Invalid saved setting | Clamp/default silently, keep the title usable, persist corrected value on next save. |
| Non-finite/stalled/over-time ball | End the delivery safely; HUD `SHOT RESET: ball stalled`; no score duplication. |
| Replay data incomplete | Skip replay and continue celebration/scoring; no user-facing error. |
| Zero-height render target | Retain the last valid aspect ratio and compact-layout state. |
| Audio unavailable | Continue silently with all gameplay and visual feedback intact. |
| Fixed-step backlog above cap | Drop excess accumulated time through the existing runtime cap; never loop without a bound. |

## 7. Tests

All tests are standalone Zia probes run through the existing Zanna binary.
They are not registered with CTest.

### Positive

- Given the same delivery inputs and rack seed, when two simulations run, then
  their launch vectors, knocked mask, and score are identical.
- Given a Standard pocket line across at least 12 rack/release seeds, when
  useful power and good accuracy are used, then it strikes 40-80% of trials and
  knocks at least seven pins in 80% of trials.
- Given two shots on the same oil cells, when the second is sampled, then its
  friction is higher and downstream carry-down is nonzero.
- Given an actual replay frame, when rendered, then the sample exposes one ball
  transform and ten pin transforms.

### Negative

- Given five rack seeds and a perfectly straight center line, when delivered
  at full useful power, then no more than one trial is a strike.
- Given minimum power on the center line, when the rack settles, then it never
  strikes and knocks no more than seven pins.
- Given a poor release with otherwise perfect pocket setup, when simulated,
  then at least half the tested timing phases fail to strike.
- Given collision events in forward and reverse order, when processed, then
  the impacted mask and knocked result are identical.
- Given a paused second-stage release, when time and input polling continue,
  then meter values and game state do not change.

### Edge and stability

- Given all enum/number settings below and above valid ranges, when loaded,
  then every getter returns its documented range and startup completes.
- Given a non-progressing or non-finite delivery, when its watchdog expires,
  then it reaches scoring exactly once with the required HUD message.
- Given 50 title/match/restart transitions, when executed, then capture/effect
  counts remain within configured maxima and no operation traps.
- Given 30, 60, and 144 Hz frame sequences covering the same fixed-step time,
  when release, trail, bowler, menu, and oil systems update, then gameplay
  outcomes match and bounded visual sample counts differ by at most one.
- Given missing key art and lane texture, when title and lane initialize, then
  procedural presentation remains usable and each missing asset logs once.

The bounded trajectory probe is the release gate. It must report every shot
and finish with `RESULT: ok`; a matrix in which all aligned shots strike is a
failure even if individual minimum-pin assertions pass.

## 8. Code references and exemplars

- Delivery state and fixed stepping: `engine/game_flow.zia`.
- Shared match state and save application: `engine/game_state.zia`,
  `engine/game_setup.zia`, and `menu.zia`.
- Published rigid-body quaternion access:
  `examples/apiaudit/graphics3d/physics3d_rotation_demo.zia`.
- Existing textured PBR material pattern: `examples/games/3dscene/game.zia`.
- Ball/pin/lane physics: `lane/ball.zia`, `lane/pins.zia`, `lane/lane.zia`.
- Replay and accessibility presentation: `presentation/replay.zia`,
  `engine/hud.zia`, `effects/celebrations.zia`, and `presentation/bowler.zia`.
- Existing standalone checks: `trajectory_probe.zia` and
  `release_upgrade_probe.zia`.

This demo-only change does not meet any ADR trigger: it adds no opcode,
grammar, verifier rule, cross-layer dependency, runtime C ABI, normative IL
reference, or workflow change.
