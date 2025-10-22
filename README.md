# Viper

**Viper** is an **IL‑first compiler toolchain and virtual machine**.  
High‑level frontends—like the included BASIC compiler—lower programs into a strongly typed, SSA‑inspired intermediate language (**Viper IL**).  
The IL can be executed by the VM today, with native backends planned.

> Viper is an active, experimental project exploring IL design, multi‑frontend architectures, and interpreter micro‑architectures.

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

---

## Building & Installing

```bash
cmake -S . -B build -DVIPER_VM_THREADED=ON   # optional; enables threaded dispatch where supported
cmake --build build -j
cmake --install build                        # installs libraries/tools if install rules are enabled
```

> The project targets **C++20** and builds with modern **CMake**. Direct‑threaded dispatch requires **GCC/Clang** for labels‑as‑values; other toolchains fall back to portable modes.

---

## Extending Viper (adding a frontend)

1. **Parse** your language into an AST.
2. **Lower** to Viper IL using the IL builder (types and control‑flow made explicit).
3. **Verify** your IL with `il-verify`.
4. **Execute** via the VM (pick a dispatch), or—once available—emit native code with a backend.

Keep frontends thin: semantics live in the IL so the VM/backends can stay generic.

---

## Roadmap

| Area                             | Status        |
|----------------------------------|---------------|
| BASIC frontend + OOP             | ✅ Done       |
| VM (switch/table/threaded)       | ✅ Done       |
| Runtime libs (string/math/I/O)   | ✅ Done       |
| IL verifier & diagnostics        | ✅ Done       |
| TUI subsystem                    | 🧪 Experimental |
| IL optimization passes           | 🧩 In progress |
| Native codegen backends          | ⏳ Planned    |
| Debugger/IDE                     | ⏳ Planned    |

---

## Contributing

We’re glad you’re interested in Viper! This project is evolving quickly and the architecture is still in flux.  
You’re welcome to explore the code, file issues, and propose **small fixes or documentation improvements**.  
However, we’re **not currently seeking large feature PRs** while the core design stabilizes. Keeping the project cohesive is the priority.  
If you want to experiment more broadly, feel free to fork—Viper is MIT‑licensed.

---

## Learn More

- **BASIC Language Guide** — `docs/basic-language.md`
- **BASIC OOP Guide** — `docs/basic-oop.md`
- **IL Quickstart & Reference** — `docs/il-guide.md`
- **Architecture Notes** — `docs/architecture.md`
- **ViperTUI** — `tui/`

---

## License

MIT — see `LICENSE`.