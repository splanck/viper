# History And Awards

## Goal

Define how the game preserves baseball history across seasons: archived standings, postseason outcomes, awards, records, milestones, retirements, and long-term league memory.

The prior plans make multi-season play possible. This plan makes multi-season play *remembered*.

## Core principle

History should be built from structured facts first and narrative presentation second.

The simulation should record:

- who won
- what happened
- who achieved notable milestones
- how careers accumulated

Only after that should the product generate story-like summaries, headlines, or historical text. If the facts are not first-class data, the history layer becomes brittle and untrustworthy.

## Why this layer matters

Without history and awards:

- seasons blur together
- championships do not feel persistent
- players feel disposable
- career arcs are hard to appreciate
- multi-season saves lose emotional weight

This is the layer that gives the franchise a memory.

## Relationship to prior plans

This plan depends on:

- `plans/13-player-development-and-aging.md`
- `plans/15-save-format-and-persistence.md`
- `plans/18-league-structure-and-postseason.md`
- `plans/19-franchise-mode.md`

It builds on the existing `CompletedGameRecord`, season persistence, and career ledger work already described earlier in the stack.

## Historical record model

The history layer should create durable season archive records.

Suggested objects:

- `ArchivedSeasonRecord`
- `ArchivedTeamSeason`
- `ArchivedPlayerSeason`
- `AwardRecord`
- `MilestoneRecord`
- `RecordBookEntry`
- `RetirementRecord`

### Archived season record

Each completed season should preserve:

- year
- league structure
- final standings
- postseason bracket and winner
- award winners
- major records broken
- notable retirements

This should be immutable once archived.

## Awards model

Awards should be derived from season performance, not hand-authored labels.

Suggested first award set:

- MVP
- Cy Young equivalent
- Rookie of the Year
- Reliever of the Year
- Gold Glove equivalents
- Silver Slugger equivalents
- Manager of the Year later if managers become first-class career actors

### Award ballots

The system should preserve:

- finalists
- winner
- ballot summary or point totals
- the statistical basis used for the decision

For v1, simple weighted score models are acceptable. The important part is that the award decision is reproducible and archived.

## Records and milestones

The game should track both league records and personal milestones.

### League records

- single-season home runs
- single-season wins
- single-season ERA minimum threshold
- single-season strikeouts
- team wins
- longest winning streak

### Career records

- career home runs
- career hits
- career strikeouts
- career wins
- career WAR-like total later if such a metric exists

### Milestones

- 500 home runs
- 3,000 hits
- 300 wins
- 2,000 strikeouts
- age-based "youngest to" and "oldest to" later if desired

Milestones should produce structured records that later output layers can summarize.

## Retirement model

Once multi-season play exists, players must eventually leave the world.

The retirement system should preserve:

- retirement year
- age at retirement
- final club
- career totals
- notable awards and records

The actual retirement decision logic may remain simple at first, but the archival format should exist from the start.

## Hall of fame / legends layer

Do not force this into v1, but the history model should leave room for:

- hall-of-fame voting
- retired numbers
- franchise legends
- all-time team lists

These systems depend on archived seasons and award records being structured correctly.

## Historical browsing outputs

The history layer should support text-first browsing of:

- past champions
- past standings
- team yearbooks
- player career pages
- award history
- all-time leaderboards
- milestone log

This matters even before any GUI exists. Text-first franchise play still needs historical browsing.

## Narrative surfaces

Narrative output should be *derived* from history data.

Examples:

- "Lakeview won its third title in five years"
- "A 37-year-old veteran reached 3,000 hits"
- "A rookie won MVP after leading the league in OPS"

These are generated summaries over structured history, not standalone hand-maintained text blobs.

## Persistence

The history layer extends `plans/15`.

Saved state should preserve:

- archived season list
- award history
- milestone ledger
- record books
- retirement records

Active save files should not need to recompute the entire league’s history from raw completed games every time. Archival records become part of persistent franchise truth once a season closes.

## Interaction with franchise mode

History affects franchise decisions:

- ring count changes player and club legacy
- aging veterans chasing milestones affect lineup decisions
- award winners affect club identity
- organizational history can influence owner and fan expectations later

This is another reason structured history needs to exist before deep narrative polish.

## Interaction with broadcast mode

The broadcast layer should be able to pull history context:

- pennant drought
- season series context
- milestone watch
- defending champion storylines

That only works if the history layer is already structured.

## What to defer

For v1:

- full hall-of-fame voting
- historical newspaper archive
- franchise ring ceremonies
- jersey retirement ceremonies
- generated biographies
- broadcaster memory systems beyond pulling structured facts

## Failure modes to watch

### 1. History rebuilt from unstable derived data

If archived history is not frozen at season close, later code changes can rewrite the past unintentionally.

### 2. Awards without stored rationale

If the game stores only the winner name, later browsing has no context and debugging voting logic becomes difficult.

### 3. Records with unclear qualification rules

If ERA titles or rate-stat leaderboards do not encode minimum thresholds, historical records become misleading.

### 4. Retirements without preserved career snapshots

If a player retires and only their final totals remain, major context is lost.

### 5. Narrative text stored without structured backing data

If the product stores only headlines and not the underlying fact ledger, history becomes impossible to verify.

## Recommended next dependency

Once history and awards are accepted, the next planning layer is `21-broadcast-and-watch-mode.md`.

The broadcast layer is where structured history starts paying visible dividends: milestone callouts, pennant-race context, defending-champion framing, and season-long story continuity.
