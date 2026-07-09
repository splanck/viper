# Chess Release Polish Notes

## Implemented Upgrade

The chess demo has been upgraded from a polished single-screen demo into a release-style game shell while staying fully in Zia and `Viper.Graphics.Canvas`.

Implemented release systems:

- Title flow with Continue, New Game, Puzzles, Analysis, Settings, and Save Slot 1 actions.
- In-game pause overlay with Resume, Save, Settings, Restart, Main Menu, and Resign.
- Persistent settings through `Viper.IO.SaveData`.
- Three-slot save system with FEN as the position source of truth.
- Casual, 5+0 blitz, 10+5 rapid, and 15+10 classical clocks.
- Board themes, piece themes, assist toggles, high-contrast mode, reduced motion, and volume settings.
- Procedural audio for menu, move, capture, check, promotion, invalid, and game-over events.
- Built-in lesson puzzles and an analysis-board entry point.
- FEN import/export helpers and PGN export from SAN history.
- Replay stepping through Ctrl+Z / Ctrl+Y.
- Persistent stats and achievements through `Viper.Game.AchievementTracker`.
- Headless `release_probe.zia` coverage for notation, settings, save slots, and puzzle data.
- Demo binary name standardized to `chess`; the old BASIC chess demo was removed.

## Remaining Polish Ideas

- Add a curated PNG piece set once repo-owned art is available.
- Add a richer puzzle catalog with mate/fork/pin/skewer categories.
- Add full save-slot selection UI instead of defaulting the quick actions to slot 1.
- Add import/export text dialogs once the runtime has a comfortable text-entry pattern for Canvas games.
- Surface live AI search stats if `ChessAI` exposes depth, score, node count, and principal variation.

## Verification

Primary checks:

```sh
viper check examples/games/chess --diagnostic-format=json
zia examples/games/chess/smoke_probe.zia
zia examples/games/chess/release_probe.zia
```
