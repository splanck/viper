# Viper Chess

A full chess engine with AI opponent, written entirely in Zia.

## Features

- Complete chess rules: castling, en passant, pawn promotion, 50-move draw
- Alpha-beta AI with iterative deepening, transposition tables, killer moves, and quiescence search
- 4 engine personalities: Beginner (depth 2), Club (3), Tournament (4), Master (5)
- 3 game modes: PvP, Player vs Computer, Computer vs Computer
- Move history in Standard Algebraic Notation (SAN)
- Casual, blitz, rapid, and classical chess clocks
- Main menu, pause menu, settings screen, continue flow, analysis board, and built-in puzzles
- Three save slots backed by cross-platform `SaveData`
- FEN import/export helpers and PGN export from move history
- Persistent board theme, piece theme, audio, accessibility, and assist settings
- Procedural audio for menu, move, capture, check, promotion, and game-over events
- Persistent stats and achievements
- Captured piece tracking
- Drag-and-drop piece movement with legal move indicators
- Optional attack-map, legal-move, coordinate, high-contrast, reduced-motion, and large-text settings
- Polished custom-canvas presentation with themed panels, interactive HUD controls, and modal overlays
- Bundled bitmap-font rendering for menu, HUD, and panel typography
- Pre-rendered piece sprites with alpha blending, shadows, and mini capture icons
- Zobrist hashing for transposition table and draw detection

## Controls

| Input | Action |
|-------|--------|
| Left click + drag | Move a piece |
| N | New game |
| Escape / P | Pause in-game; Back from menu screens |
| Enter | Start game (from menu) |
| Ctrl+S | Save to slot 1 |
| Ctrl+Z / Ctrl+Y | Step backward / forward through replay history |

## Project Structure

```
chess/
    main.zia                Entry point

    assets/
        fonts/
            viper_8x8.bdf   Bundled bitmap font used by the polished UI

    core/
        config.zia          Constants, colors, piece values, layout
        board.zia           Board state, Zobrist hashing, make/unmake move
        moves.zia           Legal move generation (all piece types)
        eval.zia            Static position evaluation (material + PST)

    engine/
        ai.zia              Alpha-beta search, transposition table, move ordering

    settings.zia            Persistent settings and theme/time-control names
    save.zia                Save-slot persistence
    notation.zia            FEN + PGN helpers
    puzzles.zia             Built-in lesson puzzle data
    stats.zia               Persistent stats + achievements
    sound.zia               Procedural audio wrapper

    ui/
        game.zia            Game loop, input handling, state machine
        renderer.zia        Board + UI drawing, status panels, overlays
        theme.zia           Visual system, colors, bitmap fonts, text helpers
        layout.zia          Named UI regions and hitbox layout helpers
        widgets.zia         Reusable canvas UI primitives
        pieces.zia          Pre-rendered piece sprite cache, shadows, mini icons

    release_probe.zia       Headless release-system smoke probe
```

## Release Polish

- [Polish Plan](polish-plan.md) — implemented release upgrade notes and remaining polish ideas

## Running

```bash
cd examples/games/chess
viper run .
```

## AI Architecture

The chess engine uses **negamax with alpha-beta pruning** and several standard enhancements:

1. **Iterative deepening** — searches depth 1, 2, ..., N, using results from shallower searches to improve move ordering
2. **Transposition table** — 64K-entry hash table (Zobrist keys) caches evaluated positions to avoid re-search
3. **Killer move heuristic** — stores 2 quiet moves per ply that caused beta cutoffs, tried early in non-capture ordering
4. **MVV-LVA ordering** — captures are sorted by Most Valuable Victim / Least Valuable Attacker
5. **Quiescence search** — extends search at leaf nodes for captures only, avoiding horizon effect
6. **Piece-square tables** — positional bonuses for piece placement (centralization, pawn structure, king safety)

## Evaluation

The static evaluation function scores positions using:
- **Material balance** — pawn=100, knight=320, bishop=330, rook=500, queen=900
- **Piece-square tables** — per-piece positional bonuses (e.g., knights prefer center, kings prefer corners in middlegame)
- Evaluation is from the perspective of the side to move (negamax convention)
