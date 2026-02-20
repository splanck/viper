# Zia Chess Game — Design & Implementation Plan

## Context

Build a release-quality 2D graphical chess game in Zia for PC and Mac. The game uses `Viper.Graphics` (Canvas, Pixels, Color) for rendering, `Viper.Input.Mouse` for drag-and-drop piece movement, a minimax engine with alpha-beta pruning and piece-square tables for AI, and supports Player vs Player, Player vs Computer, and Computer vs Computer modes. This document is the canonical design reference saved to `bugs/chess_plan.md` in the repository.

---

## Output Location

After plan approval, create:
- `demos/zia/chess/` — full game project
- `bugs/chess_plan.md` — this design document verbatim

---

## Window Layout

```
┌─────────────────────────────────────────────────────────────────────┐  960 × 700
│  ♟ Viper Chess                                    [New]  [Menu]     │  ← 40px top bar
├────────────────────────────────────┬────────────────────────────────┤
│                                    │  ● BLACK    10:00              │
│                                    │  Captures: ♙♙♗                │
│                                    │  ─────────────────────         │
│         BOARD                      │  Move History                  │
│         640 × 640                  │  1. e4   e5                    │
│         (80px squares)             │  2. Nf3  Nc6                   │
│                                    │  3. Bb5  a6                    │
│                                    │  ─────────────────────         │
│                                    │  ○ WHITE    09:47              │
│                                    │  Captures: ♟                   │
│                                    │  ─────────────────────         │
│                                    │  Mode:  [PvC]                  │
│                                    │  Level: [Hard]                 │
├────────────────────────────────────┴────────────────────────────────┤
│  White to move                                  [Draw]  [Resign]    │  ← 40px bottom bar
└─────────────────────────────────────────────────────────────────────┘
```

- **Total window:** 960 × 700
- **Board area:** x=0, y=40, w=640, h=640 — 80px per square
- **Side panel:** x=640, y=40, w=320, h=620
- **Rank/file labels:** painted just inside the board border, outside the 8×8 square grid

---

## Visual Design

### Color Palette
```
BOARD_LIGHT   = 0xF0D9B5   // Warm cream (classic Lichess light square)
BOARD_DARK    = 0xB58863   // Warm brown (classic Lichess dark square)
HL_SELECTED   = 0xF6F669   // Yellow — selected square overlay
HL_MOVE_DOT   = 0x646D40   // Olive — legal move destination dot
HL_CAPTURE    = 0x8B3A3A   // Red-brown ring — legal capture target
HL_LAST_MOVE  = 0xCDD52F   // Green-yellow — last move highlight (alpha 80)
HL_CHECK      = 0xFF4040   // Red — king square when in check
UI_BG         = 0x1A1A2E   // Dark navy — window and panel background
UI_PANEL      = 0x16213E   // Slightly lighter — panel section blocks
UI_TEXT       = 0xE0E0E0   // Light grey — primary text
UI_ACCENT     = 0xF5A623   // Orange — active player indicator
UI_DIM        = 0x888888   // Dimmed — secondary text
TOPBAR_BG     = 0x0F3460   // Deep blue — top bar background
```

### Piece Rendering — Procedural Pixels

Pieces are drawn **once at startup** into 12 Pixels buffers (6 types × 2 colors), each 70×70px. Each frame they are blitted with `canvas.BlitAlpha()`. This eliminates external asset dependencies and scales perfectly.

White pieces: `0xFFFFFF` fill, `0x303030` outline.
Black pieces: `0x303030` fill, `0xF0F0F0` outline.

**Piece shapes (70×70px, visual center at 35,50 for bottom-anchored pieces):**
- **Pawn:** base ellipse (w=36,h=9 at y=60) + neck stem + head disc (r=11)
- **Rook:** base ellipse + rectangular body (w=30,h=28) + three crenellations on top
- **Knight:** base + body built from overlapping discs forming horse-head + ear triangle + eye dot
- **Bishop:** base + tapered body via Bezier + ball disc + cross lines
- **Queen:** base + body + crown of 7 small discs arranged in arc at top
- **King:** base + body + bold cross at top (two overlapping thick boxes)

---

## Project Structure

```
demos/zia/chess/
├── viper.project
├── main.zia           # Entry — creates ChessGame, calls game.run()
├── config.zia         # All constants: colors, sizes, piece IDs, AI params
├── board.zia          # Board entity — square array, castling, en passant, Zobrist hash
├── moves.zia          # MoveGen entity — full legal move generation
├── eval.zia           # Evaluator — material + piece-square tables
├── ai.zia             # ChessAI entity — iterative deepening minimax + α-β
├── renderer.zia       # BoardRenderer — draws board, pieces, highlights, all UI panels
└── game.zia           # ChessGame entity — main loop, input, state machine
```

---

## Module Specifications

### `config.zia`

```zia
module config;

// Piece IDs (positive = white, negative = black, 0 = empty)
const EMPTY  = 0;
const PAWN   = 1;  const KNIGHT = 2;  const BISHOP = 3;
const ROOK   = 4;  const QUEEN  = 5;  const KING   = 6;

// Piece values (centipawns)
const VAL_PAWN = 100;  const VAL_KNIGHT = 320;  const VAL_BISHOP = 330;
const VAL_ROOK = 500;  const VAL_QUEEN  = 900;  const VAL_KING   = 20000;

// Window dimensions
const WIN_W = 960;  const WIN_H = 700;
const BOARD_X = 0;  const BOARD_Y = 40;
const SQ_SIZE = 80;  // pixels per square
const PIECE_SIZE = 70;  // piece Pixels buffer size

// Game states
const STATE_MENU      = 0;
const STATE_PLAYING   = 1;
const STATE_PROMO     = 2;
const STATE_AI_THINK  = 3;
const STATE_CHECKMATE = 4;
const STATE_STALEMATE = 5;
const STATE_DRAW      = 6;

// Game modes
const MODE_PVP = 0;  const MODE_PVC = 1;  const MODE_CVC = 2;

// AI difficulty (search depth)
const DIFF_EASY   = 2;
const DIFF_MEDIUM = 3;
const DIFF_HARD   = 4;
const DIFF_EXPERT = 5;
```

---

### `board.zia` — Board Entity

**Square encoding:** `sq = rank * 8 + file`, rank 0 = black back rank (row 8), rank 7 = white back rank (row 1).

```
expose Integer[64] squares   // 0=empty, +N=white piece N, -N=black piece N
expose Boolean     whiteToMove
expose Integer     enPassantFile   // -1 if none, 0-7 for file
expose Boolean[4]  castleRights    // [WK, WQ, BK, BQ]
expose Integer     halfMoveClock   // 50-move rule counter
expose Integer     fullMoveNumber

expose func init()                              // Set up starting position
expose func initFromFen(fen: String)
expose func makeMove(move: Move) -> UndoInfo    // Apply and return undo data
expose func unmakeMove(move: Move, undo: UndoInfo)
expose func getHash() -> Integer               // Zobrist hash for TT / repetition
expose func toFen() -> String
```

**Value types:**
```
value Move {
    Integer from; Integer to; Integer captured;
    Boolean enPassant; Boolean castleKing; Boolean castleQueen;
    Boolean promotion; Integer promotePiece;
}

value UndoInfo {
    Integer enPassantFile; Boolean[4] castleRights;
    Integer halfMoveClock; Integer capturedSq;
}
```

---

### `moves.zia` — Move Generator

```
expose func legalMoves(board: Board) -> List[Move]
expose func isInCheck(board: Board, white: Boolean) -> Boolean
expose func isAttacked(board: Board, sq: Integer, byWhite: Boolean) -> Boolean
expose func perft(board: Board, depth: Integer) -> Integer   // correctness testing
```

**Approach:** Generate pseudo-legal moves per piece type, filter by `isLegalAfterMove` (makeMove → check if own king attacked → unmakeMove). Direct port of the BASIC chess demo's move logic.

**Special moves handled:** en passant, both castling sides both colors, pawn promotion (generates 4 moves per promoting pawn: Q/R/B/N).

**Correctness gate:** `perft(startPos, 4)` must return `197281`. This is a well-known chess perft value that catches any move generation bugs before building the rest of the game.

---

### `eval.zia` — Evaluator

```
expose func init()                            // Load PST arrays
expose func evaluate(board: Board) -> Integer // Positive = white winning (centipawns)

hide Integer[64] PST_PAWN, PST_KNIGHT, PST_BISHOP
hide Integer[64] PST_ROOK, PST_QUEEN, PST_KING_MID, PST_KING_END
```

**Scoring components:**
1. **Material** — sum of piece values per side (from config constants)
2. **Piece-Square Tables** — positional bonuses from well-known PST tables (knights penalized on rim, pawns rewarded in center, king safety in middlegame, king activity in endgame)
3. **Endgame detection** — switch from PST_KING_MID to PST_KING_END when few queens/pieces remain

---

### `ai.zia` — Chess AI

```
expose func init(depth: Integer)
expose func findBestMove(board: Board) -> Move

hide func iterativeDeepening(board, maxDepth) -> Move
hide func alphabeta(board, depth, alpha, beta, maximizing) -> Integer
hide func quiescence(board, alpha, beta) -> Integer
hide func orderMoves(moves: List[Move], board, depth) -> List[Move]

hide Map[Integer, TTEntry] transTable   // Transposition table
hide Integer[2] killerMoves             // Killer move heuristic
```

**Algorithm:**
1. **Iterative deepening** — search depth 1..N, use each depth's best move for move ordering at depth+1
2. **Alpha-beta (negamax)** — standard pruning; prunes branches provably worse than current best
3. **Move ordering** (crucial for pruning efficiency):
   - Transposition table hit move first
   - Captures ordered by MVV-LVA (Most Valuable Victim / Least Valuable Attacker)
   - Killer moves (non-captures that caused cutoffs at same depth)
   - Remaining moves
4. **Quiescence search** — at leaf nodes, continue searching captures-only until position is "quiet" (prevents horizon-effect blunders)
5. **Transposition table** — Zobrist-keyed Map caching `{score, depth, flag, bestMove}` — avoids re-searching transpositions

**Expected strength:** approximately 1200–1600 ELO equivalent depending on hardware and depth setting.

---

### `renderer.zia` — Board Renderer

```
expose func init(canvas: Canvas)             // Build all 12 piece Pixels at startup
expose func drawBoard(lastFrom, lastTo)      // Squares + last-move highlights
expose func drawHighlights(sel, moves)       // Selected square + legal move dots/rings
expose func drawPieces(board, dragFrom, dx, dy)  // All pieces; skip dragged piece
expose func drawDragPiece(piece, x, y)       // Piece following cursor
expose func drawCheckHighlight(kingSq)
expose func drawPanel(game)                  // Side panel: clocks, history, captures, controls
expose func drawTopBar(game)
expose func drawBottomBar(game)
expose func drawPromoDialog(white)           // Q/R/B/N selection overlay
expose func drawMainMenu()
expose func drawGameOverOverlay(reason, winner)
expose func squareAt(mx, my) -> Integer      // -1 if outside board
expose func squareCenterX/Y(sq) -> Integer

hide func buildPiecePixels(type, white) -> Pixels
hide func draw{Pawn,Knight,Bishop,Rook,Queen,King}(px, white)
```

---

### `game.zia` — Main Game Entity

```
hide Canvas canvas;          hide Board board;
hide MoveGen moveGen;        hide ChessAI ai;
hide BoardRenderer renderer;

// State
hide Integer gameState;      hide Integer gameMode;
hide Integer aiDepth;        hide Boolean playerIsWhite;

// Input
hide Integer selectedSq;     // -1 = nothing selected
hide Integer dragFrom;        // -1 = not dragging
hide Integer dragX, dragY;   // Current drag cursor position
hide List[Move] legalMovesForSelected;

// History
hide List[Move] moveHistory;
hide List[String] sanHistory;       // Algebraic notation
hide List[Integer] positionHashes;  // For threefold repetition detection
hide Integer[2] captureCount;

// Animation / timing
hide Integer aiThinkStart;
hide Boolean lastMoveIsCheck;
hide Integer lastFrom, lastTo;

expose func init();   expose func run();
```

**Game loop (inside `run()`):**
```
while canvas.get_ShouldClose() == 0:
    canvas.Poll()
    handle keyboard shortcuts (Ctrl+N, Ctrl+Z, Escape)
    handle mouse (press → pick up, hold → drag, release → drop)
    if STATE_AI_THINK: call aiTurn()
    clear canvas
    drawTopBar → drawBoard → drawHighlights → drawPieces → drawDragPiece
    drawCheckHighlight (if applicable) → drawPanel → drawBottomBar
    draw promo/gameover overlays if needed
    canvas.Flip()
    SleepMs(8)   // ~120 FPS cap
```

---

## Input — Drag-and-Drop + Click-to-Move

```
Mouse press:
  sq = squareAt(mx, my)
  if sq valid and piece belongs to current human player:
    selectedSq = sq; dragFrom = sq; dragX = mx; dragY = my
    legalMovesForSelected = moveGen.legalMoves(board).filter(m.from == sq)

Mouse hold (left button):
  if dragFrom != -1: dragX = mx; dragY = my

Mouse release:
  sq = squareAt(mx, my)
  if sq != -1 and dragFrom != -1 and sq != dragFrom:
    tryMove(dragFrom, sq)     // attempt the move
  else if sq == dragFrom:
    selectedSq = (selectedSq == sq) ? -1 : sq   // toggle selection
  dragFrom = -1

Click-to-move fallback:
  if selectedSq != -1 and sq is a legal destination:
    tryMove(selectedSq, sq)
  else if sq has own piece:
    re-select sq
```

---

## Special Rules Checklist

- En passant capture
- Castling (both sides, both colors) — king and rook rights tracked in Board
- Pawn promotion — dialog shows Q/R/B/N; auto-queen in CvC mode
- Check detection + red highlight on king square
- Checkmate detection (no legal moves + in check)
- Stalemate detection (no legal moves + not in check)
- 50-move rule (halfMoveClock ≥ 100)
- Threefold repetition (Zobrist hash appears 3× in positionHashes)
- Insufficient material (K vs K, K+N vs K, K+B vs K)

---

## Implementation Phases

### Phase 1 — Board + Move Generation
- `config.zia`, `board.zia`, `moves.zia`
- Gate: `perft(startPos, 4) == 197281`

### Phase 2 — Renderer (static board, no interaction)
- `renderer.zia` — piece art, board drawing, all UI panels
- Gate: Renders starting position cleanly, side panel visible

### Phase 3 — Input + Game Loop
- `game.zia` — mouse input, drag-and-drop, click-to-move, promotion dialog
- Gate: PvP game playable, all special moves working

### Phase 4 — AI Engine
- `eval.zia`, `ai.zia` — PSTs, minimax, alpha-beta, TT, quiescence
- Gate: AI plays legal moves at all difficulties, finds checkmate in 1

### Phase 5 — Polish
- Game over detection (all draw types), game over overlay
- Main menu (mode/difficulty selection)
- CvC auto-play with 500ms delay between moves
- Undo move (Ctrl+Z) for PvP and PvC
- Captured piece display with mini piece icons
- Count-up clock per side

### Phase 6 — QA
- All special rules play-tested
- Perft verification at depth 5 (`4865609`)
- macOS ARM64 + x86-64 build and run

---

## Files to Create

| File | Role |
|------|------|
| `demos/zia/chess/viper.project` | Project descriptor |
| `demos/zia/chess/main.zia` | Entry point |
| `demos/zia/chess/config.zia` | Constants |
| `demos/zia/chess/board.zia` | Board entity + Move/UndoInfo values |
| `demos/zia/chess/moves.zia` | Legal move generator |
| `demos/zia/chess/eval.zia` | Material + PST evaluator |
| `demos/zia/chess/ai.zia` | Minimax engine |
| `demos/zia/chess/renderer.zia` | All rendering |
| `demos/zia/chess/game.zia` | Main game entity + loop |
| `bugs/chess_plan.md` | This design document |

Total estimate: ~1,800 lines of Zia.

---

## Design Decision Report

### 1. Procedural piece art (no external image files)
Pieces are drawn once at startup into 70×70 Pixels buffers using Canvas/Pixels drawing primitives (DrawDisc, DrawBox, DrawBezier, DrawTriangle, DrawArc). This means the game ships with zero external assets — no PNG/BMP files to manage, no asset loading errors. Pieces render crisp at any size and can be recolored trivially. The BASIC chess demo has no graphics; this approach bridges that gap cleanly using only the Viper runtime.

### 2. Iterative deepening minimax with alpha-beta pruning
The BASIC chess demo uses fixed-depth minimax (depth 1–4). For the Zia version, iterative deepening is added: search depth 1, 2, …, N, using each shallower result to order moves at the next depth. This dramatically improves alpha-beta efficiency (better move ordering → more cutoffs → effectively searches much deeper in the same time). Quiescence search extends captures beyond the horizon to prevent the engine from walking into obvious material loss just past its search depth. The transposition table avoids re-evaluating positions reached by different move orders. Together these give a genuinely challenging engine at Hard/Expert without requiring a chess engine library.

### 3. Piece-square tables for positional play
Material-only evaluation (the BASIC demo's approach) produces an AI that develops poorly and plays passively. PSTs are small 64-integer tables (one per piece type) encoding positional bonuses from well-known tablebases. They cost essentially zero compute but dramatically improve play quality — knights centralize, pawns advance, kings seek safety. This is the most efficient evaluation improvement possible.

### 4. 8×8 integer array board representation
Bitboards (the preferred representation for high-performance engines) are complex to implement and overkill for this use case. An 8×8 integer array (matching the BASIC chess demo) is readable, trivially debuggable, and fast enough for depth 4–5 on modern hardware. The Zobrist hash is maintained incrementally (updated on makeMove/unmakeMove) for transposition table and repetition detection.

### 5. 80×80 squares, 960×700 window
80px squares give enough canvas for a visually detailed 70px piece sprite with 5px padding on each side. 960 wide leaves 320px for the side panel — enough for a readable move history list, captured pieces, clock, and controls without crowding. This fits comfortably in any 1080p display and even most 900p laptops.

### 6. Both click-to-move AND drag-and-drop
Click-to-move (click piece, click destination) is accessible for trackpad users and new players. Drag-and-drop (press, drag, release) is the expected interaction for mouse users. Both are implemented through the same `selectedSq` / `dragFrom` state and share all move validation logic — the extra cost is minimal and the UX improvement is significant.

### 7. Zia entity architecture matching Pac-Man demo
The Pac-Man demo (`demos/zia/pacman/`) is the most mature Zia game in the repo. Its entity-per-concern architecture (Game, Player, Ghost, Maze, Config as separate entities/modules) is the established idiom for Zia games. The chess game follows this exactly: Board, MoveGen, Evaluator, AI, Renderer, and Game are all separate entities, each in its own file, imported via `bind`. This makes each component independently readable and testable.

### 8. State machine for game flow
Chess has well-defined discrete states (menu, playing, awaiting promotion, AI thinking, game over). A simple integer `gameState` field switched in the update loop (following the Pac-Man pattern) handles all transitions cleanly without complex control flow. The AI thinking state is important — the main loop continues rendering while the AI computes, preventing the window from freezing.
