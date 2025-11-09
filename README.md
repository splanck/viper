# Viper

**Viper** is an **IL‑first compiler toolchain and virtual machine**.  
High‑level frontends—like the included BASIC compiler—lower programs into a strongly typed, SSA‑inspired intermediate language (**Viper IL**). The IL can be executed by the VM today, with native backends planned.

> Viper is an active, experimental project exploring IL design, multi‑frontend architectures, and interpreter micro‑architectures.

## Documentation

- IL Quickstart & Reference: [docs/il-guide.md](docs/il-guide.md)
- BASIC Language Guide: [docs/basic-language.md](docs/basic-language.md)
- Architecture Overview: [docs/architecture.md#cpp-overview](docs/architecture.md#cpp-overview)
- Code Map (components & roles): [docs/codemap.md](docs/codemap.md)
- ViperTUI (experimental): [tui/](tui/)

---

## Why Viper?

- **IL at the center.** A single, readable, typed IR makes semantics explicit and frontends interchangeable.
- **Human‑scale design.** The IL is meant to be *read and edited*; you can learn by inspecting disassembly without a microscope.
- **Composable toolchain.** Parsers → IL builder → verifier → VM all exist as standalone tools you can script.
- **Performance playground.** Switch vs table vs direct‑threaded dispatch lets you *feel* interpreter trade‑offs.
- **Teaching & research friendly.** Clear examples, golden tests, and a small surface area encourage experimentation.

---

## Feature Highlights

### Implemented
- **BASIC Frontend** — parser, semantic analysis, OOP features, and runtime integration.
- **Viper IL** — stable, typed, SSA‑style IR with a verifier.
- **Virtual Machine** — configurable dispatch:
  - `switch` — classic `switch` jump table
  - `table` — function‑pointer dispatch
  - `threaded` — direct‑threaded labels‑as‑values (requires GCC/Clang and build flag)
- **Runtime Libraries** — portable C for strings, math, and file I/O.
- **Tooling**
  - `ilc` — compile/run BASIC or IL programs
  - `il-dis` — disassemble IL binaries
  - `il-verify` — verify IR correctness and emit diagnostics
- **Examples & Tests** — curated examples (e.g., `SELECT CASE`) and extensive golden tests.
- **TUI subsystem** — experimental text‑UI widgets (`tui/`).

### In Progress / Planned
- Optimization passes for the IL
- Native code generation backends (e.g., x64)
- Debugger/IDE integration and richer developer tooling

---

## Quickstart

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run BASIC directly:

```bash
build/src/tools/ilc/ilc front basic -run examples/basic/ex1_hello_cond.bas
```

Run IL:

```bash
build/src/tools/ilc/ilc -run examples/il/ex1_hello_cond.il
```

---

## IL at a Glance

**BASIC**

```basic
10 LET X = 2 + 3
20 LET Y = X * 2
30 PRINT "HELLO"
40 PRINT Y
50 END
```

**Viper IL (abbreviated)**

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.NL = "\n"
global const str @.HELLO = "HELLO"

func @main() -> i32 {
entry:
  %x = add 2, 3
  %y = mul %x, 2
  call @rt_print_str(const_str @.HELLO); call @rt_print_str(const_str @.NL)
  call @rt_print_i64(%y);                call @rt_print_str(const_str @.NL)
  ret 0
}
```

This is the essence of Viper: **frontends lower to a typed IL that is compact, explicit, and easy to inspect.**

---

## Architecture at a Glance

```
[Frontend(s)]
   BASIC, future languages
        │
        ▼
   [IL Builder]  ──►  [il-verify]
   (typed, SSA-ish)     (correctness)
        │
        ▼
 [Virtual Machine]  ──►  (planned) [Native Backends]
  switch / table / threaded
```

- **Frontends** lower to a *common, typed* IL.
- **Verifier** enforces type, control‑flow, and lifetime constraints with precise diagnostics.
- **VM** executes IL; dispatch is swappable for experimentation.
- **Backends** (planned) will emit native code.

---

## Interpreter Dispatch & Configuration

Choose the dispatch loop at runtime with `VIPER_DISPATCH`:

| Value      | Notes |
|------------|------|
| `switch`   | Default portable dispatch; compilers generate jump tables. |
| `table`    | Function‑pointer dispatch. |
| `threaded` | Direct‑threaded (GCC/Clang labels‑as‑values). Requires building with `-DVIPER_VM_THREADED=ON`. Falls back if unavailable. |

If compiled with `-DVIPER_VM_THREADED=ON`, the VM upgrades the default to direct‑threaded automatically on supported compilers.

### Performance Note (When `threaded` Helps)

Direct‑threaded dispatch can reduce branch mispredictions and loop overhead in tight interpreter loops where **most time is spent in the dispatch itself**. Expect the most visible gains when:
- hot paths execute many small IL instructions,
- control flow is predictable (good I‑cache locality), and
- your platform supports labels‑as‑values (GCC/Clang).

Workloads dominated by I/O, syscalls, or heavy native library calls will see little difference because interpreter overhead isn’t the bottleneck.

---

## Tools

- **`ilc`** — frontends, compile, run
  - `ilc front basic -run examples/...`
  - `ilc -run examples/il/...`
- **`il-dis`** — disassemble IL binaries for inspection.
- **`il-verify`** — verify IR; diagnostics include function/block context.

---

## Project Layout

```
.
├─ src/         # VM, IL core, frontends, tools
├─ docs/        # IL spec, BASIC language, architecture
├─ examples/    # BASIC and IL programs
├─ tests/       # golden tests across layers
├─ tui/         # experimental text UI components
├─ cmake/       # CMake helpers and package exports
├─ scripts/     # dev and CI convenience scripts
└─ .github/     # CI workflows
```

- Browse key folders:
  - Source tree: [src/](src/)
  - Documentation: [docs/](docs/)
  - Examples: [examples/](examples/)
  - Tests: [tests/](tests/)
  - CMake helpers: [cmake/](cmake/)
  - Scripts: [scripts/](scripts/)

---

## Building, Installing, Uninstalling

### Build

```bash
# Configure (optional: enable direct‑threaded VM dispatch)
cmake -S . -B build -DVIPER_VM_THREADED=ON

# Build
cmake --build build -j

# Test
ctest --test-dir build --output-on-failure
```

### Install (macOS and Linux)

The project provides standard CMake install rules for tools and headers.

```bash
# Install to /usr/local (default on macOS/Linux)
sudo cmake --install build --prefix /usr/local

# Or install to a custom prefix (no sudo if you own the prefix)
cmake --install build --prefix "$HOME/.local"
```

What gets installed:
- Binaries: `ilc`, `il-verify`, `il-dis` → `${prefix}/bin`
- Headers (public): → `${prefix}/include/viper`
- Generated header: `version.hpp` → `${prefix}/include/viper`
- Man pages: `ilc(1)`, `il-verify(1)`, `il-dis(1)` → `${prefix}/share/man/man1`

Notes
- Add `${prefix}/bin` to your `PATH` and `${prefix}/share/man` to your `MANPATH` if needed.
- The TUI demo is not installed.

### Uninstall

Uninstall removes files recorded in `install_manifest.txt` from your last `cmake --install`.

```bash
cmake --build build --target uninstall
```

### Cleaning

```bash
# Remove built objects in the current build tree + source‑generated artifacts
cmake --build build --target clean-all

# Stronger cleanup: also remove build tree binaries and CMake cache/files
cmake --build build --target clean-dist
```

> The project targets **C++20** and builds with modern **CMake**. Direct‑threaded dispatch requires **GCC/Clang** for labels‑as‑values; other toolchains fall back to portable modes.

### Packages (macOS and Linux)

You can create native packages using **CPack** after install rules are in place.

```bash
cmake -S . -B build
cmake --build build -j

# Build packages (generator auto-selects per platform)
cmake --build build --target package
```

Outputs (in `build/`):
- macOS: `Viper-<version>-macos.pkg`
- Linux: `viper-<version>-<system>-<arch>.deb` and/or `.rpm` if the tooling is available

Install a package with your OS’s standard tools (e.g., `sudo dpkg -i …` or `sudo rpm -i …`).

---

## Compiling on Different Platforms

Viper is cross‑platform and uses standard CMake toolchain discovery. The canonical compiler is **Clang**; GCC is supported.

### macOS
- Use Apple Clang (installed with Xcode or Command Line Tools).
- The build enables `lld` automatically when available; falls back to system linker otherwise.
- On Apple Silicon (arm64), x86_64 codegen assemble/link tests are skipped by default.
- Install to `/usr/local` or a custom prefix: `sudo cmake --install build --prefix /usr/local`.

### Linux (Clang or GCC)
- Clang is recommended; GCC 11+ is supported.
- You can force a compiler with `CC`/`CXX`:
  ```bash
  CC=clang CXX=clang++ cmake -S . -B build
  cmake --build build -j
  ```
- The install step is identical to macOS (`cmake --install build --prefix …`).

### Windows
- Clang‑CL is preferred; MSVC may work but is not the primary configuration.
- POSIX‑specific tests and scripts are skipped or gated. If you primarily target Windows, consider building with LLVM’s `clang-cl` and Ninja.


---

## Extending Viper (adding a frontend)

1. **Parse** your language into an AST.
2. **Lower** to Viper IL using the IL builder (types and control‑flow made explicit).
3. **Verify** your IL with `il-verify`.
4. **Execute** via the VM (pick a dispatch), or—once available—emit native code with a backend.

Keep frontends thin: semantics live in the IL so the VM/backends can stay generic.

---

## Roadmap & Status

All components are under active development; icons reflect current maturity, not completion.

| Area                             | Status                                      |
|----------------------------------|---------------------------------------------|
| BASIC frontend + OOP             | 🔧 Available (actively evolving)            |
| VM (switch/table/threaded)       | 🔧 Available (actively evolving)            |
| Runtime libs (string/math/I/O)   | 🔧 Available (actively evolving)            |
| IL verifier & diagnostics        | 🔧 Available (actively evolving)            |
| TUI subsystem                    | 🧪 Experimental                              |
| IL optimization passes           | 🧩 In progress                               |
| Native codegen backends          | 🧪 Experimental (basic implementation)       |
| Debugger/IDE                     | ⏳ Planned                                   |

---

## Contributing

We’re glad you’re interested in Viper! This project is evolving quickly and the architecture is still in flux. You’re welcome to explore the code, file issues, and propose **small fixes or documentation improvements**. However, we’re **not currently seeking large feature PRs** while the core design stabilizes. Keeping the project cohesive is the priority. If you want to experiment more broadly, feel free to fork—Viper is MIT‑licensed.

---

 

## License

MIT License — a short summary:

- Permissive: you may use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the software.
- Conditions: include the copyright and permission notice in
  all copies or substantial portions of the software.
- Warranty: provided "AS IS", without warranty of any kind; the authors are
  not liable for any claim, damages, or other liability.

See the full text in [`LICENSE`](LICENSE).
