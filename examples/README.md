# Viper Examples

Showcase programs demonstrating the Viper compiler toolchain, runtime, and language frontends.

## Applications (`apps/`)

Full-featured applications built with Zia.

| Project | Description | Features |
|---------|-------------|----------|
| [sqldb](apps/sqldb/) | PostgreSQL-compatible SQL database engine | MVCC, WAL, B-tree indexes, wire protocol, 60K+ lines |
| [viperide](apps/viperide/) | Integrated development environment | GUI widgets, tabs, IntelliSense, project tree, build system |
| [paint](apps/paint/) | Drawing application (MS Paint-style) | 8 tools (brush, pencil, eraser, fill, shapes), color palette, custom widgets |
| [webserver](apps/webserver/) | Multi-threaded HTTP server | Routing, static files, JSON API, thread pool |
| [varc](apps/varc/) | Archive utility | DEFLATE compression, AES encryption, checksums |
| [telnet](apps/telnet/) | Telnet client and server | TCP sockets, session management, threading |

## Games (`games/`)

Playable games showcasing graphics, AI, and game engine patterns.

| Project | Language | Description | Highlights |
|---------|----------|-------------|------------|
| [chess](games/chess/) | Zia | Chess with AI opponent | Iterative deepening, alpha-beta, quiescence search, transposition table, drag-and-drop GUI |
| [sidescroller](games/sidescroller/) | Zia | Platform game engine | Physics, camera, HUD, multiple enemy types, pickups, particles, menus |
| [pacman](games/pacman/) | Zia | Pac-Man with ghost AI | BFS pathfinding, scatter/chase/frightened modes, Canvas rendering |
| [centipede](games/centipede/) | Zia | Arcade centipede game | Multiple enemy types, particle effects, Canvas graphics |
| [graphics-show](games/graphics-show/) | Zia | Visual effects showcase | Fireworks, plasma, Mandelbrot, starfield, matrix rain, snake |
| [frogger](games/frogger/) | Zia | Frogger clone | Single-file implementation, ANSI terminal |
| [chess-basic](games/chess-basic/) | BASIC | Chess with minimax AI | Alpha-beta pruning, ANSI board rendering |
| [pacman-basic](games/pacman-basic/) | BASIC | Pac-Man with ghost AI | Chase/scatter/frightened FSM, ANSI rendering |
| [centipede-basic](games/centipede-basic/) | BASIC | Terminal centipede | OOP entities, level progression |
| [frogger-basic](games/frogger-basic/) | BASIC | Frogger clone | Multi-lane traffic, river mechanics, OOP |
| [vtris](games/vtris/) | BASIC | Tetris | All 7 pieces with rotation, line clearing, level progression |
| [monopoly](games/monopoly/) | BASIC | Monopoly board game | 4 players (1 human + 3 AI), full board, property trading |

## SQL Engine - BASIC (`sqldb-basic/`)

A SQL database engine written in BASIC (9,600 lines). Implements lexer, parser, executor, indexes, and a test suite.

## API Audit (`apiaudit/`)

Systematic coverage of every Viper runtime class. Each file exercises all public methods and properties of one API (available in both BASIC and Zia). Organized by namespace: `collections/`, `core/`, `crypto/`, `functional/`, `game/`, `graphics/`, `io/`, `math/`, `network/`, `text/`, `threads/`, `time/`.

## BASIC Language (`basic/`)

Selected BASIC language feature demonstrations:
- `namespace_demo.bas` - Namespaces, USING directives, cross-namespace inheritance
- `select_case.bas` - SELECT CASE patterns
- `oop/` - Object-oriented programming patterns

## IL Examples (`il/`)

Intermediate Language programs for VM development and testing:
- `ex1` through `ex6` - Progressive IL feature demonstrations
- `benchmarks/` - VM and optimizer performance benchmarks (fib, arithmetic, branching, strings)
- `1.2/` - IL v1.2 features (block parameters)

## C++ Embedding (`embedding/`)

Demonstrations of embedding the Viper VM in C++ host applications:
- `stepping_example.cpp` - Single-step debugger API
- `register_times2.cpp` - Registering native C++ functions as IL externs
- `combined.cpp` - TCO, extern registration, opcode counters, polling

## Building and Running

### Run a Zia demo directly
```sh
viper run examples/games/chess/
```

### Build all demos to native binaries
```sh
./scripts/build_demos.sh
# Outputs to examples/bin/
```

### Build a single demo
```sh
viper build examples/apps/paint/ -o paint
```
