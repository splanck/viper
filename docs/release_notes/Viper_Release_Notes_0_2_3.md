# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.3. Content is subject to change before
> the official release.

## Version 0.2.3 - Pre-Alpha (March 2026) — DRAFT

### Release Overview

Version 0.2.3 is a hardening and infrastructure release. Rather than adding major new user-facing
features, this cycle focused on production readiness: comprehensive safety audits across every layer
(VM, codegen, runtime, network), concurrency hardening with TSan verification, three new IL optimizer
passes, major AArch64 backend performance work (post-RA scheduler, register coalescer, loop-invariant
hoisting, cross-block store-load forwarding), an interactive REPL for both languages, a multi-language
benchmark suite, and a large-scale codebase reorganization that consolidates documentation and examples
into clean hierarchies.

1,802 files changed across 32 commits. ~47K lines added and ~70K lines removed. 437 stale files
deleted and 81 new files added. Test count increased from 1,261 to 1,272 (+11 net; large stale test
fixture deletion offset by new coverage).

---

### New Features

#### Interactive REPL

A full-featured interactive REPL accessible via `viper repl`, `zia` (no args), or `vbasic` (no args).
Built on the TUI framework's TerminalSession/InputDecoder for raw-mode terminal I/O — zero external
dependencies.

- Expression auto-print with type-aware coloring (Bool/Int/Num/String/Object)
- Variable and function persistence across inputs via statement replay
- Entity, value, and interface definition support
- Multi-line input via bracket depth (Zia) and block keyword tracking (BASIC)
- Tab completion via CompletionEngine (Zia) and keyword matching (BASIC)
- Meta-commands: `.help`, `.quit`, `.clear`, `.vars`, `.funcs`, `.binds`, `.type`,
  `.il`, `.time`, `.load`, `.save`
- Persistent history (`~/.viper/repl_history_{lang}`), double Ctrl-C exit
- Non-interactive piped input mode
- Windows support via `_pipe`/`_dup`/`_dup2`/`_read` stdout capture
- 86 unit tests (51 Zia + 35 BASIC)

```bash
$ zia
zia> var x = 42
zia> x * 2
84 : Integer
zia> .il
; IL output for last expression...
```

#### IL Optimizer — Three New Passes

**EH Optimization (eh-opt)**

Removes redundant `eh.push`/`eh.pop` pairs when the protected region contains no
potentially-throwing instructions (calls, traps, checked operations). Dead handler blocks are
cleaned up by subsequent DCE/SimplifyCFG. Registered in the O2 pipeline after check-opt.

**Loop Rotation (loop-rotate)**

Converts while-style loops into do-while form by duplicating the header condition into the latch
block and inserting a guard for the initial check. Eliminates one branch per iteration and improves
LICM/unrolling opportunities. Conservative: only rotates single-latch, single-exit loops with pure
headers. Registered in the O2 pipeline after loop-simplify.

**Reassociation (reassociate)**

Canonicalizes operand order for commutative+associative integer ops (Add/Mul/And/Or/Xor). Placed
before EarlyCSE in the O2 pipeline to expose more common subexpression elimination opportunities.

#### IL Optimizer — Pass Improvements

- **EarlyCSE**: Fixpoint iteration (max 4 passes) for multi-pass CSE
- **LICM**: Refined load hoisting — allows non-escaping alloca loads past modifying calls via
  `BasicAA::isNonEscapingAlloca()` accessor
- **ValueKey**: Add ConstStr and GAddr to safe CSE opcodes, eliminating redundant address
  materializations in EarlyCSE/GVN
- **Verifier**: Replace reachability check with full Cooper-Harvey-Kennedy dominator computation
  for proper dominance verification of SSA uses
- **SCCP**: Block parameters treated as SSA φ-nodes merging only from executable predecessors
- **Inliner**: CallGraph SCC analysis (Tarjan) to prevent inlining mutually-recursive functions;
  cost model tuning (`maxCodeGrowth=2000`) and O1 pipeline integration
- **SimplifyCFG**: Convert hard verification asserts to soft early-returns for fuzz test
  compatibility
- **CheckOpt**: Remove incorrect overflow-to-plain demotion that violated IL spec (verifier
  requires signed arithmetic to use `.ovf` variants); overflow constant folding deferred to
  ConstFold pass
- **DCE**: Fix `predEdges` map invalidation after instruction removal (vector pointer stability)

#### Alloca Escape Verification

New Pass 4 in FunctionVerifier warns when a `ret` instruction directly returns an alloca-derived
pointer, catching dangerous stack-pointer escapes at verification time.

#### Zia Lowering Optimizations

- **Virtual dispatch**: O(N) if-else chain replaced with `SwitchI32` for multi-implementation
  method dispatch
- **Match expressions**: `SwitchI32` fast path for integer-only match arms without guards,
  falling back to generic lowering otherwise

#### Async/Await Syntax

New `async`/`await` keywords with AST nodes, parser support, semantic checking, and lowering to
`Future.Get` runtime calls:

```rust
func fetchData() -> String {
    var result = await Http.GetAsync("https://example.com/data");
    return result;
}
```

#### Multi-Language Benchmark Suite

Cross-language benchmark programs for performance comparison across C, C#, Java, Lua, Python, and
Rust. Covers arithmetic stress, branch stress, call stress, fibonacci, inline stress, mixed stress,
redundant stress, string stress, and unsigned division stress. Matching IL benchmark programs for
direct Viper performance measurement.

#### AArch64 Exception Handling

Full EH opcode support for the AArch64 backend: `EhPush`, `EhPop`, `EhEntry`, `TrapDiv`,
`TrapOvf`, `TrapIdx`, `TrapNull`, `TrapCast`, `ErrGetMsg`, `ErrGetCode`, `Resume`.

#### SipHash-2-4 Hash Function

Replace FNV-1a with keyed SipHash-2-4 using per-process random seed from OS CSPRNG for HashDoS
resistance in all runtime hash maps.

#### GC Epoch Tagging

Per-entry survival counter skips promoted objects in trial-deletion, with periodic full scans to
catch new cycles. Reduces GC overhead for long-lived objects.

#### AArch64 PassManager

Wire existing pass infrastructure into AArch64 `CodegenPipeline`, replacing monolithic per-function
loop. Brings the ARM64 backend architecture in line with x86-64.

#### AArch64 Performance Optimizations

Major performance work across the AArch64 backend, adding several new optimization passes:

**Post-RA Instruction Scheduler**

List scheduler running after register allocation with latency-based priority ordering and
anti-dependency tracking. Reorders instructions within basic blocks to improve pipeline utilization
while respecting data dependencies and register constraints.

**Register Coalescer**

Pre-regalloc `MovRR`/`FMovRR` elimination via live interval interference analysis (~270 LOC).
Reduces unnecessary register-to-register moves by merging compatible live ranges before linear scan
allocation. Integrated into the pipeline before the register allocator pass.

**MIR Loop-Invariant Constant Hoisting**

Hoists `MovRI` (move-immediate) instructions from loop bodies into preheader blocks when the
register is callee-saved (x19-x28) and defined only by `MovRI` with the same immediate value
throughout the loop. Uses natural loop body computation via reverse-reachability BFS from the latch
through predecessors, correctly handling non-contiguous loop bodies (blocks placed after the latch in
layout order).

**Cross-Block Store-Load Forwarding**

Peephole optimization that forwards stores to loads across basic block boundaries when the layout
predecessor has a single successor and the store/load access the same FP-relative offset. Includes
reachability verification to ensure the predecessor actually reaches the successor (unconditional
branch, conditional branch target, or fallthrough).

**Additional Peephole Optimizations**

- **CSET+branch fusion**: Fuses compare-and-set with subsequent conditional branch for tighter
  conditional sequences
- **Dead FP store elimination**: Removes redundant floating-point stores to the same stack slot
- **Leaf function register preference**: Prefers low-numbered (caller-saved) registers for leaf
  functions that don't need callee-saved save/restore
- **Logical immediate emission**: Proper AArch64 encoding for `AND`/`ORR`/`EOR` immediate operands
- **Dead overflow DCE**: Correct elimination of unused overflow-checked arithmetic results
- **SmulhRRR opcode**: New MIR opcode for proper signed multiply-high overflow detection

**Peephole Decomposition**

Split the monolithic 2,750-line `Peephole.cpp` into 6 focused sub-passes under `peephole/`:
`IdentityElim`, `StrengthReduce`, `CopyPropDCE`, `BranchOpt`, `MemoryOpt`, `LoopOpt`. Shared
peephole templates (`PeepholeDCE.hpp`, `PeepholeCopyProp.hpp`) parameterized on target traits are
used by both AArch64 and x86-64 backends.

#### Game Engine Framework (10-Item Improvement Plan)

A comprehensive game development infrastructure built as pure Zia libraries and C runtime additions:

**GameBase + IScene (Zia library)** — Reusable game loop framework. `GameBase` entity handles canvas
creation, frame pacing, DeltaTime clamping, and scene management. `IScene` interface defines
`update`/`draw`/`onEnter`/`onExit` lifecycle. Eliminates ~100 lines of boilerplate per game.

**Screen Transitions** — `transitionTo()` on GameBase orchestrates fade-out → scene switch → fade-in
using ScreenFX. Also adds `shake()` and `flash()` for screen effects.

**Action Presets (C runtime)** — `Action.LoadPreset("platformer")` loads standard input bindings in
one call. Four presets: `standard_movement`, `menu_navigation`, `platformer`, `topdown`.

**Canvas Frame Helpers (C runtime)** — `BeginFrame()` combines Poll + ShouldClose check.
`SetDTMax()` auto-clamps DeltaTime. Text layout: `TextCentered`, `TextRight`, `TextCenteredScaled`.

**SaveData (C runtime)** — Cross-platform key-value persistence. JSON storage in platform-appropriate
directories (macOS Application Support, Linux XDG, Windows AppData).

**DebugOverlay (C runtime)** — Real-time FPS/dt/watch variable overlay with color-coded FPS,
16-frame rolling average, and up to 16 custom watch entries.

**SoundBank + Synth (C runtime)** — Named sound registry maps string names to Sound objects.
Procedural synth generates tones, sweeps, noise, and 6 preset game SFX (jump/coin/hit/explosion/
powerup/laser) without WAV files. Uses Bhaskara I sine approximation and in-memory WAV generation.

**Platformer Showcase Demo** — ~600 LOC across 6 files demonstrating 11+ runtime APIs in one
cohesive game: Tilemap, Camera, CollisionRect, StateMachine, SpriteAnimation, ObjectPool,
PathFollower, ButtonGroup, DebugOverlay, ScreenFX, and Action presets.

**Demo Refactoring** — Sidescroller demo refactored to use StateMachine (replacing manual integer
state tracking) and ButtonGroup (replacing manual selection management).

---

### Comprehensive Safety Audits

The bulk of this release is a multi-phase safety audit touching every layer of the platform.

#### VM Execution Loop (8 fixes)

- Convert assert-only guards to runtime traps in branch target lookup, switch index, and EH handlers
- Add null guard for `prepareTrap` handler
- Fix `fr.func->name` null checks in trap diagnostics
- Add defensive operand checks for trap/EH ops

#### x86-64 Codegen (9 fixes)

- **EFLAGS clobber**: Guard MOV-zero→XOR peephole against EFLAGS clobber between TEST and CMOV
  (silent miscompilation for `select` with falseVal=0)
- **Block arguments**: Extend `EdgeArg` with `argValues` to carry full ILValue data for constant
  block arguments; materialize constants into fresh vregs via MOVri/MOVSDmr before PX_COPY
- **Peephole DCE**: Fix implicit register liveness for CQO (reads RAX) and IDIV/DIV (reads RAX+RDX)
  — DCE was incorrectly eliminating dividend loads
- **Deterministic labels**: Replace three static `uint32_t` counters with per-function
  `nextLocalLabelId()`, reset to 0 per function for output determinism
- **Parallel move resolution**: Replace entry parameter MOV loop with PX_COPY pseudo-instruction
  for topological sort and cycle-breaking (fixes crash in multi-param entity init calls)
- **GNU-stack marking**: Add `.note.GNU-stack` section to ELF output for non-executable stack
- **PE/COFF directives**: `.rdata` section and ELF `.type @function` in AsmEmitter
- **PIE support**: `-pie` flag for Linux linker

#### AArch64 Codegen (5 fixes)

- **Dominator intersect guard**: Add runtime guard in `Dominators.cpp intersect()` for release builds
- **AsmEmitter refactor**: Extract `resolveBaseOffset()` helper from four duplicated base+offset
  load/store functions
- **Loop body BFS over-inclusion**: Fix natural loop body computation crawling backwards past
  the header for BASIC two-header for-loop patterns, causing compilation crashes (SIGBUS)
- **SLF reachability**: Add predecessor reachability verification for cross-block store-load
  forwarding to prevent incorrect forwarding when the layout predecessor diverges
- **Fast-path param stores**: Emit param-to-alloca stores for >8-argument functions in the call
  fast path, fixing uninitialized stack reads in `lowerCallWithArgs`

#### Runtime — Resource Lifecycle (10 fixes)

- GC epoch-counter overflow: `uint8` → `uint32` (rt_gc.c)
- String intern table dangling pointer on GC collect (rt_string_intern.c)
- TLS session cache stale pointer after context free (rt_tls.c)
- Parallel worker thread-local cleanup on join (rt_parallel.c)
- PkgDeflate double-free on realloc failure (PkgDeflate.cpp)
- ThreadsRuntime detached-thread resource leak (ThreadsRuntime.cpp)
- File watcher handle leak on rewatch (rt_watcher.c)
- Audio waveOut handle leak on close (vaud_platform_win32.c)
- Closure env pointer offset extracted to named constants with `static_assert`
- GC shutdown flag guard for Windows re-init safety

#### Runtime — Capacity Overflow Guards (16 files)

Integer overflow guards before capacity doubling in seq, deque, map, set, bag, bimap, countmap,
defaultmap, frozenmap, frozenset, intmap, multimap, sortedset, sparsearray, treemap, and weakmap.
Prevents undefined behavior when capacity approaches `INT64_MAX/2` or `SIZE_MAX/2`.

#### Runtime — String Operations (12 sites)

Replace `strlen()` with `rt_string_len_bytes()`/`rt_str_len()` in 5 case-conversion functions,
2 LIKE functions, box hash, bloomfilter, bimap/bag helpers, and playlist operations.

#### Runtime — File Operations (8 fixes)

- 32-bit `ftell`/`fseek` overflow on Windows → 64-bit equivalents (linereader, PNG loader)
- Delete corrupt PNG file on partial write failure
- Check `fclose()` return in HTTP download, remove partial file on failure
- Validate stream state after `Serializer::write`
- Check write result in `ArWriter::finishToFile`

#### Runtime — Allocation Failure Handling

Replace silent data-dropping on allocation failure with `rt_trap` in `rt_list_push`, `rt_map_new`,
`rt_map_set`, `rt_set_put`. Replace `assert`-only bounds checks in `rt_arr_str` and `rt_arr_obj`
get/put with `rt_trap` + `rt_arr_oob_panic`.

#### Runtime — Type System (5 fixes)

- Zia `lowerAs()` missing numeric conversion instructions
- Assignment coercion for Number↔Integer fields and vars
- Checked `fptosi` (`CastFpToSiRteChk`) with NaN/overflow guards
- Separate `CastFpToSiRteChk` from `Fptosi` in lowering pipeline

#### Network & TLS Security (8 findings)

- Native ECDSA P-256 verification for Linux TLS CertificateVerify (no OpenSSL dependency)
- WebSocket fragmentation reassembly capped at 64MB with RFC 6455 close code 1009
- TLS transcript buffer increased from 8KB to 32KB for large cert chains
- MSVC-compatible 128-bit arithmetic for ECDSA P-256 (`_addcarry_u64`, `_subborrow_u64`,
  `_umul128` replacing `__uint128_t`)
- TOML parser nesting depth limit (`kMaxTomlDepth=128`)
- BASIC parser `peek()` distance limit (`kMaxPeekDistance=1000`)
- Volatile memset for crypto key material wiping (HKDF, TLS)
- Replace `atoi()` with `strtol()` + range validation in WebSocket port parsing

#### Concurrency Hardening (TSan-verified)

- Pool allocator: `block->next` pointer uses atomic load/store to prevent ARM64 memory ordering
  corruption in lock-free freelist
- SipHash: Fix non-atomic `rt_siphash_seeded_` fast-path check causing stale seed reads on ARM64
  weak memory model
- GC: Fix non-atomic `g_shutdown_registered` causing potential double `atexit` registration
- GC: Remove TOCTOU race by eliminating unnecessary unlock/relock gap in snapshot
- Init races: `PTHREAD_MUTEX_INITIALIZER` / `InitOnceExecuteOnce` for all global init paths
- ARM64 barriers: `__dmb(_ARM64_BARRIER_ISH)` in rt_platform.h + rt_pool.c

---

### Zia Compiler Fixes

#### P0 Bugs

- **BUG-EH-001**: Exception handler param types corrected (Ptr/I64 → Error/ResumeTok)
- **BUG-OPT-001**: `Optional<String>` mapping corrected (Str → Ptr for nullable boxing)

#### P1 Bugs

- **BUG-MATCH-002**: String match return type (I64 → I1)
- **BUG-MATCH-003**: Boolean match with `Zext1` before `ICmpEq`
- **BUG-MATCH-001**: Negative literal patterns in match arms

#### P2-P3 Bugs

- **BUG-VAL-001**: Value type String field init/copy (raw ptr store + retain)
- **BUG-OPT-002**: Nested coalescing inner type derivation
- **BUG-IL-001**: Save/restore IRBuilder temp counter around lambda lowering
- **BUG-IL-002**: Type annotations for Dynamic result types in IL serializer; add `call.indirect`
  parser/serializer support

#### Sema Fixes

- Guard-clause narrowing interaction: assignment type checking uses original declared type
- Clear narrowing on reassignment to prevent stale type info
- Force-unwrap allows reference/nullable types after narrowing
- `Optional<String>` maps to IL Str type (not Ptr) matching runtime externs
- Call instructions use extern-declared return type for IL verification
- Match exhaustiveness checking (W019)
- Escape analysis in BasicAA (non-escaping allocas vs Param → NoAlias)

---

### Sidescroller Demo Improvements

- Fix lock-free pool allocator race: reserve first slab block before sharing remainder with freelist
- Fix `TILE_BRIDGE` missing from `isSolid()` — players fell through bridges into spike pits
- Fix `PS_HURT` state stuck permanently — allow state transitions once iframes expire
- Add 12 procedurally generated WAV sound effects: jump, shoot, coin, hurt, stomp, enemy death,
  powerup, checkpoint, level complete, death, menu select, menu move
- SoundManager entity integration

---

### Codebase Reorganization

#### Directory Consolidation

All demos and examples consolidated under a unified `examples/` root:

```text
examples/
├── apps/       (viperide, paint, sqldb, webserver, varc, telnet)
├── games/      (chess, pacman, centipede, sidescroller, frogger, vtris, ...)
├── embedding/  (C++ VM integration examples)
├── apiaudit/   (runtime API verification)
├── basic/      (BASIC language examples)
├── il/         (IL benchmark and test programs)
└── sqldb-basic/ (SQL demo in BASIC)
```

~80 trivial/redundant/dead demo files removed. 5 entire directories deleted (gfx_centipede,
gui_test, particles, classes, vedit). 7 broken/redundant scripts removed.

#### Documentation Consolidation

- Eliminated `devdocs/` directory: 28 files merged into `docs/` tree
- 25 stale devdocs files deleted (outdated plans, superseded specs)
- 117 broken relative links fixed across docs/
- `docs/README.md` rebuilt as clean navigation hub
- 286 obsolete test sources removed from `docs/bugs/bug_testing/`
- YAML frontmatter added to 95 documentation files
- 548 bare code blocks tagged with language identifiers
- Terminology standardized: "standard library" → "ViperLib", "interpreter" → "VM"

#### Large File Splits

Readability refactoring of the largest source files:

| Original | Split Into | Purpose |
|----------|-----------|---------|
| `BytecodeVM.cpp` | + `BytecodeVM_threaded.cpp` | Threaded interpreter extracted |
| `rt_graphics.c` | + `rt_canvas.c` + `rt_drawing.c` + `rt_drawing_advanced.c` + `rt_graphics_stubs.c` | Graphics rendering split (2,750 LOC) + stubs |
| `rt_network_http.c` | + `rt_http_url.c` | URL parsing extracted |
| `rt_tls.c` | + `rt_tls_verify.c` + `rt_tls_internal.h` | Cert verification extracted |
| `vg_ide_widgets.h` | Split into 6 focused sub-headers | Umbrella include preserved |
| `Peephole.cpp` | 6 sub-passes in `peephole/` directory | AArch64 peephole decomposition (2,750 LOC) |
| `Lowerer.hpp` | + `LowererTypes.hpp` + `LowererSymbolTable.hpp` + `LowererTypeLayout.hpp` | Zia lowerer decomposition foundation |

#### Cross-Platform Improvements

- Network tests (`RTNetworkHardenTests`, `RTNetworkTimeoutTests`) ported to Windows via
  `sock_t`/`SOCK_CLOSE`/`SOCK_INVALID` abstractions + WinSock2
- Windows build script parity improvements

#### Zia Review Cleanup

Removed `zia-review/` directory (review complete). All findings resolved and tracked in main
issue tracker.

---

### Testing

#### New Test Coverage

| Suite | Tests Added | Description |
|-------|-------------|-------------|
| Zia parser errors | 25 | Parser negative/error-recovery tests |
| Zia lexer | 62 | Lexer edge-case unit tests |
| REPL | 86 | 51 Zia + 35 BASIC REPL tests |
| x86-64 determinism | 357 | Backend output determinism stress (8 scenarios, 407 compilations) |
| rt_map | 24 | Comprehensive map tests |
| rt_pool | 11 | Pool allocator tests |
| VM equivalence | 5 | VM vs BytecodeVM output equivalence |
| Zia frontend | 75 | 35 parser + 24 sema + 16 lowerer unit tests |
| Fuzz harnesses | 2 | libFuzzer harnesses for Zia lexer and parser |

#### Determinism Stress Test

8 scenarios with 407 total compilations verify byte-identical assembly output:

- Repeated compilation (N=100): same module compiled 100 times
- RegAlloc pressure (N=50): 16 live vregs forcing spills
- RoData pool (N=50): string/f64 literals with dedup
- Multi-function ordering (N=50): 10 functions, insertion order
- Complex CFG (N=50): switch + nested if/else + while loop
- Separate construction: pointer-address independence
- ISel patterns (N=50): strength reduction hash map stability
- Static counter awareness: label normalization for known counters

---

### Graphics

- **Linux X11**: Fix 32-bit TrueColor visual via `XMatchVisualInfo`; add RGBA→BGRA swizzle in
  presentation buffer for correct color rendering
- **Linux linking**: Add `-lX11` for graphics demos
- **macOS**: Guard frameworks behind `#if defined(__APPLE__)` in AArch64 backend

---

### Project Statistics

| Metric              | v0.2.2    | v0.2.3 (draft) | Change     |
|---------------------|-----------|----------------|------------|
| C/C++ Source (LOC)  | ~1,000,000 | ~820,000*     | -180,000*  |
| C/C++ Source Files  | 2,288     | ~2,500         | +212       |
| Test Count          | 1,261     | 1,272          | +11        |
| Commits             | —         | 32             | —          |
| Files Changed       | —         | 1,802          | —          |
| Lines Added         | —         | 46,557         | —          |
| Lines Removed       | —         | 70,013         | —          |
| New Files           | —         | 81             | —          |
| Deleted Files       | —         | 437            | —          |

*\* Net LOC decreased due to deletion of 437 stale files (devdocs, dead demos, test fixtures,
zia-review). Test count is net +11 because 286 obsolete test fixture files were removed while
~300 new tests were added across Zia frontend, REPL, determinism, and pool allocator suites.*

---

### Breaking Changes

1. **Directory restructure**: `demos/` consolidated into `examples/`. Update any hardcoded paths.
2. **devdocs/ removed**: All developer documentation now lives under `docs/`.

---

### Architecture

```text
┌──────────────┐  ┌──────────────┐
│ BASIC Source │  │  Zia Source  │
│    (.bas)    │  │    (.zia)    │
└──────┬───────┘  └──────┬───────┘
       │                 │
       ▼                 ▼
┌─────────────────────────────────────────────────────┐
│                     Viper IL                        │
│      SimplifyCFG → SCCP → Reassociate (NEW)         │
│       → EarlyCSE → Mem2Reg → DSE (MemorySSA)        │
│       → CheckOpt → EH-Opt (NEW) → LoopRotate (NEW)  │
│       → Inliner                                     │
│                                                     │
│      Alloca Escape Verification (NEW)               │
└─────────────────────┬───────────────────────────────┘
                      │
      ┌───────────────┼───────────────┐
      ▼               ▼               ▼
┌──────────┐    ┌──────────┐    ┌───────────────┐
│  IL VM   │    │  x86-64  │    │    AArch64    │
│ Bytecode │    │  Native  │    │    Native     │
│    VM    │    └──────────┘    │  PassMgr (NEW)│
│  REPL    │                   │  Coalescer    │ (NEW)
│  (NEW)   │                   │  Scheduler    │ (NEW)
└──────────┘                   │  Loop Hoist   │ (NEW)
                               │  6 Peephole   │ (NEW)
                               └───────────────┘
```

---

### Feature Comparison

| Feature                    | v0.2.2               | v0.2.3 (draft)                     |
|----------------------------|----------------------|------------------------------------|
| Interactive REPL           | No                   | Full REPL for Zia and BASIC        |
| IL Optimizer Passes        | 35                   | 38 (+EH-Opt, LoopRotate, Reassoc)  |
| Test Count                 | 1,261                | 1,272 (+11 net)                    |
| Fuzz Harnesses             | No                   | Zia lexer + parser                 |
| Determinism Tests          | No                   | 357 checks, 407 compilations       |
| AArch64 EH Opcodes        | No                   | Full support                       |
| AArch64 PassManager        | No                   | Wired into CodegenPipeline         |
| AArch64 Post-RA Scheduler  | No                   | List scheduler with latency model  |
| AArch64 Reg Coalescer      | No                   | Pre-regalloc MovRR elimination     |
| AArch64 Loop Hoisting      | No                   | MovRI hoisting with BFS loop body  |
| AArch64 Cross-Block SLF    | No                   | Store-load forwarding across BBs   |
| AArch64 Peephole Sub-passes| Monolithic (2750 LOC)| 6 focused sub-passes + shared templates |
| SipHash (HashDoS resist.)  | FNV-1a               | SipHash-2-4 with OS CSPRNG seed    |
| GC Epoch Tagging           | No                   | Survival counter, promoted skip    |
| Async/Await (Zia)          | No                   | Parser + sema + Future.Get lower   |
| ECDSA P-256 (native)       | OpenSSL-dependent    | Pure C, MSVC-compatible            |
| Linux Graphics             | Broken colors        | Correct X11 TrueColor + BGRA      |
| Benchmark Suite            | No                   | 6 languages × 9 benchmarks         |
| Codebase Organization      | demos/ + devdocs/    | Unified examples/ + docs/          |

---

### v0.2.x Roadmap

Remaining v0.2.x focus areas:

- Zia debugger integration with breakpoints and watch expressions
- Native code generation coverage expansion
- RISC-V backend exploration
- GUI library maturation (accessibility, additional widget types)
- Runtime API stability and performance improvements
- Self-hosting compiler groundwork

---

*Viper Compiler Platform v0.2.3 (Pre-Alpha) — DRAFT*
*Target: March 2026*
*Note: This is an early development release. Future milestones will define supported releases when appropriate.*
