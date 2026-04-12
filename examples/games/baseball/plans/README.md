# Baseball Backend Plans

This folder holds the draft plans for the baseball manager sim. The single-game backend
(plans `00`–`09`) is the foundation; the season-level layer (`10`–`15`) builds on it;
the product-level layer (`16`–`21`) expands that foundation into a full management game.

Recommended reading order:

1. `00-simulation-overview.md`
2. `01-domain-and-rules-model.md`
3. `02-simulation-pipeline.md`
4. `03-implementation-phases.md`
5. `04-player-ratings-schema.md`
6. `05-plate-appearance-model.md`
7. `06-calibration-and-data-plan.md`
8. `07-batted-ball-and-fielding-model.md`
9. `08-runner-advancement-and-baserunning.md`
10. `09-in-game-manager-decision-model.md`
11. `10-season-simulation-architecture.md`
12. `11-roster-and-transaction-rules.md`
13. `12-fatigue-injuries-and-availability.md`
14. `13-player-development-and-aging.md`
15. `14-manager-command-layer.md`
16. `15-save-format-and-persistence.md`
17. `16-season-calibration-and-reporting.md`
18. `17-human-manager-mode.md`
19. `18-league-structure-and-postseason.md`
20. `19-franchise-mode.md`
21. `20-history-and-awards.md`
22. `21-broadcast-and-watch-mode.md`

These plans assume:

- Text output only
- Backend simulation first
- Realistic baseball outcomes are the top priority
- The long-term target is closer to OOTP-style management than to an action game

The main architectural rule is simple:

- The simulation must own the truth
- Presentation must consume events from the simulation
- Manager decisions must act on stable game state, not on UI-specific code

The single-game backend (plans `00`–`09`) covers:

- How player talent is represented
- How one plate appearance is resolved
- How realism is measured and calibrated
- How balls in play become outs, hits, and errors
- How runner advancement is resolved
- How the manager interacts with the engine at safe decision points

The season-level layer (plans `10`–`15`) adds:

- Calendar, daily loop, schedule, standings, and the playoff hook
- Roster compartments and the legal transactions that move players between them
- Cross-game fatigue, injury, and availability state
- Player development, aging, and the off-season tick
- A typed manager command contract that supports both AI and human play
- Save format, persistence, versioning, and deterministic replay

The product-level layer (plans `16`–`21`) adds:

- Season-scale calibration and reporting
- Real human-manager control through the typed decision layer
- League structure, divisions, and postseason competition
- Franchise-mode economics, scouting, and player acquisition
- Historical memory through awards, records, and archived seasons
- A broadcast/watch-mode architecture that consumes simulation events without owning truth
