# vTRIS - Advanced Tetris Demo for Viper BASIC

A feature-rich, colorful Tetris implementation showcasing Viper BASIC's OOP capabilities, ANSI graphics, and game development features.

## Features

### 🎮 Complete Game Mechanics
- **7 Classic Tetromino Pieces** (I, O, T, S, Z, J, L)
- **Piece Rotation** with collision detection
- **Line Clearing** with proper animation
- **Floor Collision** (BUG-112 fixed!)
- **Game Over Detection**

### 🎨 Enhanced Visuals
- **Gradient Borders** - Yellow to cyan to green to blue
- **Colorful Pieces** - Each piece type has unique color
- **Pattern Background** - Subtle dot pattern in empty cells
- **Professional UI** - Clean sidebar with stats
- **ANSI Art Menu** - Eye-catching title screen

### 📊 Scoring System
- **Progressive Scoring**
  - 1 Line = 100 points
  - 2 Lines = 400 points (2²×100)
  - 3 Lines = 900 points (3²×100)
  - 4 Lines = 1600 points (4²×100)
- **All-Time High Scores** - Top 10 leaderboard
- **Default Champions** - HAL, GLaDOS, VIPER, BYTE, CODE

### 📈 Level System
- **Progressive Difficulty** - Speed increases every 10 lines
- **Level Display** - Current level shown in sidebar
- **Speed Scaling** - Gets faster with each level (minimum speed cap)

### 🎯 Gameplay
- **Next Piece Preview** - See what's coming
- **Live Stats** - Score, lines, level updated in real-time
- **Responsive Controls** - Smooth piece movement
- **Main Menu** - Professional game flow

## File Structure

```
demos/vTris/
├── README.md           ← You are here
├── vtris.bas          ← Main game file (600+ lines)
├── pieces.bas         ← Piece class (7 types + rotation)
├── board.bas          ← Board class (collision + rendering)
└── scoreboard.bas     ← High score system
```

## How to Run

```bash
cd /Users/stephen/git/viper

# Run the demo
./build/src/tools/ilc/ilc front basic -run demos/vTris/vtris.bas
```

## Controls

| Key | Action |
|-----|--------|
| **A** / **D** | Move piece left/right |
| **W** | Rotate piece clockwise |
| **S** | Soft drop (move down faster) |
| **Q** | Quit to menu / Quit game |
| **1-3** | Menu selection |

## Menu Options

1. **New Game** - Start playing
2. **High Scores** - View top 10 all-time scores
3. **Instructions** - View controls and scoring
4. **Q** - Exit to terminal

## Technical Highlights

### OOP Architecture
- **3 Main Classes** - Piece, Board, Scoreboard
- **Encapsulation** - Clean separation of concerns
- **Object Composition** - Pieces contain shapes, Board manages grid

### Bug Fixes Demonstrated
- **BUG-111 Workaround** - Uses class-level TempShape array for rotation
- **BUG-112 Fixed** - CheckLines now correctly checks rows 0-19 (not 1-20)

### ANSI Graphics
- **8 Colors Used** - Full color palette
- **Box Drawing** - ╔═╗║╚╝ characters
- **Unicode Blocks** - ██ for pieces, ·· for pattern
- **Cursor Control** - LOCATE for precise positioning
- **Color Gradients** - Border transitions through spectrum

### Code Statistics
- **~800 lines** of production BASIC code
- **15+ methods** across 3 classes
- **Complex algorithms**:
  - Matrix rotation (90° clockwise)
  - Collision detection (bounds + overlap)
  - Line clearing with gravity
  - High score insertion sort

## Development Notes

Created as a comprehensive stress test and demonstration of Viper BASIC's capabilities:
- ✅ OOP features (classes, methods, constructors)
- ✅ Arrays in classes (2D arrays, object arrays)
- ✅ ANSI terminal control (CLS, COLOR, LOCATE)
- ✅ Modular code (AddFile across 4 files)
- ✅ Game loop architecture
- ✅ Complex state management

## Known Limitations

- High scores use demo data (file I/O planned for future)
- Name entry defaults to "YOU" (input system enhancement planned)
- Pattern rendering may vary based on terminal font support

## Credits

**Game Design**: Classic Tetris mechanics
**Implementation**: Viper BASIC Demo
**Purpose**: Showcase compiler OOP capabilities
**Test Suite**: 15+ verification tests in `/bugs/bug_testing/`

## Version History

- **v2.0** (2025-11-19) - Enhanced demo version
  - Added main menu with ANSI art
  - Added scoreboard system
  - Added level progression
  - Enhanced graphics with gradients and patterns
  - Moved to /demos/vTris

- **v1.0** (2025-11-19) - Initial playable version
  - Basic Tetris gameplay
  - 7 piece types with rotation
  - Line clearing
  - Located in /examples/vTris

---

**Play Now:** `./build/src/tools/ilc/ilc front basic -run demos/vTris/vtris.bas`

Enjoy the game! 🎮
