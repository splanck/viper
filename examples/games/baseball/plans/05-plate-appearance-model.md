# Plate Appearance Model

## Goal

Define a plate-appearance engine that is:

- Baseball-like
- Explainable
- Deterministic from a seed
- Rich enough to support realistic counts and pitch counts
- Still simple enough to implement early

The plate appearance is the core atom of the simulation. If this model is wrong,
the entire game will feel wrong.

## Design principle

The engine should be pitch-driven, but not fully physical.

That means:

- each pitch is a distinct decision and outcome
- the count matters
- pitch count matters
- pitch mix matters
- handedness matters

But the model should still resolve from ratings and controlled distributions rather than
continuous biomechanics.

## Recommended step model

Each pitch should resolve through these stages.

### Stage 1: Context assembly

Gather the resolution context:

- batter talent
- pitcher talent
- pitch repertoire
- handedness matchup
- count
- outs
- base state
- manager tactical intent
- pitcher fatigue
- park and weather hooks if active

This context object should be explicit and easy to log.

### Stage 2: Pitch selection

The pitcher chooses a pitch plan.

Suggested outputs:

- chosen pitch type
- target location class
- target intent

Suggested intent buckets:

- attack zone
- edge strike
- chase pitch
- waste pitch
- pitch around

For v1, this can be rule-based rather than AI-driven.

Examples:

- behind in count: more likely attack zone
- two strikes: more likely chase pitch
- open base with dangerous hitter: more likely pitch around

### Stage 3: Actual pitch execution

Convert the intended pitch into the actual thrown pitch.

Suggested outputs:

- actual location
- actual velocity band
- actual movement effectiveness
- actual command quality

This is where:

- pitcher command
- pitcher control
- fatigue
- pitch-specific command

all affect what the batter really sees.

### Stage 4: Batter read and swing decision

The batter decides whether to swing.

This stage should consider:

- zone judgment
- chase resistance
- attack aggression
- count leverage
- actual pitch location quality
- pitcher deception or stuff influence

Suggested result:

- `take`
- `protective_swing`
- `full_swing`

The distinction between protective and full swing is useful because it affects
foul-ball frequency and quality of contact.

### Stage 5: Called result or contact attempt

If `take`:

- called ball
- called strike
- hit by pitch

If swing:

- swing and miss
- foul ball
- ball in play

This stage should be influenced by:

- batter contact skill
- whiff avoidance
- pitcher stuff
- pitch type matchup
- count and protect mode

### Stage 6: Contact quality

If the pitch becomes ball in play, generate contact quality.

Suggested outputs:

- contact authority
- launch profile
- spray profile

Recommended intermediate categories:

- topped
- weakly hit
- solid
- barreled

Then convert to:

- exit velocity
- launch angle
- spray angle

This stage should use:

- raw power
- game power
- hard contact
- barrel skill
- movement suppression
- pitch execution quality

### Stage 7: Official plate-appearance termination

The pitch either:

- continues the count
- ends the PA without ball in play
- hands off to the ball-in-play engine

At this point, the engine should either:

- update the count and continue
- produce a non-contact `OfficialPlayResult`
- create a `PlayResolutionState` for batted-ball resolution

## Rule ownership inside the plate-appearance engine

Some rule triggers belong to the PA engine before the runner layer ever gets involved.

### Intentional walk

This should produce a distinct official classification, not merely a generic walk.

The PA engine should therefore emit:

- `walk_type = intentional`
- the same base-award behavior as a normal walk
- separate stat-credit fields for batter and pitcher

### Uncaught third strike

This trigger is owned by the PA engine.

On strike three, the engine should determine:

- whether the catcher secured the pitch cleanly
- whether first base occupancy and out count make the batter automatically out
- whether a live batter-runner should be created instead

If the batter-runner is live, the PA engine should seed `PlayResolutionState`
and hand off to the runner layer. The official result must still record a strikeout.

## Count effects

The count should materially influence behavior.

### Pitcher side

- 0-2 or 1-2
  - more chase pitches
  - higher strikeout intent

- 2-0, 3-1
  - more zone-filling
  - lower chase expectation

### Batter side

- hitter's counts
  - more aggressive attack swings
  - more damage-oriented swings

- two strikes
  - more protective swings
  - slightly lower quality of contact
  - higher foul-ball survival

Without count effects, the sim will miss a major source of real baseball texture.

## Pitch-count effects

Pitch count should matter immediately, even before detailed stamina systems.

Suggested first-cut uses:

- track total pitches thrown
- degrade command and stuff modestly as fatigue rises
- increase walk and hard-contact risk when fatigued

This is enough for v1. It already gives the manager game meaningful starter usage pressure.

## Handedness effects

At minimum, the plate-appearance model should apply handedness in:

- chase and take quality
- contact quality
- home-run authority
- pitch selection tendencies

Do not treat LHP vs LHB and LHP vs RHB as purely cosmetic.

## Non-ball-in-play end states

These should be first-class official outcomes:

- strikeout swinging
- strikeout looking
- walk
- intentional walk
- hit by pitch

That distinction matters for:

- text narration
- stats
- manager logic
- later presentation

## Foul-ball model

Foul balls need their own treatment.

Recommended handling:

- before two strikes, foul adds a strike
- with two strikes, foul extends the PA unless bunt rules later say otherwise
- protective swings increase foul-ball likelihood
- high-contact hitters foul off more borderline two-strike pitches

If fouls are not modeled carefully, strikeout and pitch-count distributions will drift badly.

## Example pitch-resolution skeleton

```text
resolve_pitch(context):
  plan = choose_pitch_plan(context)
  actual_pitch = execute_pitch(plan, context)
  swing_decision = choose_swing_decision(actual_pitch, context)

  if swing_decision == take:
    return resolve_taken_pitch(actual_pitch, context)

  swing_outcome = resolve_swing_contact(actual_pitch, swing_decision, context)

  if swing_outcome == miss:
    return swinging_strike
  if swing_outcome == foul:
    return foul_ball
  if swing_outcome == in_play:
    return build_batted_ball_context(actual_pitch, swing_decision, context)
```

## Suggested early formulas

Do not start with giant all-in-one probability tables.

Prefer chained probability estimates:

1. `P(swing)`
2. `P(contact | swing)`
3. `P(in_play | contact)`
4. `P(barrel | in_play)` and related contact classes
5. `batted_ball_shape_distribution`

This structure is much easier to tune than a single monolithic resolver.

## Recommended v1 simplifications

These simplifications are acceptable at first:

- umpire quality is fixed and neutral
- no catcher framing effect at first if it complicates the model too early
- pitch types can be abstracted into fastball, breaking, offspeed buckets
- no pitch sequencing memory beyond count and simple usage tendencies
- no explicit tunneling or pitch-recognition submodels

Those can all be layered in later if the basic baseball outcomes are sound.

## Failure modes to watch

### 1. Too many three-true-outcome plate appearances

If the model overweights walks, strikeouts, and home runs, the sim will feel synthetic.

### 2. Too little count variation

If most PAs resolve in the same number of pitches, the pacing will feel fake.

### 3. Contact quality detached from count

Great hitters should punish mistakes more in favorable counts.

### 4. Fouls not extending plate appearances realistically

This will distort both strikeout rate and pitch counts.

### 5. Pitch repertoire not mattering

If all pitchers reduce to generic stuff and control, they will feel interchangeable.

## What this model should produce by the end of v1

At the plate-appearance level, the engine should already support believable:

- pitch counts
- walk rates
- strikeout rates
- contact rates
- handedness effects
- count leverage effects
- fatigue pressure on pitchers

That is the foundation for everything else.
