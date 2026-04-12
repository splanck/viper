# Player Development And Aging

## Goal

Define how player ratings (from `plans/04 § Recommended rating families`) evolve between seasons.

The single-game backend treats player ratings as constants. That's correct for one game and acceptable for one season. For multi-season play, ratings must evolve: young players develop, prime players plateau, older players decline. Without this, every season is the same season.

## Core principle

True talent ratings are slowly-moving truth. Observed stats (BA, ERA, OPS) are noisy reflections of the underlying truth. The talent itself moves between seasons; observed stats move within seasons.

Age should be derived from `date_of_birth` plus the current simulation date. The
engine should not literally increment every player's age by 1 on the same off-season
tick and store that as mutable truth. Development and aging rules should evaluate
against a chosen season reference date, such as Opening Day.

This is the deeper version of `plans/04 § Design principle § 1. Separate talent from results`. Plan 04 made the case that ratings ≠ stats. This plan makes the case that ratings *change*, but on a different time scale than stats fluctuate.

## Why this layer matters

Without aging and development:

- a multi-season simulation feels like the same season repeated
- prospects never become stars
- veterans never decline
- players with "ceiling vs floor" become indistinguishable
- there's no reason to scout, draft, or develop minor-leaguers
- the long-term franchise meta-game has no texture

Plan 04 already anticipates this layer (`plans/04 § Design principle § 1`):

> A player should not be "a .287 hitter" in the model. They should be modeled with a particular contact, power, discipline, swing, and batted-ball profile that tends to generate a range of plausible outcomes.

That distinction matters specifically for: regression to the mean, multi-season simulation, aging and development, injuries and fatigue, different results under different parks. This plan owns the "aging and development" piece.

## Aging curves

Each rating has an aging curve — a function from age to multiplier on the rating's base value. Curves vary by skill type because not all skills age the same way:

### Hitting

- **Contact / whiff avoidance** — peak ~26-29, slow decline; hitters retain bat-to-ball skill into their mid-30s
- **Raw power / game power** — peak ~27-30, faster decline after 33; power is the first thing to go for most hitters
- **Plate discipline** — peak ~28-32, very slow decline; this is the most stable hitting skill
- **Speed / first step** — peak ~24-26, steep decline; speed leaves earliest

### Pitching

- **Stuff** — peak ~25-28, decline after 31; raw velocity falls with age
- **Control** — peak ~28-32, slow decline; can even improve into early 30s
- **Command** — peak ~28-33, slowest decline; veteran pitchers compensate for lost stuff with command
- **Stamina** — peak ~24-28, decline after 30; older pitchers throw fewer pitches per outing
- **Movement** — peak ~26-30, slow decline; tied loosely to stuff but more stable

### Defense

- **Reaction** — peak ~24-26, steep decline; first reaction is athleticism
- **Range** — peak ~24-26, decline after 28; range is the second thing to go (after speed)
- **Hands** — peak ~26-30, slow decline; soft hands persist
- **Arm strength** — peak ~24-27, decline after 30
- **Arm accuracy** — peak ~28-32, slow decline; experience compensates

### Aging curve shape

Each curve is parameterized by:

- `peakAge: Integer` — the age at which the rating is at 100% of its base
- `developmentRate: Float` — how fast the rating climbs from age 18 to peak
- `declineRate: Float` — how fast the rating falls from peak to age 40
- `lateDeclineRate: Float` — accelerated decline after age 35

The default curve type is a piecewise-linear function: linear climb from age 18 to `peakAge`, flat at peak for ±2 years, linear decline after peak.

For v1, all players of the same role share the same curve shape but different parameters. Per-player curve customization is deferred.

## Development model

Young players develop based on their *talent ceiling*, not their current rating.
Talent ceiling is part of the latent player profile defined in `plans/04` and
represents where the player's skills can peak if development goes well.

### Development tick

Once per off-season, the development engine runs for every player under age 27 on the
next season's reference date:

1. Compute the gap between current rating and ceiling for each skill
2. Move the current rating partway toward the ceiling (~15-25% of the gap by default)
3. Add modest noise (±5% of the move)
4. Clamp to legal rating ranges

Players with high ceilings at 18 develop into stars by 24-26. Players with low ceilings never improve much.

### Development variance

Real prospect development is highly variable. Two players with identical ceilings can have very different career trajectories. Variance is modeled by:

- a per-player `developmentVarianceTrait` that scales the noise on each tick
- "breakout" events: a small chance per season of an unexpectedly large development jump (deferred for v1; placeholder)
- "stalled" events: a small chance per season of zero development for that year (deferred for v1)

### Development ceiling drift

Talent ceilings themselves can drift slightly over time — a player whose ceiling is set at age 18 isn't guaranteed to reach exactly that ceiling. For v1, ceilings are static. Drift can be added later if calibration shows prospects are too predictable.

## Position transitions

As athleticism declines, players move to less demanding positions:

- catcher → 1B / DH
- shortstop → 2B / 3B
- 2B / 3B → 1B / DH
- center field → corner outfield → 1B / DH

Position transitions are triggered when defensive ratings (`reaction`, `range`) fall below role-specific thresholds. The transition itself is deferred to v1.5 — for v1, players keep their listed position throughout the season and the model is the *trigger*, not the move.

The plan owns the *concept* of position transitions so future code knows where to hook them in.

## Durability changes

`Player.fatigueResistance` and `Player.injuryProneness` from `plans/04 § Durability` drift with age:

- **Fatigue resistance** — peak ~26-28, slow decline; younger players bounce back faster
- **Injury proneness** — slowly increases with age; rises more sharply after 32
- **Prior IL stints accelerate the drift** — a player who had two IL-60 stints in a season starts the next year with elevated injury proneness

Drift values are small per year (~1-3% adjustments) but compound across seasons. By age 36, a previously durable player should have noticeably elevated injury risk.

## Off-season events

The off-season is a single calendar event that runs after the regular season (and any
playoffs) end and before the next season starts. It performs:

1. **Season-reference age refresh** — advance the calendar to the next season reference date and recompute age-derived curves from each player's `date_of_birth`
2. **Development tick** — every player under 27 runs through the development model
3. **Durability drift** — every player's `fatigueResistance` and `injuryProneness` adjust based on age and prior-season workload
4. **Cumulative workload reset** — `cumulativeSeasonPitches` and `defensiveInningsLast7Days` from `plans/12` reset to 0
5. **Injury state reset** — players on the IL at season's end recover (IL stints reset); season-ending injuries may carry over (IL-60 placements with return dates past the season end)
6. **Career progression hooks** — refresh any age-band labels or service-time-derived tags if the game tracks them later (service-time logic itself is deferred)
7. **Retirement check** — players above a configurable age (default 40) and below a performance threshold may retire (deferred to v1.5; for v1, no players retire)
8. **Off-season report** — emit a season summary with per-player development/decline deltas

The off-season event is *one* atomic transition. Save state captures the season-end snapshot pre-off-season; load can replay the off-season deterministically.

## Multi-season testing harness

The aging and development model needs its own calibration harness, parallel to `plans/06`:

- **Trajectory test** — sim a known player from age 18 to age 40, plot rating vs age, verify the curve shape
- **Variance test** — sim 1000 copies of the same prospect, verify the distribution of outcomes is reasonable (not all identical, not wildly random)
- **Star prospect test** — sim a high-ceiling 19-year-old over five years, verify they become a star
- **Bust prospect test** — sim a low-ceiling 19-year-old, verify they never become a star
- **Veteran decline test** — sim a 32-year-old star over five years, verify gradual decline
- **Late-career test** — sim a 38-year-old, verify steeper decline

These tests live alongside the existing `calibration_probe.zia` and run on demand.

## What to defer

For v1:

- mid-season position changes
- hot/cold streaks within a season
- "breakout year" mechanics (sudden jump in performance)
- per-player customized curve shapes
- "talent ceiling drift" — ceilings are static
- pitcher type transitions (starter → reliever as stamina drops)
- mental aging (clutch ratings, leadership, etc.)
- park-driven development (pitchers in pitcher's parks developing differently)
- injury history beyond IL stint counts
- coaching influence on development
- "developmental year" / lost season due to injury rehab

## Failure modes to watch

### 1. Every player declining at the same rate

If aging curves are too uniform, every player ages identically and the variance that makes a real franchise interesting disappears. The trajectory test catches this.

### 2. Fountain of youth

If aging is omitted or ratings are clamped to peak values, no one declines. Veterans should visibly slow down. Late-career test catches this.

### 3. Prospects never developing

If development rates are too conservative, every prospect tops out at 80% of their ceiling and no one becomes a star. Star prospect test catches this.

### 4. Wild prospect variance

If development variance is too high, prospects bounce around year to year and the model loses predictive value. Variance test catches this.

### 5. Cumulative workload not resetting

If `cumulativeSeasonPitches` from `plans/12` doesn't reset in the off-season, year-two starters begin the season with a dead-arm penalty. Test by simulating two seasons and inspecting the year-two starting pitch counts.

### 6. IL stints persisting through the off-season

If a player with an IL-10 stint at season's end is still on the IL in spring training, the off-season reset failed. The off-season clears all IL-10s and IL-60s with return dates inside the off-season window.

### 7. Aging applied mid-season

The aging tick should run *only* in the off-season event. If it runs mid-season (e.g., on player birthday), ratings change unexpectedly during the season. Always batch the tick at the off-season boundary.

### 8. Season-reference status changes firing on arbitrary dates

Any age-band or service-time-derived labels used by downstream systems should update at
the same season boundary, not on arbitrary mid-season dates.

### 9. Saved development state doesn't match replayed state

If the development RNG stream isn't seeded deterministically from the season seed, replaying an off-season produces different development outcomes. The off-season event must use a separate, deterministic RNG stream derived from the season seed.

## Recommended next dependency

Once player development and aging are accepted, the next planning layer is `14-manager-command-layer.md`. The aging-aware decision contexts (e.g., "this veteran needs more rest", "this prospect should start more often") need a formal manager command contract to be exposed cleanly.
