# Baseball Backend Plans

This folder holds the draft plans for the first backend-only stage of the baseball manager sim.

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

These plans assume:

- Text output only
- Backend simulation first
- Realistic baseball outcomes are the top priority
- The long-term target is closer to OOTP-style management than to an action game

The main architectural rule is simple:

- The simulation must own the truth
- Presentation must consume events from the simulation
- Manager decisions must act on stable game state, not on UI-specific code

The current planning expansion beyond the baseline architecture is:

- How player talent is represented
- How one plate appearance is resolved
- How realism is measured and calibrated
- How balls in play become outs, hits, and errors
- How runner advancement is resolved
- How the manager interacts with the engine at safe decision points

The next planning focus after this single-game backend set is likely:

- Season simulation architecture
- Roster construction and transaction rules
- Long-term player development, aging, and injuries
