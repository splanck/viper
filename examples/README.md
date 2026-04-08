# Viper Examples

Showcase programs demonstrating the Viper compiler toolchain, [runtime library](../docs/viperlib/README.md), and language frontends. All examples can be run via the VM or compiled to native binaries.

---

## 🚀 Quick Reference

```sh
viper run examples/games/chess/       # Run a project directory
viper build examples/apps/paint/ -o paint  # Compile to native binary
./scripts/build_demos.sh              # Build all demos (outputs to examples/bin/)
scripts\build_demos.cmd               # Windows equivalent
```

---

## 🖥️ Applications

Full-featured applications built with [Zia](../docs/zia-reference.md).

| Project | Description | Highlights |
|---------|-------------|------------|
| [ViperSQL](apps/vipersql/) | PostgreSQL-compatible SQL database server | MVCC, WAL, B-tree indexes, PG wire protocol, vsql client, 70K+ lines |
| [ViperIDE](apps/viperide/) | Integrated development environment | [GUI](../docs/viperlib/gui/README.md) widgets, tabs, IntelliSense, project tree, build system |
| [Paint](apps/paint/) | Drawing application (MS Paint-style) | 8 tools (brush, pencil, eraser, fill, shapes), color palette, custom widgets |
| [WebServer](apps/webserver/) | Multi-threaded HTTP server | Routing, static files, JSON API, [thread pool](../docs/viperlib/threads.md) |
| [Varc](apps/varc/) | Archive utility | DEFLATE compression, AES [encryption](../docs/viperlib/crypto.md), checksums |
| [Telnet](apps/telnet/) | Telnet client and server | [TCP sockets](../docs/viperlib/network.md), session management, threading |

---

## 🎮 Games

Playable games showcasing [graphics](../docs/viperlib/graphics/README.md), AI, and [game engine](../docs/viperlib/game/README.md) patterns.

### Zia Games

| Project | Description | Highlights |
|---------|-------------|------------|
| [Chess](games/chess/) | Chess with AI opponent | Iterative deepening, alpha-beta, quiescence search, transposition table, drag-and-drop GUI |
| [XENOSCAPE](games/xenoscape/) | Metroidvania sidescroller | 25+ enemies, 10 levels, parallax, particles, lighting, abilities, saves (17K LOC) |
| [Pac-Man](games/pacman/) | Pac-Man with ghost AI | BFS pathfinding, scatter/chase/frightened modes, [Canvas](../docs/viperlib/graphics/canvas.md) rendering |
| [Centipede](games/centipede/) | Arcade centipede game | Multiple enemy types, particle effects, Canvas graphics |
| [Frogger](games/frogger/) | Frogger clone | Single-file implementation, ANSI terminal |
| [Graphics Show](games/graphics-show/) | Visual effects showcase | Fireworks, plasma, Mandelbrot, starfield, matrix rain, snake |
| [Fade Test](games/fade-test/) | Graphics transition test | Screen fade and transition effects |

### BASIC Games

| Project | Description | Highlights |
|---------|-------------|------------|
| [Chess](games/chess-basic/) | Chess with minimax AI | Alpha-beta pruning, ANSI board rendering |
| [Pac-Man](games/pacman-basic/) | Pac-Man with ghost AI | Chase/scatter/frightened FSM, ANSI rendering |
| [Centipede](games/centipede-basic/) | Terminal centipede | OOP entities, level progression |
| [Frogger](games/frogger-basic/) | Frogger clone | Multi-lane traffic, river mechanics, OOP |
| [VTris](games/vtris/) | Tetris | All 7 pieces with rotation, line clearing, level progression |
| [Monopoly](games/monopoly/) | Monopoly board game | 4 players (1 human + 3 AI), full board, property trading |

### Shared Game Library

The [`games/lib/`](games/lib/) directory provides reusable base classes for Zia games:

- **`GameBase`** (221 LOC) — Game loop, scene management, input handling, frame timing
- **`IScene`** — Scene interface for state-driven game architecture

---

## 🗃️ SQL Engine — BASIC

A [SQL database engine](sqldb-basic/) written entirely in [BASIC](../docs/basic-reference.md) (9,600 lines). Implements a complete pipeline: lexer, parser, executor, indexes, schema management, and a test suite.

---

## 🔍 API Audit

The [`apiaudit/`](apiaudit/) directory provides **systematic coverage of Viper runtime classes** across hundreds of source files. Most APIs have both a Zia and BASIC version; `graphics3d` is still more Zia-heavy for rendering demos, but shared runtime surfaces such as `Model3D`, `AnimController3D`, and `SceneNode3D` binding sync now ship with both Zia and BASIC samples.

Organized by namespace:

| Namespace | Coverage |
|-----------|----------|
| [collections](apiaudit/collections/) | Bag, Bytes, Heap, List, Map, Queue, Ring, Seq, Stack, TreeMap, Set, ... |
| [core](apiaudit/core/) | Box, Object, String |
| [crypto](apiaudit/crypto/) | Hash, KeyDerive, Rand, Aes, Cipher, Tls |
| [functional](apiaudit/functional/) | Option, Result, Lazy, LazySeq |
| [game](apiaudit/game/) | Collision, Grid2D, ObjectPool, ParticleEmitter, Pathfinder, ... |
| [graphics](apiaudit/graphics/) | Canvas, Color, Pixels, Sprite, Tilemap, Camera, ... |
| [gui](apiaudit/gui/) | App, Button, Checkbox, Label, Slider, TextInput, ... |
| [input](apiaudit/input/) | Keyboard, Mouse, Pad |
| [io](apiaudit/io/) | File, Dir, Path, Archive, Compress, ... |
| [math](apiaudit/math/) | Math, Random, Vec2, Vec3, Bits, BigInt, ... |
| [network](apiaudit/network/) | Http, Tcp, TcpServer, Udp, Dns, WebSocket, ... |
| [sound](apiaudit/sound/) | Audio, Music, Sound, Voice |
| [text](apiaudit/text/) | Csv, Json, Pattern, StringBuilder, Template, Uuid, ... |
| [threads](apiaudit/threads/) | Thread, Barrier, Gate, Monitor, RwLock, Channel, ... |
| [time](apiaudit/time/) | Clock, DateTime, Stopwatch, Countdown |

> Run the full audit: `./examples/apiaudit/run_audit.sh`

---

## 📘 BASIC Language Examples

The [`basic/`](basic/) directory contains 28 BASIC programs demonstrating language features:

- **Namespaces** — `namespace_demo.bas`: USING directives, cross-namespace inheritance
- **Control flow** — `select_case.bas`, `ex_elseif.bas`, `ex_not.bas`
- **I/O** — `ex_input_prompt_min.bas`, `ex_print_commas.bas`, `ex_print_semicolons.bas`
- **OOP** — [`oop/`](basic/oop/): collections, text processing, mixed reports
- **Math** — `monte_carlo_pi.bas`, `random_walk.bas`
- **Basics** — `ex1` through `ex6`: hello world, loops, arrays, conditionals

---

## ⚙️ IL Examples

The [`il/`](il/) directory contains 22 [Viper IL](../docs/il-guide.md) programs for VM development and testing:

- **Tutorials** — `ex1` through `ex6`: progressive IL feature demonstrations (hello, loops, tables, factorial, strings, heap arrays)
- **Benchmarks** — [`benchmarks/`](il/benchmarks/): VM and [optimizer](../docs/il-passes.md) performance tests (fib, arithmetic, branching, strings)
- **IL v1.2** — [`1.2/`](il/1.2/): block parameter features
- **Advanced** — `break_label.il`, `random_three.il`, `summary.il`, `trace_min.il`
- **Debugging** — `debug_script.il` + `debug_script.txt`: debugger integration demo

---

## 🔗 C++ Embedding

The [`embedding/`](embedding/) directory demonstrates embedding the Viper VM in C++ host applications:

| File | Description |
|------|-------------|
| `stepping_example.cpp` | Single-step debugger API |
| `register_times2.cpp` | Registering native C++ functions as IL externs |
| `combined.cpp` | TCO, extern registration, opcode counters, polling |

> See the [VM Guide](../docs/vm.md) for the full embedding API.

---

## 🔨 Building and Running

### Run a demo directly

```sh
viper run examples/games/chess/          # Zia game
viper run examples/apps/paint/           # Zia app
./build/src/tools/ilrun/ilrun examples/il/ex1_hello_cond.il  # IL program
```

### Build all demos to native binaries

```sh
# macOS / Linux
./scripts/build_demos.sh
./scripts/build_demos.sh --clean   # Clean rebuild

# Windows
scripts\build_demos.cmd
scripts\build_demos.cmd --clean
```

Native binaries are output to `examples/bin/`.

### Build a single demo

```sh
viper build examples/apps/paint/ -o paint
./paint
```
