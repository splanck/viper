# Player Ratings Schema

## Goal

Define a player-talent model that is:

- Legible to the developer
- Flexible enough for realistic baseball outcomes
- Stable across multiple seasons
- Not overfit to one year of observed stats

The key requirement is that ratings represent underlying ability, not just a copy
of a stat line.

## Design principles

### 1. Separate talent from results

A player should not be "a .287 hitter" in the model.
They should be modeled with a particular contact, power, discipline, swing, and
batted-ball profile that tends to generate a range of plausible outcomes.

That distinction matters for:

- Regression to the mean
- Multi-season simulation
- Aging and development
- Injuries and fatigue
- Different results under different parks or defensive environments

### 2. Avoid one giant overall rating

Do not center the model around a single overall number and a few hand-tuned modifiers.

The engine should instead use a small number of meaningful skill groups whose effects
are traceable.

### 3. Ratings should map to engine steps

Each rating should exist because it influences a concrete part of the simulation:

- Take vs swing
- Whiff vs contact
- Ball in play profile
- Runner advancement
- Fielding conversion

If a rating does not map to a real simulation step, it probably does not belong in v1.

### 4. Split stable skill from situational state

Keep permanent or semi-permanent talent separate from in-game state like:

- current fatigue
- current confidence
- current injury penalty
- park effect
- platoon matchup effect
- weather effect

This avoids corrupting the talent model with transient conditions.

## Hitting

The hitting model should use a compact set of ratings with explicit meaning.

### Plate discipline family

- `zone_judgment`
  - ability to distinguish balls from strikes
  - influences take or swing decision quality

- `chase_resistance`
  - tendency to avoid expanding the zone
  - influences swing decisions on pitcher-friendly offerings

- `attack_aggression`
  - willingness to swing early or often
  - influences pitch-count behavior and contact volume

### Contact family

- `contact_vs_fastball`
- `contact_vs_breaking`
- `contact_vs_offspeed`

This is better than one generic contact rating if you want the pitch mix to matter later.
If the engine needs a smaller v1 model, start with:

- `contact`
- `whiff_avoidance`

### Quality-of-contact family

- `raw_power`
  - maximum damage ceiling on well-struck contact

- `game_power`
  - how often raw power shows up in realistic game conditions

- `barrel_skill`
  - frequency of optimal contact windows

- `hard_contact`
  - tendency to create strong exit velocity even outside ideal barrel outcomes

### Batted-ball profile family

- `ground_ball_tendency`
- `line_drive_tendency`
- `fly_ball_tendency`
- `pull_tendency`
- `center_tendency`
- `opposite_field_tendency`

These should not all be free-floating independent ratings. Internally they should likely
normalize into distributions so the engine never produces invalid combinations.

## Running

Baserunning and speed should exist as their own rating family, not a hitter subcategory.

- `speed`
  - raw foot speed

- `first_step`
  - burst out of the box and off bases

- `baserunning_instinct`
  - read quality on hits in play

- `steal_jump`
  - jump quality on stolen-base attempts

- `steal_skill`
  - conversion rate once an attempt occurs

## Pitching

Pitching should be built around how pitchers create bad swings, misses, weak contact,
and called strikes.

### Core family

- `stuff`
  - bat-missing quality

- `command`
  - ability to hit intended target areas

- `control`
  - walk suppression and strike throwing

- `movement`
  - contact suppression and damage suppression

- `stamina`
  - workload capacity before sharp degradation

### Shape and profile family

- `ground_ball_tendency`
- `fly_ball_tendency`
- `pop_up_induction`
- `hold_runners`

### Pitch mix

Pitchers should eventually expose real pitch arsenals.

Suggested pitch object fields:

- pitch type
- velocity band
- movement profile
- command profile
- usage tendency
- platoon effectiveness
- put-away effectiveness

For v1, you can simplify the arsenal into 2-4 abstract pitch buckets if needed,
but the data model should leave room for true pitch definitions.

## Defense

Defense should not be one global fielding number.

Use position-sensitive ability.

### Shared defensive skills

- `hands`
- `reaction`
- `range`
- `arm_strength`
- `arm_accuracy`

### Position-specific overlays

- catcher
  - `framing`
  - `blocking`
  - `pop_time`

- middle infield
  - `turn_double_play`

- infield corners
  - `hot_corner_reaction`
  - `scoop_skill`

- outfield
  - `route_efficiency`
  - `wall_play`

The fielding engine can use generic formulas, but inputs should still be position-aware.

## Handedness and splits

The model should treat handedness as a first-class baseball property, not a cosmetic field.

Required first-cut fields:

- batter bats: L / R / S
- player throws: L / R

Recommended split handling:

- Keep a baseline talent profile
- Apply split modifiers at resolution time
- Avoid duplicating the entire player skill tree per handed matchup

That keeps the data cleaner and easier to calibrate.

## Recommended data layout

Use three layers per player.

### 1. Identity

Stable metadata:

- name
- handedness
- positions
- age

### 2. Talent

Persistent ratings:

- hitting
- pitching
- defense
- running
- durability

### 3. Dynamic state

Mutable simulation state:

- fatigue
- current injury modifiers
- seasonal hot or cold variance if added later
- morale or role comfort if added later

This separation is important for multi-season simulation.

## Internal scale

Pick one internal scale and stick to it everywhere.

Reasonable choices:

- `0.0 to 1.0`
- `0 to 100`

For the engine, `0.0 to 1.0` is usually easier because it composes well with probability
weights and normalized blends.

For display, you can map it later to a 20-80 scouting scale or 1-100 ratings if needed.

## From ratings to outcomes

Do not hardcode direct stat outcomes off one rating.

Use staged resolution:

1. Pitch quality and location
2. Swing or take decision
3. Contact or miss decision
4. Quality-of-contact generation
5. Ball-in-play profile generation
6. Field and defense conversion

Each stage should use a limited subset of ratings.

That makes the engine:

- easier to debug
- easier to calibrate
- less fragile under later changes

## What not to include yet

The first backend version does not need:

- clubhouse personality ratings
- leadership
- popularity
- clutch
- abstract momentum traits
- hidden "winner" ratings

If later desired, those should be added only after the core baseball outcomes are stable.

## Suggested v1 minimal schema

If you need a compressed starting point, use this:

The minimal schema still needs to cover every rating family that the later v1
engines rely on. If you compress further than this, document the derivations
explicitly rather than silently inventing missing traits at resolution time.

- hitter
  - `zone_judgment`
  - `chase_resistance`
  - `attack_aggression`
  - `contact`
  - `whiff_avoidance`
  - `raw_power`
  - `game_power`
  - `hard_contact`
  - `ground_ball_tendency`
  - `line_drive_tendency`
  - `fly_ball_tendency`
  - `pull_tendency`

- pitcher
  - `stuff`
  - `command`
  - `control`
  - `movement`
  - `stamina`
  - `ground_ball_tendency`
  - `hold_runners`

- defender
  - `reaction`
  - `range`
  - `hands`
  - `arm_strength`
  - `arm_accuracy`
  - `turn_double_play`
  - catcher extras if needed

- runner
  - `speed`
  - `first_step`
  - `baserunning_instinct`

That is enough to build a believable first engine without collapsing everything into generic averages.

## Recommended follow-up after schema

Once this schema is accepted, the next step should be to define exactly which ratings are
consulted by each plate-appearance stage.

That mapping belongs in the plate-appearance planning doc.
