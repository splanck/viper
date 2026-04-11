# Batted-Ball And Fielding Model

## Goal

Define how a ball in play becomes:

- an out
- a hit
- an error
- a home run
- or a play that creates runner advancement pressure

This layer needs to feel like baseball without requiring full rigid-body simulation.

## Core principle

The model should be time-and-space aware, not animation-driven.

That means a batted ball should resolve from:

- contact quality
- launch profile
- direction
- park geometry
- defender starting position
- defender range and reaction
- throw quality

The output should be a baseball result, not just a visual trajectory.

## Required input from the plate-appearance engine

The ball-in-play layer should start from a compact `BattedBall` object.

Recommended first-cut fields:

- `contact_class`
  - topped
  - weak
  - solid
  - barreled

- `batted_ball_type`
  - ground_ball
  - line_drive
  - fly_ball
  - pop_up

- `exit_velocity`
- `launch_angle`
- `spray_angle`
- `hang_time_estimate`
- `batter_hand`
- `batter_speed_profile`
- `pitcher_contact_suppression_context`

The point is to hand off a credible physical-ish summary, not a fully simulated
ball object with spin and seam physics.

## Field representation

The first backend version should use a 2D park model with a small amount of
derived vertical information.

## Shared timing and space contract

The fielding and baserunning layers need one common timing model.

Recommended planning rule:

- distance is represented in one shared park-space convention
- time is represented in one shared simulation-time unit
- `hang_time_estimate`, `time_to_intercept`, `control_time`, `transfer_time`, and `throw_travel_time` all use that same time unit
- runner progress and fielder position calculations consume the same coordinate system

Recommended first anchors:

- 90-foot basepaths as the baseline running distance
- park coordinates derived from that same scale

The exact implementation can be feet or normalized park units, but the contract must
be singular across all race calculations.

### Coordinate system

Recommended convention:

- home plate at `(0, 0)`
- positive `y` points to center field
- negative `x` points toward first-base side
- positive `x` points toward third-base side

The exact handedness of the coordinate system does not matter as much as keeping
it consistent everywhere.

### Ballpark object

The park model should eventually support:

- left-field wall distance
- left-center wall distance
- center-field wall distance
- right-center wall distance
- right-field wall distance
- foul-territory class
- infield speed
- outfield speed
- optional wall-height classes

For v1, wall shape can be piecewise linear or represented as named depth bands.

### Defensive geometry

Each defender needs:

- default pre-pitch position
- position family
  - pitcher
  - catcher
  - first base
  - second base
  - third base
  - shortstop
  - left field
  - center field
  - right field

- positioning template hooks
  - standard
  - double-play depth
  - infield in
  - no-doubles depth later

The early engine can use fixed templates. The data model should still leave room
for alignment changes later.

The pitcher needs to exist in this geometry from v1, even before bunt-specific logic,
so the engine can handle comebackers, slow rollers, and weak contact near the mound
without inventing ad hoc exceptions.

## Resolution philosophy

The engine should resolve balls in play through candidate fielders and race windows.

It should answer questions like:

- Can anyone reach the ball in time for a catch?
- If not, who fields it first?
- How cleanly is it fielded?
- How quickly can the defense attempt a throw?
- What runner-advancement opportunities does the play create?

That structure produces baseball-like outcomes without forcing a frame-by-frame sim.

## Recommended resolution pipeline

### 1. Fair or foul determination

Use spray angle and ball type to determine whether the ball stays in play.

Must-have early cases:

- obvious foul balls
- fair line drives and fly balls
- fair or foul grounders near the lines
- pop-ups in foul territory

This stage should produce either:

- dead-ball foul result
- live fair ball
- catchable foul ball opportunity

### 2. Home-run and wall-ball check

For deep air balls:

- compare projected landing point against wall depth
- determine home run, wall hit, or warning-track catch opportunity

For v1:

- home run logic can be simple
- ricochet details can be abstracted
- wall height can be deferred unless it is needed to keep homer rates sane

### 3. Candidate fielder selection

The engine should identify plausible defenders based on:

- ball landing or intercept zone
- hang time
- defender start position
- defender reaction
- route efficiency
- position priority

Examples:

- shallow flare behind second: second baseman, shortstop, center fielder
- deep fly to gap: center fielder, left fielder or right fielder
- hard grounder in hole: shortstop or third baseman depending spray angle

Do not resolve directly from "zone 7 single" style tables if realism is the goal.

### 4. Air-ball catchability

For liners, flies, and pop-ups:

- compute each fielder's time-to-intercept estimate
- compare to hang time and landing geometry
- derive catch probability from the time margin

Key ratings:

- reaction
- range
- route_efficiency for outfielders later
- hands for completion reliability

This lets the engine distinguish:

- routine fly
- tough running catch
- diving or lunging chance
- no-play hit

### 5. Ground-ball and low-line-drive handling

For grounders and low liners:

- determine first-touch defender
- compute reaction and pickup quality
- decide clean field, bobble, deflection, or no-play
- compute transfer and throw window

Key ratings:

- reaction
- hands
- range
- arm_strength
- arm_accuracy
- double-play skill where relevant

Pitcher `ground_ball_tendency` and batter speed should materially affect this layer.

### 6. Error model

Errors should not be random decoration.

Error chances should appear at specific stages:

- catch completion error
- pickup or handling error
- transfer error
- throwing error

The scorer layer later decides official error charging, but the simulation should
know when a misplay occurred materially.

### 7. Defensive-control outcome

After the first successful fielding action, the ball-in-play layer should report:

- which defender controls the ball
- where on the field control occurred
- how long it took
- whether the ball is live and cleanly controlled
- what throw options are open

That output feeds the runner-advancement engine rather than directly forcing
all runners into predetermined destinations.

## Air-ball model details

The engine needs a better-than-binary air-ball model.

Recommended buckets:

- infield pop-up
- shallow outfield flare
- standard outfield fly
- deep drive
- wall ball

Important behaviors:

- infield pop-ups should almost never become hits absent confusion or error
- shallow flares should stress the gap between infield and outfield positioning
- deep drives should separate doubles, triples, wall outs, and home runs

## Infield-fly adjudication ownership

The fielding layer owns the trigger for the infield-fly rule because it has the
necessary information about fair territory, trajectory, likely fielder, and
ordinary effort.

The fielding result should therefore be able to report:

- `infield_fly_called`
- likely infielder under the ball
- ordinary-effort confidence

The runner layer then applies the rule consequences, and the official result builder
records the batter out without leaving the call to text or scorer-side reconstruction.

## Ground-ball model details

Grounders are responsible for a large share of baseball texture.

The v1 model should support:

- routine grounders turned into standard outs
- hard-hit grounders that get through
- slow rollers with infield race plays
- double-play balls
- ground balls that produce only one out due to speed or poor positioning

If this layer is weak, the game will not feel like baseball even if PA outcomes are decent.

## Line drives

Line drives need explicit handling because they sit between grounders and flies.

The early engine should distinguish:

- catchable liners
- one-hop liners through the infield
- gap liners that become doubles
- screaming liners directly at defenders

Without this, batting average on balls in play and extra-base-hit shape will drift.

## Suggested early formulas

Prefer derived margins over giant lookup tables.

Examples:

- `catch_margin = hang_time - time_to_intercept`
- `clean_field_margin = fielding_skill_blend - difficulty_score`
- `throw_margin = runner_time_to_base - defense_time_to_force_or_tag`

Those margins are easier to calibrate than monolithic zone outcome tables.

## Output to downstream systems

The fielding layer should not directly mutate permanent game state.

It should produce a structured result such as:

- `ball_status`
  - dead_foul
  - caught
  - fielded_live
  - over_wall_home_run

- `first_fielder`
- `control_location`
- `control_time`
- `misplay_flags`
- `rule_flags`
  - `infield_fly_called`
- `available_throw_targets`
- `advancement_pressure`

That result becomes input to `PlayResolutionState` and then the
`OfficialPlayResult`.

## V1 simplifications that are acceptable

- use fixed standard defensive positioning templates
- use approximate wall geometry
- do not simulate hops frame by frame
- do not model exact spin or slice
- abstract diving and sliding into catch or fielding probability bands
- keep cutoff and relay logic simple at first

These are acceptable as long as hit distribution and fielding texture remain believable.

## Failure modes to watch

### 1. Too many balls fall in

If fielders cannot convert enough routine balls into outs, offense will look wrong fast.

### 2. Outfielders erase too much contact

If gap balls are caught too often, doubles and triples disappear.

### 3. Infield hits are unrealistic

Slow runners should not beat out routine grounders, and elite runners should not
need miracle rollers every time.

### 4. Errors are either absent or cartoonish

Fielding mistakes should matter but not dominate.

### 5. Park dimensions do not matter

If all parks play the same despite different wall depths, the park model is too weak.

## Recommended next dependency

Once the fielding model is accepted, the next planning layer is:

- runner advancement timing
- tag and force resolution
- throws, holds, and send decisions
