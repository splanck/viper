# Roster And Transaction Rules

## Goal

Define the legal season-level personnel state for each club: who is on the active
roster, who is unavailable, who is in the callup pool, and how players legally move
between those compartments.

The single-game `LineupCard` from `plans/01 § LineupCard` is the *daily* expression of
personnel decisions. This plan defines the *season-long* compartments those daily cards
are drawn from, and the typed transactions that move players between compartments.

## Core principle

Season roster state and daily lineup state are not the same thing.

- Roster compartments answer: "is this player eligible to appear for this club today?"
- The lineup card answers: "is this eligible player starting, on the bench, or unused today?"

Transactions move players between season compartments. Building today's starting nine
or choosing today's starter is **not** a roster transaction; it is a daily lineup-card
decision through the manager layer.

## Why this layer matters

Without season compartments, a manager sim collapses into "pick any 9 players from the
organization every day." Real management depends on scarcity and availability:

- who is on the active roster
- who is on the IL
- who can be called up
- how many pitchers the club is carrying
- who is rotation depth versus bullpen depth

The single-game `LineupCard` from `plans/01` already models batting slots and an active
pitcher. This plan does not redefine the daily card. It defines the *pool* the card is
drawn from and the rules that govern how that pool changes day to day.

## Roster compartments

A `RosterCompartment` is one of:

- **Active hitters** — position players currently on the MLB active roster and eligible to start or sit on the bench today
- **Active pitchers** — pitchers currently on the MLB active roster and eligible to start or relieve today, subject to availability rules
- **IL-10** — 10-day injured list; minimum 10 days from placement to activation
- **IL-60** — 60-day injured list; minimum 60 days; 40-man implications are deferred
- **Minors callup pool** — players not on the active roster but available for promotion
- **Designated for assignment** — deferred to v1.5+

A player is in exactly one compartment at any moment.

### Active role tags

Compartment membership alone is not enough. Active players also carry **role tags**
that guide daily lineup construction but do not change hard eligibility:

- hitter role tags
  - regular
  - platoon
  - utility
  - backup catcher
  - bench bat
  - defensive replacement

- pitcher role tags
  - rotation
  - opener
  - swingman
  - long relief
  - middle relief
  - setup
  - closer

Role tags are recommendations and AI hints. They do not override legality. A manager
can choose a utility player to start or a long reliever to open a game if the player
is otherwise eligible.

## Active roster size and pitcher cap

Defaults are configured on the `Ruleset` from `plans/01 § Ruleset`:

- `activeRosterSize: Integer = 26`
- `maxActivePitchers: Integer = 13`
- `minRotationSize: Integer = 5`
- `minBenchCatchers: Integer = 2` later if realism tuning needs it

The modern MLB defaults are only defaults. Era-aware or fictional leagues should be
able to change them without code edits.

Hard invariants:

- `active_hitter_count + active_pitcher_count <= activeRosterSize`
- `active_pitcher_count <= maxActivePitchers`
- every player belongs to exactly one compartment

## Transaction types

Every roster move is a typed `Transaction` action processed through a single entry
point. Transactions are emitted as events for the season log and succeed or fail
atomically.

Supported transactions for v1:

- **`Promote(player, fromCompartment, toCompartment)`**
  - e.g. `minors -> active_hitters`
  - e.g. `minors -> active_pitchers`

- **`Demote(player, fromCompartment, toCompartment)`**
  - e.g. `active_hitters -> minors`
  - e.g. `active_pitchers -> minors`

- **`PlaceOnIL(player, ilType, expectedReturnDay)`**
  - moves from an active compartment to `IL-10` or `IL-60`

- **`ActivateFromIL(player, toCompartment)`**
  - moves from IL back to `active_hitters` or `active_pitchers`

- **`ChangeActiveRole(player, newRoleTag)`**
  - changes a player's usage role without moving compartments
  - e.g. long reliever -> setup man
  - e.g. regular -> platoon

Deferred to v1.5+:

- **`Designate(player)`**
- **`Trade(playerOut, playerIn, withTeam)`**
- **`Waiver(player)`**
- **`ConvertTwoWayStatus(player)`** if two-way rules ever matter

### Atomic transactions

A single conceptual move often requires multiple compartment changes:

- call up player X from minors
- send player Y down to make room

These must execute as one atomic transaction so the active roster cap is never violated
mid-flight.

The transaction processor accepts a list of compartment changes that succeed or fail
together.

## Lineup card sourcing

When a daily lineup card is built (per `plans/10 § Daily simulation loop`), the legal
source pool is the team's current active roster:

- **Batting order and starting fielders** are drawn from `Active hitters`
- **Bench available for substitution** is the set of `Active hitters` not in the starting lineup
- **Starting pitcher** is one `Active pitchers` player who is starter-capable by role and passes today's availability checks
- **Bullpen available** is the remaining set of `Active pitchers` who are relief-capable by role and pass today's availability checks

That keeps the daily lineup card and the season roster layer cleanly separated:

- roster compartments define who can appear
- daily lineup choices define who does appear

The legality validator from `plans/01 § LineupCard.validateStart` extends with:

- every lineup-card player must be in the correct active compartment
- no IL or minors player can appear
- the active pitcher must come from `Active pitchers`
- the active roster caps still hold after any same-day transactions

## Pitcher availability windows

Beyond compartment membership, pitchers have day-of-use constraints. These are driven
by `12-fatigue-injuries-and-availability.md`, but the roster legality layer consults
their result.

For v1:

- **Starting pitcher** — cannot start unless `daysSinceLastStart >= rotationGap` (default 4)
- **Relief pitcher** — can be unavailable after heavy recent use even while remaining on the active roster
- **Three-batter minimum** — enforced in-game by `LineupCard.canRemovePitcher`; this plan does not duplicate that rule

The season layer caches a per-day availability snapshot for active players. The daily
lineup validator consults the snapshot rather than recomputing rest logic ad hoc.

## Legality checks

Legality checks layer on top of `plans/09 § Legality layer`. The daily check has three
tiers:

1. **Compartment legality** — all lineup-card players are in the proper active compartment; no IL or minors players appear
2. **Single-game legality** — 9 batting slots, no duplicates, valid positions, valid DH state
3. **Availability legality** — every involved player passes today's hard availability check from `12-fatigue-injuries-and-availability.md`

The `validateLineupForToday(card, day) -> Result` function should be the single entry
point. The current implementation still uses `Boolean` for `LineupCard.validateStart()`;
the migration to a typed `Result` belongs with the manager-command work in `14-manager-command-layer.md`.

## Calendar-aware transaction rules

Some transactions have date-aware constraints:

- an `IL-10` placement on day D means activation is legal on D+10 or later
- an `IL-60` placement on day D means activation is legal on D+60 or later
- a `Demote` to the minors can have a recall lock later; for v1 the lock is 0 days unless rules say otherwise
- a `Trade` (deferred) takes effect at a defined boundary, usually end of day

The transaction processor consults `SeasonCalendar.currentDay` to validate these.

## Persistence and replay

Roster state is part of the season-level persistent state captured by
`15-save-format-and-persistence.md`:

- compartment membership for every player
- active role tags for every active player
- IL placement dates and expected return dates
- the transaction event log for the current season

Loading a save must restore compartments and role tags byte-identically. There is no
"rebuild the roster from old lineup cards" path.

## What to defer

For v1:

- waivers, options, and 40-man distinctions
- Rule 5 draft and minor-league draft
- free agency and contract rules
- trade-deadline mechanics
- expanded September call-ups
- minor-league depth beyond a flat callup pool
- service-time rules and options clocks

## Failure modes to watch

### 1. Compartment leaks

A transaction that moves a player out of one compartment but fails to move them into
another leaves the player in zero compartments. The processor must verify "exactly one
compartment" for every player after every transaction.

### 2. Active size cap violation

A promotion without a paired move-out can bust the 26-man cap. Atomic transactions and
post-transaction invariants prevent this.

### 3. Pitcher cap violation

Promoting a pitcher or activating one from the IL can push the club over the active
pitcher cap. That must be rejected before the day begins.

### 4. Daily lineup state leaking into season compartments

If "bench today" or "starting left fielder today" becomes a season compartment, the
manager loses the ability to set daily lineups cleanly. Keep starters and bench as
lineup-card state only.

### 5. IL date arithmetic off-by-one

The classic IL-10 bug: a player placed on the IL on day D is activated on D+9 instead
of D+10. Test the full sequence explicitly.

### 6. Lineup card sourced from the wrong day's roster

If the season layer uses yesterday's active roster snapshot for today's lineup card,
IL moves and promotions silently fail. Always source the card from current roster state.

### 7. Doubleheader availability not refreshed

A pitcher used in game one of a doubleheader can incorrectly appear in game two if the
availability snapshot is not refreshed between games.

## Recommended next dependency

Once roster and transaction rules are accepted, the next planning layer is
`12-fatigue-injuries-and-availability.md`. The availability checks above and the IL
placement triggers both depend on a durable between-game workload model.
