# Fatigue, Injuries, And Availability

## Goal

Define how player workload from one game affects availability in subsequent games.

The single-game backend already models *within-game* pitcher fatigue: pitch counts
degrade command and stuff over the course of one outing (`plans/05 § Pitch-count
effects`). This plan introduces the *between-game* layer: pitcher recovery between
starts, position-player rest, day-to-day injuries, IL stints, and the daily
availability snapshot the manager command layer consults.

## Core principle

Within-game fatigue and between-game recovery are different things. They share the word "fatigue" but operate on different time scales and consult different state.

A real manager sim is largely about between-game state: who needs a day off, which pitcher is on his throw day, how many appearances has this reliever made in the last week, who's on the IL and when can they come back. This plan owns all of that.

A second principle: **hard eligibility and soft readiness are different things**.

The model needs:

- `availableForGame` — a hard legality flag
- `readinessTier` — a soft recommendation signal such as `fresh`, `available`, `tired`, `emergency_only`, `unavailable`
- optional availability notes or reasons

The legality layer consumes `availableForGame`. The manager layer consumes the full
availability snapshot. Without the soft readiness signal, resting players becomes
blind and position-player fatigue cannot influence decisions cleanly.

## Why this layer matters

Without between-game fatigue:

- starting pitchers can pitch every day
- relievers never need rest
- the bullpen has no usage pressure
- the lineup is identical every day
- pinch-hitting and resting players are pointless

This is the layer that turns the single-game engine into a manager sim. Pinching, resting, and bullpen management only become meaningful when fatigue persists across days.

## Availability model

Each player should have a daily `AvailabilitySnapshot` computed by the season layer.

Suggested first-cut fields:

- `availableForGame: Boolean`
- `readinessTier: String`
- `recommendedRest: Boolean`
- `reasonCode: String`
- `workloadSummary: String` or structured counters

Interpretation:

- `availableForGame == false` means the player is not legal to use
- `availableForGame == true` with `readinessTier == tired` means the player is legal but a rest recommendation exists
- `recommendedRest` is advisory, not a legality blocker, for most position players

This snapshot is what `plans/14` should expose to human and AI managers.

## Pitcher fatigue model

Two distinct fatigue concepts coexist:

### Within-game (already in `plans/05`)

Pitch counts degrade `command` and `stuff` over the course of one outing. This is consumed by the plate-appearance engine and resets when the pitcher leaves the game. **No change to the existing model.**

### Between-game (this plan)

Each pitcher carries a `PitcherRestState`:

- `daysSinceLastAppearance: Integer` — incremented each day, reset to 0 on appearance
- `lastAppearancePitchCount: Integer` — pitches thrown in the last appearance
- `pitchesLast7Days: Integer` — rolling sum
- `appearancesLast7Days: Integer` — rolling count
- `daysSinceLastStart: Integer` — for starters, distinct from `daysSinceLastAppearance`
- `currentStintPitchCount: Integer` — for active starters in the current rotation cycle
- `cumulativeSeasonPitches: Integer` — for season-long workload tracking

These advance once per `GameDay` in the daily loop (`plans/10 § Daily simulation loop`), regardless of whether the pitcher appeared.

### Recovery curves

Recovery is a function of role and workload:

- **Starting pitcher** — needs `rotationGap` days off after a start (default 4); recovers ~1 stamina point per day off
- **Long reliever** — needs 2 days off after a 30+ pitch appearance, 1 day off after a 15-30 pitch appearance, 0 days off after a sub-15 pitch appearance
- **Closer / setup** — can pitch on consecutive days; needs 1 day off after a 3rd consecutive appearance
- **Mop-up** — same as long reliever

The role-specific curves are tuned in `plans/06 § Calibration` follow-up runs. Initial values are placeholders.

### Cumulative season workload

A separate slow-moving fatigue accumulates over the season for starters: a starter who has thrown 200 innings by August has degraded `stamina` and slightly degraded `stuff` for the remainder of the season. This is what models late-season "dead arm" and is the difference between Phase 5 first-realism-pass calibration and a real manager sim.

For v1: start the cumulative degradation at 150 innings and apply a small linear penalty.

## Position-player fatigue

Position players accumulate fatigue per started game:

- `gamesStartedConsecutive: Integer` — resets when the player gets a day off
- `defensiveInningsLast7Days: Integer`
- `dayGameAfterNightGameMarker: Boolean` — set when yesterday was a night game and today is a day game

A `FatigueState` carries these. Starting a player when their `gamesStartedConsecutive` is high incurs a small penalty to fielding `reaction` and offensive `contact` (rates tuned in calibration).

For v1, position-player fatigue is a soft penalty, not an availability blocker. The manager *should* rest players who have started 7+ games in a row, but the engine does not force it.

### Catcher special case

Catchers are special: they play more demanding defense and recover more slowly. The default catcher rest cycle is "one day off per 5 games started". The model carries this as a separate `catcherStartsLast5Days` counter so the manager command layer can pinpoint catcher rest decisions.

## Recovery rules

Recovery applies on every `GameDay` in the daily loop, regardless of whether the player appeared:

- All `PitcherRestState.daysSinceLastAppearance` increment by 1
- All `daysSinceLastStart` increment by 1
- The 7-day rolling windows recompute based on the calendar, not based on increments
- `gamesStartedConsecutive` increments by 1 for any position player who started, resets to 0 for any who did not
- Off-days from `plans/10 § Off-days and rest` reduce all fatigue counters by an extra increment

Cumulative season workload does NOT decrement; it only grows. It resets at the start of a new season (handled by `13-player-development-and-aging.md` off-season events).

## Injury model

### Injury rolls

Injury exposure should be workload-weighted, not a flat "one roll per appearance"
rule. A one-batter relief appearance and a 110-pitch start are not comparable loads.

Use one injury exposure calculation per player-game appearance with modifiers from
workload:

- pitchers
  - pitches thrown
  - batters faced
  - days rest deficit
  - cumulative season workload

- position players
  - whether they started
  - defensive innings played
  - catcher workload
  - running stress later if needed

Base rates still need calibration, but the exposure unit should be workload-aware.

Suggested starting rates:

- base injury chance: ~0.5% per game appearance for position players
- base injury chance: ~0.8% per game appearance for pitchers before workload modifiers
- modifier: `Player.injuryProneness` from `plans/04 § Durability` raises the rate
- modifier: high cumulative workload raises the rate (a starter at 220+ innings has 2x the base rate)
- modifier: low rest raises the rate (pitching on 0 days rest spikes injury chance)

When an injury occurs, the engine rolls a severity:

- ~70% day-to-day (1-3 days)
- ~20% IL-10 (10-30 days)
- ~9% IL-60 (60-120 days)
- ~1% season-ending

These rates are placeholders; calibration tunes them against league injury rates.

### Severity tiers

- **Day-to-day** — `availableForGame = false` for the rolled number of days; player stays in their active compartment
- **IL-10** — triggers a `PlaceOnIL(player, IL10, returnDay)` transaction (per `plans/11 § Transaction types`)
- **IL-60** — triggers `PlaceOnIL(player, IL60, returnDay)`
- **Season-ending** — IL-60 with `returnDay > seasonEnd`; off-season activation

### In-game injuries (deferred)

For v1, injuries are rolled *between* games, not during a play. Real baseball has injuries on slides, collisions, and HBPs — that's a richer model that belongs in a v1.5 plan.

### Injury proneness drift

`Player.injuryProneness` is mostly stable but drifts upward slowly with age and with prior IL stints. This drift is owned by `13-player-development-and-aging.md`.

## Availability snapshot

The daily `AvailabilitySnapshot` is computed once per player at the start of each
game boundary:

- at the start of a normal game day
- again between games one and two of a doubleheader

The hard flag is:

```
availableForGame = (
    not on IL-10 or IL-60
    AND day-to-day injury counter == 0
    AND not in minors
    AND (for pitchers) pitcher availability rules pass
)
```

`readinessTier` and `recommendedRest` are derived from fatigue and workload state.
Examples:

- healthy everyday player, moderate usage -> `available`, `recommendedRest = false`
- catcher with 5 straight starts -> `tired`, `recommendedRest = true`
- reliever used three straight days -> `unavailable`

For doubleheaders, the snapshot is recomputed *between* games one and two so a
pitcher used in game one is unavailable for game two.

The manager command layer (`14-manager-command-layer.md`) reads the snapshot. The
legality layer only enforces `availableForGame == true`; the readiness fields guide
choices without over-constraining the manager.

The single-game `LineupCard` validation extends to check the hard flag (see
`plans/11 § Legality checks`).

## Cross-references

This layer feeds:

- **`plans/11 § Pitcher availability windows`** — the `daysSinceLastStart` and back-to-back rules above are exactly the windows plan 11 references
- **`plans/11 § Transaction types`** — IL placements trigger `PlaceOnIL` and `ActivateFromIL` transactions automatically when injuries are rolled
- **`plans/14 § Decision context types`** — every manager-decision context carries the availability snapshot for relevant players
- **`plans/13 § Durability changes`** — long-term injury proneness drift originates here; aging plan applies the slow drift

It reads from:

- **`plans/04 § Durability`** — `fatigueResistance` and `injuryProneness` ratings as inputs
- **`plans/05 § Pitch-count effects`** — within-game fatigue model is the baseline this plan extends
- **`plans/10 § Daily simulation loop`** — every fatigue/recovery update happens at a known step in the daily loop

## Persistence

All fatigue and injury state is part of the season-level persistent state (`15-save-format-and-persistence.md`):

- per-pitcher rest state
- per-position-player fatigue state
- IL placements with return dates
- day-to-day injury counters
- cumulative season workload counters

A loaded save must restore these byte-identically; pitchers come back with the exact same rest counters they had when saved.

## What to defer

For v1:

- workload-based velocity decay over a season (different from stamina)
- role-specific injury distributions (elbow vs shoulder vs back vs hamstring)
- in-game injuries during a play (HBPs, collisions, slides)
- post-injury rehab assignments and minor-league rehab starts
- soft-tissue injuries from cold weather / day-night swings beyond the basic marker
- workload-based long-term durability degradation beyond the late-season "dead arm" model
- pitching coach influence on recovery
- "tweaks" — sub-injury performance dips that don't trigger IL

## Failure modes to watch

### 1. Pitchers pitching on illegal rest

The classic bug: the manager command layer offers a pitcher whose `daysSinceLastAppearance == 0` because the `AvailabilitySnapshot` wasn't recomputed after yesterday's game. Test: simulate two consecutive days, verify yesterday's starter is not in today's available pool.

### 2. Injury accumulation runaway

If injury rates are tuned too high or recovery never fires, every player ends up on the IL by midseason. Calibration check: average IL stints per team per season should land in a realistic band (modern MLB: ~25-35 IL stints per team per year).

### 3. IL never clearing

If the IL activation logic checks `>` instead of `>=` on the return day, players never come off. Test: place on IL-10 on day D, attempt activation on D+10, expect success.

### 4. Off-day fatigue not applied

If the daily loop skips fatigue/recovery updates on off-days, off-days don't actually rest anyone. Test: a pitcher with `daysSinceLastAppearance == 2` after an off-day should read `3`, not stay at `2`.

### 5. Doubleheader pitcher availability bug

If `availableForGame` is computed once per day instead of once per game, the same pitcher appears in both halves of a doubleheader. Test: pitch a starter in game one of a doubleheader, attempt to start them in game two, expect rejection.

### 6. Day-to-day injury counter underflow

If the day-to-day counter isn't clamped at zero, a player who never gets injured eventually has a "negative" injury count that confuses the availability check. Always clamp at zero.

### 7. Catcher rest never enforced

Without the catcher-specific counter, the manager rests catchers via the generic `gamesStartedConsecutive` rule, which is too lenient. Catchers need their own counter.

### 8. Cumulative workload reset between seasons

`cumulativeSeasonPitches` must reset to 0 at the start of a new season (handled by `plans/13 § Off-season events`). If it doesn't reset, every starter starts year two with a "dead arm" penalty already applied.

## Recommended next dependency

Once fatigue, injuries, and availability are accepted, the next planning layer is `13-player-development-and-aging.md`. The injury proneness drift, the season-end workload reset, and the durability changes between seasons all live in plan 13.
