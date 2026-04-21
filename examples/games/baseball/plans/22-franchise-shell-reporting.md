# 22. Franchise Shell Reporting

The interactive baseball demo now treats the franchise shell as the primary
management surface instead of a thin sim launcher.

Implemented reporting surfaces:

- Dashboard with current date, standings, completed-game totals, recent games, and next user-team game.
- Completed-game review for every game in the current season.
- Persisted box score rendering from `CompletedGameRecord`, so saved games can be reviewed after reload.
- League leaders for AVG, OBP, SLG, HR, RBI, H, ERA, WHIP, SO, and IP.
- Team batting and pitching statistics for the user club.
- Roster compartment view covering active hitters, active pitchers, minors, and injured lists.
- Health/workload view covering unavailable players, rest recommendations, and pitcher usage.
- Manager decision-log review.
- Transaction-log review.
- Pacing-profile changes from the main shell.
- Sim-N-days control plus post-sim user-game box score output.
- Save-slot deletion from the boot screen.

Design constraints:

- The simulation still owns all baseball truth. Reporting consumes season state
  and completed-game records only.
- The box score writer does not depend on live `GameState`, which keeps saved
  game review reliable.
- Leaderboards use dynamic minimums so early-season saves remain informative
  without letting long-season rate categories become dominated by one-appearance
  players.
- The user-facing roster and health reports use availability/readiness state,
  not hidden player ratings.

Verification:

- `examples/games/baseball/reporting_probe.zia` builds a default franchise,
  simulates several days, and verifies the major reporting blocks are present.
- CTest registration: `zia_reporting_baseball`.
