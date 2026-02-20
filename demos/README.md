# Viper Demos

Example programs demonstrating the Viper platform. Zia is the flagship language — these demos show what real Viper applications look like.

---

## Zia Demos

All Zia demos are full project directories with a `viper.project` file. Run any demo directly from the project root:

```bash
# VM mode (immediate, no compilation step)
./build/src/tools/viper/viper run demos/zia/<demo>/

# Native binary (compile then run)
./build/src/tools/viper/viper build demos/zia/<demo>/ -o demos/bin/<demo>
./demos/bin/<demo>
```

Or build all Zia demos at once:

```bash
./scripts/build_demos.sh          # Build all demos as native binaries
./scripts/build_demos.sh --clean  # Clean and rebuild
```

Native binaries land in `demos/bin/`.

---

### ViperIDE — `demos/zia/viperide/`

A full-featured code editor built entirely in Zia, demonstrating the complete Viper GUI stack.

**Features:**
- Multi-tab document management with modified-file indicators
- Syntax-highlighted code editor with line numbers
- Integrated find/replace bar with match count and navigation
- Minimap for quick file navigation
- Breadcrumb path display
- Command palette for quick actions
- Integrated build system — compile and run Zia/BASIC files directly from the IDE
- File open/save dialogs with native OS integration
- Status bar with cursor position and language mode
- Toast notifications for build results

```bash
./build/src/tools/viper/viper run demos/zia/viperide/
```

---

### Paint — `demos/zia/paint/`

A canvas-based paint application with a full tool palette.

**Features:**
- Freehand brush, eraser, line, rectangle, ellipse, and fill tools
- Color picker with palette, HSV sliders, and hex input
- Adjustable brush size
- Undo/redo history
- Canvas zoom and pan
- Save/load images

```bash
./build/src/tools/viper/viper run demos/zia/paint/
```

---

### Pac-Man — `demos/zia/pacman/`

A complete Pac-Man arcade game using the Viper graphics engine.

**Features:**
- Tile-based maze rendering
- Four ghost types with distinct AI behaviors (chase, scatter, frightened)
- Animations, sprite sheets, sound cues
- Lives, scoring, and level progression

```bash
./build/src/tools/viper/viper run demos/zia/pacman/
```

---

### SQL Database — `demos/zia/sqldb/`

An embedded SQL database engine with a REPL interface.

**Features:**
- SQL parser and query executor
- CREATE TABLE, INSERT, SELECT (with WHERE, ORDER BY, LIMIT), UPDATE, DELETE
- Persistent storage with BinFile serialization
- B-tree indexing
- Interactive REPL with query history
- Both a BASIC and Zia implementation

```bash
./build/src/tools/viper/viper run demos/zia/sqldb/
```

---

### Web Server — `demos/zia/webserver/`

A multi-threaded HTTP server demonstrating Viper's networking and threading.

**Features:**
- HTTP/1.1 request parsing and response generation
- Multi-threaded request handling via thread pool
- Static file serving
- Configurable routing
- Request/response logging

```bash
./build/src/tools/viper/viper run demos/zia/webserver/
```

---

### Telnet — `demos/zia/telnet/`

A telnet client and server demonstrating TCP networking.

**Features:**
- `server.zia` — interactive shell with filesystem navigation (`ls`, `cd`, `cat`)
- `client.zia` — connects to any telnet endpoint with a live terminal session
- Multi-client support via threads

```bash
# Start the server
./build/src/tools/viper/viper run demos/zia/telnet/ --entry server.zia

# In another terminal, connect with the client
./build/src/tools/viper/viper run demos/zia/telnet/ --entry client.zia
```

---

### Centipede (Graphics) — `demos/zia/gfx_centipede/`

The classic arcade game implemented using the `Viper.Graphics.Canvas` API.

**Features:**
- Pixel-perfect sprite rendering
- Mushroom field, multi-segment centipede, spider and flea enemies
- Score tracking and level progression
- Keyboard input via `Viper.Input`

```bash
./build/src/tools/viper/viper run demos/zia/gfx_centipede/
```

---

### Graphics Showcase — `demos/zia/graphics_show/`

An interactive gallery showcasing the Viper graphics API capabilities.

**Features:**
- 2D primitives: lines, rectangles, circles, polygons
- Sprite animation and sprite sheets
- Tilemap rendering
- Camera transform and viewport
- Particle effects
- Pixel-level canvas manipulation

```bash
./build/src/tools/viper/viper run demos/zia/graphics_show/
```

---

### VEdit — `demos/zia/vedit/`

A lightweight text editor built with the Viper GUI widget library.

**Features:**
- Multi-line text editing with the CodeEditor widget
- File open and save via native dialogs
- Simple toolbar and status bar

```bash
./build/src/tools/viper/viper run demos/zia/vedit/
```

---

### VArc — `demos/zia/varc/`

A command-line archive utility demonstrating Viper's I/O and crypto modules.

**Features:**
- Create, list, test, and extract `.varc` archives
- DEFLATE compression via `Viper.IO.Compress`
- Optional AES-256 encryption via `Viper.Crypto.Aes`
- Recursive directory archiving

```bash
./build/src/tools/viper/viper run demos/zia/varc/
```

---

## BASIC Demos

BASIC demos run directly via `vbasic` or the unified `viper` driver:

```bash
./build/src/tools/viper/viper run demos/basic/<demo>/<main>.bas

# Or with the standalone interpreter
./build/src/tools/vbasic/vbasic demos/basic/<demo>/<main>.bas
```

| Demo | Entry Point | Description |
|------|-------------|-------------|
| [chess](basic/chess/) | `chess.bas` | Complete chess game with AI opponent, full rule set, ANSI board |
| [vtris](basic/vtris/) | `vtris.bas` | Tetris clone with levels, scoring, and colorful ANSI graphics |
| [pacman](basic/pacman/) | `pacman.bas` | Pac-Man with ghost AI, maze, and terminal graphics |
| [centipede](basic/centipede/) | `centipede.bas` | Centipede arcade game with ANSI colors and scoreboard |
| [frogger](basic/frogger/) | `frogger.bas` | Frogger road-crossing game with ANSI graphics and high scores |
| [monopoly](basic/monopoly/) | `monopoly.bas` | Monopoly board game: 1 human + 3 AI players, properties, trading |
| [particles](basic/particles/) | `main.bas` | Canvas-based particle system with physics and color fading |
| [sqldb](basic/sqldb/) | `sqldb.bas` | SQL database engine with REPL (BASIC version) |
| [classes](basic/classes/) | various | Language feature showcase: collections, crypto, I/O, math, fmt |
| [gui_test](basic/gui_test/) | `simple.bas` | Basic GUI widget integration test |

---

## Running as Native Binaries

Every demo (Zia and BASIC) compiles to a standalone native binary:

```bash
# Build a single demo
./build/src/tools/viper/viper build demos/zia/viperide/ -o demos/bin/viperide

# Build all demos at once (ARM64 macOS / Linux)
./scripts/build_demos.sh

# Build all demos (Windows x86-64)
scripts\build_demos.cmd
```

Pre-built binaries from the last run live in `demos/bin/`.

---

## See Also

- [Runtime Library Reference](../docs/viperlib/README.md) — complete API for all `Viper.*` classes
- [Zia Language Reference](../docs/zia-reference.md)
- [BASIC Language Reference](../docs/basic-reference.md)
- [Getting Started](../docs/getting-started.md)
