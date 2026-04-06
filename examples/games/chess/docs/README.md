# Viper Chess

A full chess engine with AI opponent, written entirely in Zia.

## Features

- Complete chess rules: castling, en passant, pawn promotion, 50-move draw
- Alpha-beta AI with iterative deepening, transposition tables, killer moves, and quiescence search
- 4 difficulty levels: Easy (depth 2), Medium (3), Hard (4), Expert (5)
- 3 game modes: PvP, Player vs Computer, Computer vs Computer
- Move history in Standard Algebraic Notation (SAN)
- Chess clocks with per-move timing
- Captured piece tracking
- Drag-and-drop piece movement with legal move indicators
- Pre-rendered piece sprites with alpha blending
- Zobrist hashing for transposition table and draw detection

## Controls

| Input | Action |
|-------|--------|
| Left click + drag | Move a piece |
| N | New game |
| Escape | Return to main menu |
| Enter | Start game (from menu) |

## Project Structure

```
chess/
    main.zia                Entry point

    core/
        config.zia          Constants, colors, piece values, layout
        board.zia           Board state, Zobrist hashing, make/unmake move
        moves.zia           Legal move generation (all piece types)
        eval.zia            Static position evaluation (material + PST)

    engine/
        ai.zia              Alpha-beta search, transposition table, move ordering

    ui/
        game.zia            Game loop, input handling, state machine
        renderer.zia        Board + UI drawing, status panels
        pieces.zia          Pre-rendered piece sprite cache (Pixels + BlitAlpha)
```

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
