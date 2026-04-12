# Franchise Mode

## Goal

Define the long-horizon team-building layer around the season simulator: roster construction, transactions, scouting visibility, budgets, player acquisition, and delegation boundaries between human and AI.

The earlier plans built the baseball engine, the season engine, and the manager interface. This plan defines the broader organization-management game that sits above them.

## Core principle

Franchise mode should make scarcity, uncertainty, and resource tradeoffs meaningful without forcing every player into a GM-heavy experience.

That means two design rules:

1. Front-office systems must create meaningful constraints.
2. The human role boundary must stay configurable.

Some players want to be the field manager with AI front office support. Others want full OOTP-style control. The franchise layer must support both cleanly.

## Why this layer matters

Without franchise mode:

- every season begins from essentially static roster assumptions
- drafting, development, and scouting have no strategic frame
- contracts and budgets do not matter
- injuries and aging matter only tactically, not organizationally
- multi-season saves become stat archives rather than team-building games

This is the layer that turns a baseball simulator into a long-form management game.

## Relationship to prior plans

This plan depends on:

- `plans/11-roster-and-transaction-rules.md`
- `plans/12-fatigue-injuries-and-availability.md`
- `plans/13-player-development-and-aging.md`
- `plans/18-league-structure-and-postseason.md`

It also builds on the implemented season systems in:

- `src/season/season_model.zia`
- `src/season/season_engine.zia`
- `src/season/persistence.zia`

Those already support active rosters, minors pools, injuries, and development. Franchise mode expands them into a full organizational game.

## Human role boundaries

Franchise mode should support three role profiles.

### 1. Field manager

The user controls:

- daily lineup
- bullpen usage
- in-game tactics

The AI controls:

- roster churn
- scouting
- contracts
- trades
- draft

### 2. Manager plus roster authority

The user controls:

- everything above
- IL replacements
- callups and demotions
- active roster shape
- bullpen and bench composition

The AI still controls deeper financial and acquisition systems unless explicitly delegated otherwise.

### 3. Full franchise authority

The user controls:

- roster construction
- contracts and payroll
- scouting priorities
- trades
- free agency
- amateur acquisition
- manager decisions if desired

This is the OOTP-like full-control mode.

## Core subsystem groups

Franchise mode should be planned as several connected subsystems rather than one monolith.

### 1. Organization model

The club needs a richer organizational state:

- MLB active roster
- injured list
- reserve / 40-man equivalent if modeled
- one or more minor-league levels
- prospect and depth roles
- optional staff assignments later

For v1, a simplified minors structure is acceptable, but a single undifferentiated callup pool will eventually be too thin.

### 2. Financial model

At minimum:

- payroll
- team budget
- contract years and annual salary
- cash or operating flexibility

These do not need to become accounting software, but they must create real roster tradeoffs.

### 3. Contract model

Suggested first-cut contract data:

- player id
- start year / end year
- annual salaries
- arbitration or team-control flags
- option flags later
- free-agent eligibility date

For v1, guaranteed annual salary plus team-control stage is enough.

### 4. Scouting and information model

This is essential. Franchise mode should not expose hidden talent truth directly.

Suggested structure:

- true ratings remain the simulation truth
- scouting reports are noisy estimates
- scouting quality varies by organization investment or staff quality later
- injury and makeup risk can be coarse later

This is the long-term extension of `plans/14 § Manager-visible player view`.

### 5. Acquisition systems

The franchise layer should eventually support:

- amateur draft
- free agency
- trades
- waiver-style moves later
- international signings later

For v1 of franchise mode, the required acquisition set is:

- trades
- simple free agency
- simple amateur draft or prospect replenishment mechanism

Without a replenishment mechanism, multi-season play eventually empties the talent pipeline.

## Delegated AI front office

Because the user role boundary is configurable, the game needs AI front-office logic.

AI front-office responsibilities may include:

- setting budgets and spending profile from club personality
- deciding when to promote prospects
- deciding whether to buy, sell, or hold at key season checkpoints
- filling obvious roster holes
- negotiating simple contracts in delegated modes

This AI should operate through the same transaction and roster legality systems as the human player.

## Calendar integration

Franchise mode decisions do not happen continuously; they happen on known calendar rhythms.

Suggested key franchise checkpoints:

- pre-season roster cutdown
- opening day
- weekly injury / callup review
- monthly financial and scouting review
- draft day
- trade deadline
- postseason roster review
- off-season free agency / contract renewal window

This keeps the organizational game manageable instead of constantly interruptive.

## Scouting visibility design

Scouting should be intentionally imperfect.

### Visible values

The user sees:

- star or letter grades
- coarse role projections
- injury risk labels
- recent performance
- development trend labels

### Hidden truth

The simulation still uses:

- exact ratings
- exact ceilings
- exact development variance

This separation is necessary. Without it, franchise mode becomes solved arithmetic rather than baseball management under uncertainty.

## Trade model

Trades should not be arbitrary swaps of visible current stats.

The trade system should evaluate:

- present talent
- future talent
- positional scarcity
- contract cost
- team competitive window
- budget flexibility

For v1, the AI trade model can be conservative and simplified, but it still needs a coherent valuation framework.

## Draft and talent replenishment

A multi-season baseball sim needs new players entering the world.

The replenishment system should create:

- incoming amateur pool
- age and archetype distribution
- ceiling / variance profiles
- scouting uncertainty

If a full amateur draft is too large for the first pass, a simple off-season replenishment pipeline is acceptable, but it must still preserve league talent flow.

## Budget and owner pressure

Franchise mode becomes more interesting when success is constrained by budget and expectations.

For a first planning pass, use a modest owner / market model:

- market size
- payroll target
- competitiveness expectation
- patience level

The owner system should constrain choices, not become the star of the product.

## Reports and user-facing outputs

Franchise mode should support:

- organization depth chart
- payroll summary
- contract obligations
- scouting board
- trade block summary
- prospect list
- injuries and return timeline
- roster-need report

These outputs are required for text-first play and later UI layers.

## What to defer

For v1:

- detailed rule-5 / option-year mechanics
- full collective-bargaining simulation
- owner politics beyond simple expectations
- staff hiring and coaching trees
- international posting systems
- detailed clubhouse chemistry
- complex arbitration hearing simulation
- sponsorship and stadium business systems

## Failure modes to watch

### 1. Full-control design forcing every player into GM duties

If the field-manager path is not viable, the product drifts away from the original fantasy.

### 2. Franchise decisions with perfect information

If scouting is exact, franchise mode becomes spreadsheet optimization instead of baseball management.

### 3. Budget without meaningful pressure

If budgets exist but never constrain behavior, they are just decorative numbers.

### 4. Talent pipeline collapse

If the draft or replenishment system is weak, the league ages into emptiness over many seasons.

### 5. AI front office bypassing legality

If AI clubs operate outside roster, contract, or transaction rules, competitive integrity collapses.

### 6. Too many systems before the basics are good

If trades, contracts, and scouting are added before player evaluation, roster logic, and season calibration are credible, the whole mode rests on noise.

## Recommended next dependency

Once franchise mode is accepted, the next planning layer is `20-history-and-awards.md`.

Long-running franchise saves need memory: records, awards, retirements, milestones, and preserved league history. Without that, seasons blur together even if the underlying economics are good.
