# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.3. Content is subject to change before
> the official release.

## Version 0.2.3 - Pre-Alpha (March 2026) — DRAFT

### Release Overview

Version 0.2.3 is a major hardening, tooling, and infrastructure release. Highlights:

- **3D Graphics Engine** — 28-class engine with scene graph, skeletal animation, physics, particles, terrain, post-processing, and FBX loading. Four backends: Metal, D3D11, OpenGL, software rasterizer.
- **Native Assembler + Linker** — zero external tool dependencies. Source → executable using only Viper's own code. DWARF v5 debug info, ICF, branch trampolines, Mach-O two-level namespace.
- **Enum Types** — for both Zia and BASIC, with match exhaustiveness checking.
- **Language Server** — dual-protocol (LSP + MCP) `zia-server` for editors and AI assistants.
- **AArch64 Performance** — post-RA scheduler, register coalescer, loop-invariant hoisting, cross-block store-load forwarding, division strength reduction. **24-87% benchmark improvements** on Apple M4 Max.
- **3 New IL Optimizer Passes** — EH optimization, loop rotation, reassociation. Full O2 pipeline restored (10 passes were missing from native codegen).
- **Interactive REPL** — `viper repl` for both Zia and BASIC with tab completion, persistent history, and meta-commands.
- **Comprehensive Safety Audits** — VM, codegen, runtime, network, and concurrency hardening with TSan verification.
- **Backend Codegen Review** — decomposed monolithic files, CFG-aware register allocation liveness, shared infrastructure across both backends.
- **Game Engine Framework** — GameBase/IScene, screen transitions, A* pathfinding, SaveData, DebugOverlay, SoundBank/Synth.
- **Frontend Decomposition** — shared utilities, parser/lowerer file splits, CRTP LexerBase.
- **Codebase Reorganization** — demos consolidated into `examples/`, devdocs merged into `docs/`, 900+ doc fixes.
- **Website** — Solarized dark/light theme, 66-page project site.

#### By the Numbers

| Metric | Value |
|--------|-------|
| Commits | 82 |
| Files changed | 3,322 |
| Lines added | ~245K |
| Lines removed | ~321K |
| Source files | 2,637 |
| Runtime classes | 272 |
| Codebase | ~700K LOC |

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

#### Native Assembler and Linker

A complete in-process assembler and linker that eliminates all external tool dependencies (`as`, `ld`,
`link.exe`) from the native compilation pipeline. Viper can now go from source to executable using
only its own code — zero external dependencies, true to the project philosophy.

**Assembler (MIR → .o)**

- Binary encoders for both x86-64 (49 opcode encodings, REX/ModR/M/SIB, RIP-relative relocations)
  and AArch64 (70+ opcodes, branch resolution, FP literal materialization)
- Object file writers for ELF (x86_64 + AArch64), Mach-O (x86_64 + AArch64), and PE/COFF (Windows)
- Per-function text sections for dead stripping
- `--native-asm` / `--system-asm` CLI flags to choose pipeline

**Linker (.o + archives → executable)**

- Archive reader supporting GNU ar, BSD ar, and COFF archive formats
- Object file readers for ELF, Mach-O, and COFF with unified `ObjFile` API
- Symbol resolution with weak/strong precedence and archive demand-pull
- Section merging with ObjC metadata preservation and page-aligned layout
- Relocation application for x86-64 and AArch64 (PC-rel, absolute, ADRP/PageOff12)
- Executable writers for Mach-O (dyld bind opcodes, GOT generation), ELF (program headers,
  dynamic section), and PE (import tables, DOS stub)
- Native ad-hoc code signing with `CS_LINKER_SIGNED` flag (arm64 macOS)
- `--native-link` / `--system-link` CLI flags

**Optimizations**

- Mark-and-sweep dead section stripping (`DeadStripPass`) — ~53% binary size reduction
- Cross-module string deduplication (promotes duplicate LOCAL rodata to shared GLOBAL symbols)
- Segment-level VA packing instead of per-section page alignment
- Debug/metadata section filtering

**Debug Info & Advanced Linker Features**

- DWARF v5 `.debug_line` section encoding via `DebugLineTable` — maps machine instructions back
  to source file, line, and column for debugger integration
- Non-alloc debug section handling across all three executable writers:
  ELF (`SHT_PROGBITS` without `SHF_ALLOC`, emitted after `PT_LOAD` segments),
  Mach-O (`__DWARF` segment with `vmaddr=0`, no VM permissions),
  PE (`IMAGE_SCN_MEM_DISCARDABLE`, excluded from `SizeOfImage`)
- Identical Code Folding (ICF) pass — merges functions with byte-identical `.text` content and
  matching relocations, reducing binary size for template-heavy or copy-heavy codegen output
- Branch trampoline generation for out-of-range relocations — automatically inserts veneer stubs
  when branch targets exceed architecture-specific displacement limits (±128MB on AArch64,
  ±2GB on x86-64)
- `OutputSection.alloc` flag distinguishes loadable sections from debug/metadata sections

**Mach-O Two-Level Namespace**

- Add `MH_TWOLEVEL` flag to Mach-O header, replacing flat namespace resolution. Required for
  macOS ARM64 — without it, CoreAnimation PAC pointer authentication traps fire ~1 second after
  window creation
- Per-symbol dylib ordinal assignment in bind opcodes using
  `BIND_OPCODE_SET_DYLIB_ORDINAL_IMM`/`ULEB` instead of flat lookup
- Flat lookup (ordinal -2) fallback for `OBJC_CLASS_$`/`OBJC_METACLASS_$` symbols whose defining
  framework can't be determined by prefix alone
- Fix dylib detection to strip leading underscores before prefix matching (handles
  `_objc_empty_cache`, `__CFConstantStringClassReference`)
- Update nlist library ordinals in symbol table for two-level namespace compatibility

**Hardening**

- Archive member size limits, relocation bounds checks, section/symbol count caps
- `isCStringSection` flag prevents string dedup from corrupting binary data sections
- Expanded dynamic symbol recognition to avoid false undefined-symbol errors

**Assembler & Linker Layer Review (15 items)**

Systematic review and refactoring of the native assembler/linker infrastructure:

- Reverse-index map for `findOutputLocation` (O(S×C) → O(1) lookup)
- Named relocation constants (`RelocConstants.hpp`) replacing magic numbers
- Shared `ObjFileWriterUtil.hpp` deduplicating 3× `appendLE`/`alignUp`/`padTo` implementations
- Centralized Mach-O name mangling (`NameMangling.hpp`)
- Extracted relocation classification (`RelocClassify.hpp`, 189 LOC)
- Immediate range validation asserts (AArch64 imm12, x86-64 imm32)
- Dead-strip statistics reporting to stderr
- Shared segment layout utilities (`classifySections`, `computeSegmentSpan`)
- Local symbol offset bounds check in `RelocApplier`
- Split `MachOExeWriter` (1,029 → 670 LOC) into `MachOCodeSign` + `MachOBindRebase` modules
- Data-driven framework detection (`kFrameworkRules[]` table replacing ad-hoc prefix checks)

All 11 demos build and run with the native pipeline. 13 new assembler tests, 6 string dedup tests,
11 debug line tests, 9 debug section tests, 10 ELF exe writer tests, 7 linker integration tests,
plus ICF, branch trampoline, symbol resolver, and relocation edge-case test suites.

#### Zia Language Server (zia-server)

A dual-protocol language server supporting both MCP (for AI assistants) and LSP (for editors):

- JSON value type + recursive-descent parser/emitter (zero dependencies)
- MCP transport (newline-delimited) with 11 tool definitions for AI assistants
- LSP transport (Content-Length framed) with diagnostics, completions, hover, and document symbols
- JSON-RPC 2.0 request/response handling
- `CompilerBridge` facade wrapping Zia compiler APIs
- VS Code extension with auto-discovery of `zia-server` binary

#### Zia Language Features

**Enum Types**

Enumeration types for both Zia and BASIC frontends:

```rust
enum Color { Red, Green = 5, Blue }
match c {
    Color.Red => Say("red")
    Color.Green => Say("green")
    _ => Say("other")
}
```

- Zia: `enum` declaration, explicit/auto-increment values, `expose` visibility, match exhaustiveness
  checking, variant access via dot notation
- BASIC: `ENUM...END ENUM` blocks, explicit/negative values, keyword-as-name collision handling
- Variants lowered as I64 constants; 16 Zia + 8 BASIC enum tests

**Match OR Patterns**

```rust
match x {
    1 | 2 | 3 => Say("small")
    10 | 20 => Say("round")
    _ => Say("other")
}
```

New `Pattern::Kind::Or` supports pipe-separated alternatives in match arms. Lowered as a waterfall
of test blocks — each subpattern's success jumps to the arm body, failure falls through to the next.

**Typed Catch with List Shorthand**

Exception handlers can now specify a type for the caught error, with list-literal shorthand syntax.

**Say/Print Auto-Dispatch**

`Say(42)`, `Say(3.14)`, `Say(true)` now work without explicit `.ToString()` conversion via typed
runtime variants (`SayInt`, `SayNum`, `SayBool`, `PrintBool`).

**.Len → .Length Rename**

Collection `.Len` property renamed to `.Length` across List, Map, and Set for consistency with the
language specification. `.Len` remains as an alias for backward compatibility.

#### Bible Audit Remediation

Three missing platform features identified by the comprehensive documentation audit:

- **Http.Put/Delete**: `Http.Put()`, `PutBytes()`, `Delete()`, `DeleteBytes()` completing the full
  REST verb set
- **Range iteration**: `.rev()` for reverse iteration and `.step(n)` for stepped iteration in
  `for i in range` loops
- **Color constants**: `Color.RED` through `Color.ORANGE` (10 named constants) as static properties

Plus 900+ documentation fixes across the Bible reference manual.

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

**Division/Modulo Strength Reduction**

Peephole pass that replaces expensive SDIV/UDIV/SREM/UREM instructions with cheaper sequences:

- `SDIV` by power-of-2: sign-corrected arithmetic shift (22 cycles → 4 cycles)
- `SDIV` by arbitrary constant: magic number multiply-high (22 cycles → 6-8 cycles)
- `UREM` by power-of-2: AND mask (26 cycles → 1 cycle)
- `SREM` by power-of-2: sign-corrected AND+SUB (26 cycles → 5 cycles)

**Full O2 Pipeline Restoration**

Restored 10 missing IL optimization passes to the native codegen O2 pipeline: loop-simplify,
loop-rotate, indvars, loop-unroll, check-opt, eh-opt, sibling-recursion, constfold, licm,
reassociate. The codegen pipeline was running a stripped-down O1-level optimizer despite requesting
`-O2`. Also fixed x86-64 codegen O2 gating threshold.

**Benchmark Results** (Apple M4 Max, native -O2 vs previous baseline):

| Benchmark | Before | After | Improvement |
|-----------|--------|-------|-------------|
| branch_stress | 180ms | 24ms | **-87%** |
| redundant_stress | 315ms | 104ms | **-67%** |
| mixed_stress | 83ms | 30ms | **-63%** |
| udiv_stress | 180ms | 74ms | **-59%** |
| inline_stress | 144ms | 79ms | **-46%** |
| call_stress | 45ms | 34ms | **-24%** |
| arith_stress | 140ms | 119ms | **-15%** |

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

Split the monolithic 2,750-line AArch64 `Peephole.cpp` into 6 focused sub-passes under `peephole/`:
`IdentityElim`, `StrengthReduce`, `CopyPropDCE`, `BranchOpt`, `MemoryOpt`, `LoopOpt`. Shared
peephole templates (`PeepholeDCE.hpp`, `PeepholeCopyProp.hpp`) parameterized on target traits are
used by both AArch64 and x86-64 backends.

#### x86-64 Backend Improvements

**Comprehensive Backend Codegen Review** — 20-item systematic review of both backends with
improvements across modularity, shared infrastructure, and test coverage:

**x86-64 Peephole Decomposition**

Split the 1,470-line monolithic `Peephole.cpp` into 4 focused sub-passes under `peephole/`:
`ArithSimplify` (MOV-zero→XOR, CMP-zero→TEST, strength reduction), `MovFolding` (redundant MOV
elimination), `DCE` (dead code elimination with implicit register tracking), `BranchOpt` (branch
optimization and cold block reordering). Peephole iteration now bounded by `kMaxIterations=100`.

**CFG-Aware Register Allocation Liveness**

Replaced the conservative "unconditional spill" hack (which force-spilled ALL cross-block vregs)
with proper backward dataflow liveness analysis. The new `LivenessAnalysis` class computes per-block
`liveIn`/`liveOut` sets using the standard fixed-point iteration, so only vregs that are *truly* live
across block boundaries get spill slots. This is the same algorithm used by production compilers.

**Shared Dataflow Solver**

Extracted the backward dataflow liveness algorithm into a shared template
(`common/ra/DataflowLiveness.hpp`) used by both x86-64 and AArch64 backends. The AArch64 allocator's
inline liveness code was extracted into a separate `Liveness` class (matching x86-64's pattern),
both now delegating to the shared solver. Also includes a shared `buildPredecessors()` utility.

**Shared Linker Utilities**

Common encoding utilities (`ExeWriterUtil.hpp`) shared between Mach-O and PE executable writers:
`writeLE16/32/64`, `writeBE32/64`, `writeULEB128`, `writePad`, `padTo`, and `resolveMainAddress()`.
ObjC dynamic stub generation extracted from the native linker into `DynStubGen.hpp/cpp`.

**Additional Improvements**

- `LoweringRuleTable.hpp` declarations moved to `.cpp` for faster incremental compile times
- AArch64 division handlers parameterized (4 near-identical → shared `lowerDivisionChk`)
- AArch64 `OpcodeDispatch` refactored with handler table replacing 1K-line switch
- AArch64 `AsmEmitter` consolidated with `emit2Op`/`emit3Op` primitives
- `SchedulerPass` hash maps replaced with `vector<optional<>>` for O(1) cache-friendly lookup
- `CopyPropDCE` `MovRR`/`FMovRR` logic parameterized (90% duplication eliminated)
- Shared prologue/epilogue iteration (`FrameCodegen.hpp`) eliminates callee-saved
  register save/restore duplication between AsmEmitter and BinaryEncoder
- x86-64 peephole fixed-point iteration with `kMaxIterations=100` bound

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

#### New Runtime APIs

**Canvas.DeltaTime** — Milliseconds elapsed since the last frame, available as a property. Combined
with `Canvas.SetDTMax()` for automatic delta-time clamping, this enables frame-rate-independent
physics without manual timer management.

**ParticleEmitter Rendering** — `ParticleEmitter.Draw()`, `DrawAt()`, and `DrawToPixels()` methods
for rendering particle systems directly, eliminating the need for manual particle array iteration in
game loops.

**Camera Parallax Layers** — `Camera.AddParallaxLayer()`, `RemoveLayer()`, `ClearLayers()`, and
`DrawParallax()` for multi-layer parallax scrolling backgrounds with configurable scroll speeds.

**Camera.SnapTo** — Instant camera repositioning for level transitions and respawn events, bypassing
the normal smooth-follow interpolation.

#### 3D Graphics Engine (Graphics3D)

A complete 3D graphics engine built as a zero-dependency C runtime module with pluggable GPU backends.

**Core Architecture**
- Scene graph with frustum culling and node hierarchy (`Scene3D`, `SceneNode3D`)
- Transform system with parent-child propagation (`Transform3D`)
- Material system with PBR properties, environment mapping, and emissive surfaces (`Material3D`)
- Camera system with shake, follow, and smooth interpolation (`Camera3D`)
- Light system with point, directional, and spot lights (`Light3D`)

**Rendering**
- Four backends: Metal (macOS), D3D11 (Windows), OpenGL (Linux), software rasterizer (fallback)
- Multi-pass rendering pipeline with alpha blending and depth sorting
- Render-to-texture for post-processing and offscreen rendering (`RenderTarget3D`)
- Post-processing effects: bloom, vignette, color grading, screen-space effects (`PostFX3D`)
- Skybox rendering with cubemap support (`CubeMap3D`)
- Instance batching for efficient rendering of repeated meshes (`InstanceBatch3D`)
- Decal projection (`Decal3D`), terrain rendering (`Terrain3D`), water simulation (`Water3D`)

**Animation & Physics**
- Skeletal animation with bone hierarchies and blend trees (`Skeleton3D`, `Animation3D`, `AnimBlend3D`, `AnimPlayer3D`)
- Morph targets for facial animation and mesh deformation (`MorphTarget3D`)
- Character controller combining skeletal animation and physics (`Character3D`)
- 3D physics with rigid bodies and trigger volumes (`Physics3DBody`, `Physics3DWorld`, `Trigger3D`)

**Navigation & Loading**
- Navigation mesh for AI pathfinding (`NavMesh3D`)
- Path system for waypoint-based movement (`Path3D`)
- FBX file loader for importing meshes, skeletons, and animations (`FBX`)
- Raycasting for object picking and interaction (`RayHit3D`)
- Particle systems (`Particles3D`)

28 classes total. Documented in [Graphics3D Guide](../graphics3d-guide.md) and [Graphics3D Architecture](../graphics3d-architecture.md).

#### Website

A hand-crafted project website under `misc/site/` for GitHub Pages deployment:

- **66 pages**: Landing, docs hub, showcase gallery, feature pages, blog, learning paths
- **Solarized dark/light theme** with CSS custom properties (single `style.css` themes all pages)
- Syntax highlighting with Solarized accent palette
- Responsive layout, localStorage theme persistence
- Zero dependencies — plain HTML/CSS/JS

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
`rt_map_set`, `rt_set_add`. Replace `assert`-only bounds checks in `rt_arr_str` and `rt_arr_obj`
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

### Sidescroller Demo — "Nova Run" (Full Game)

The sidescroller demo evolved from a single-level tech demo into a complete 5-level platformer
("Nova Run") across 8 phases of development:

#### Content

- 5 themed levels: Training Grounds, Crystal Caverns, Volcanic Depths, Sky Fortress, Ancient Ruins
- 5 parallax backgrounds with round stars, atmospheric blur, and volumetric clouds
- 7 new tile types across 3 level themes with 3D bevels, crystal facets, and lava veins
- Gradient title screen with scrolling elements and pulsing animations
- Data-driven boss health bar and all 5 level names displayed in HUD

#### Sprite Art Overhaul

All entities redrawn using `DrawTriangle`, `DrawEllipse`, `DrawBezier`, `DrawThickLine`, `Blur`,
and `Tint` — replacing the original rectangles-only rendering. Includes 4-frame player run cycle
(was 2), 4-frame slime animation (was 2), and boss attack wind-up frame.

#### Physics & Systems

- Frame-rate-independent physics via DeltaTime scaling across all subsystems
- Gamepad support via `Action.BindPadButton` with integer button constants
- Persistent high scores via CSV file I/O
- 12 procedurally generated WAV sound effects with SoundManager entity integration
- ParticleEmitter-based effects replacing manual particle arrays
- Projectile particle trails for player and enemy bullets

#### Visual Effects

Landing dust, wall-slide sparks, jetpack flame, ambient lava glow, crystal shimmer, speed trail,
and level intro splash screens.

#### Bug Fixes

- Fix lock-free pool allocator race: reserve first slab block before sharing remainder with freelist
- Fix `TILE_BRIDGE` missing from `isSolid()` — players fell through bridges into spike pits
- Fix `PS_HURT` state stuck permanently — allow state transitions once iframes expire
- Fix turret/boss shared `etimer` corruption: hurt handler and AI timer cancelled each other out,
  leaving enemies stuck in permanent hurt state. Added hurt-state guards to turret and boss AI
- Clear nearby enemy bullets on enemy death to prevent ghost collision damage at dead turret
  positions (4-tile radius despawn)

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

#### Frontend Decomposition

Systematic 10-item improvement across BASIC, Zia, and shared frontend infrastructure:

**Shared Infrastructure (fe_common/)**
- Extract string escape processing to `EscapeSequences.hpp` (shared by both frontends)
- Extract diagnostic formatter to `DiagnosticFormatter.hpp`
- Replace O(n) linear searches with hash tables in BASIC parser/builtins

**Zia File Splits**
- `Parser_Expr.cpp` (1,804 LOC → 3 files)
- `Lowerer_Decl.cpp` (1,690 LOC → 3 files)
- `Lowerer_Expr_Complex.cpp` (1,386 LOC → 3 files)
- Extract entity layout helpers (`computeEntityFieldLayout`, `buildEntityVtable`,
  `inheritEntityMembers`) from monolithic `registerEntityLayout`

**BASIC Improvements**
- Consolidate member array field handling (4 bugs) into `MemberArrayResolver`
- Migrate BASIC Lexer to `LexerBase` CRTP (shared cursor management with Zia)

**Header Decomposition**
- Extract Zia type layout structs to `LowererTypes.hpp` (-195 LOC from header)
- Extract Zia Sema support types to `sema/SemaTypes.hpp` (-159 LOC)
- Extract BASIC `require*()` methods to `lowerer/LowererRuntimeRequirements.hpp`

**Type Coercion Unification**
- Unified numeric coercion logic between BASIC and Zia frontends

#### Large File Splits

Readability refactoring of the largest source files:

| Original | Split Into | Purpose |
|----------|-----------|---------|
| `BytecodeVM.cpp` | + `BytecodeVM_threaded.cpp` | Threaded interpreter extracted |
| `rt_graphics.c` | + `rt_canvas.c` + `rt_drawing.c` + `rt_drawing_advanced.c` + `rt_graphics_stubs.c` | Graphics rendering split (2,750 LOC) + stubs |
| `rt_network_http.c` | + `rt_http_url.c` | URL parsing extracted |
| `rt_tls.c` | + `rt_tls_verify.c` + `rt_tls_internal.h` | Cert verification extracted |
| `vg_ide_widgets.h` | Split into 6 focused sub-headers | Umbrella include preserved |
| `Peephole.cpp` (AArch64) | 6 sub-passes in `peephole/` directory | AArch64 peephole decomposition (2,750 LOC) |
| `Peephole.cpp` (x86-64) | 4 sub-passes in `peephole/` directory | x86-64 peephole decomposition (1,470 LOC) |
| `RegAllocLinear.cpp` | 8 files in `ra/` directory | AArch64 register allocator decomposition (1,478 LOC) |
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
| Zia/BASIC enums | 24 | 16 Zia + 8 BASIC enum tests |
| Native assembler | 13 | Binary encoder + object file writer tests |
| String dedup | 6 | Cross-module string deduplication tests |
| Symbol resolver | 13 | Weak/strong symbol resolution + archive demand-pull |
| Relocation edge cases | 8 | Out-of-range branch, overflow, and boundary tests |
| Dataflow liveness | 9 | Shared backward dataflow solver (31 assertions) |
| Encoding validation | 1 | Opcode coverage validation for encoding tables |
| x86-64 peephole | 1 | Peephole sub-pass integration test |
| DWARF debug line | 11 | Line table encoding, file/directory entries, sequences |
| DWARF debug sections | 9 | Non-alloc section handling across ELF/Mach-O/PE |
| ICF | 1 | Identical Code Folding correctness |
| Branch trampolines | 1 | Out-of-range relocation veneer generation |
| ELF exe writer | 10 | ELF executable output validation |
| Linker integration | 7 | End-to-end linker pipeline (multi-object, archives) |
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

### Runtime Consistency Audit

Systematic audit across all runtime C headers, `runtime.def` registrations, and viperlib documentation
to enforce naming, behavioral, and type-safety consistency.

#### C Function Naming (Phase 1)

Renamed 40+ C functions to follow `rt_<type>_<verb>` naming convention. Affected collections: List,
Set, Map, Stack, Queue, Deque, Ring, Heap, Seq, Bag, Bytes. Key renames include `Contains`→`Has`,
`Count`→`Length`, `Size`→`Length`, `IsEmpty` property additions, and `TryPop`/`TryPeek` safe-access
variants for Stack, Queue, Deque, and Heap.

#### runtime.def Registration (Phase 2)

Aligned all IL-level method names and signatures with their C implementations. Added missing
`RT_METHOD`/`RT_PROP`/`RT_ALIAS` entries for newly renamed functions and ensured every `RT_FUNC`
has corresponding class registrations.

#### Collection Behavioral Consistency (Phase 3)

Added missing operations to bring all collections to feature parity where semantically appropriate:
`Clone()` for Stack, Queue, Set, Map; `First()`/`Last()`/`Reverse()` for Ring; `TryPopFront()`/
`TryPopBack()` for Deque; `Items()` alias for Heap's `ToSeq()`.

#### Enum Adoption (Phase 4)

Converted 12 `#define` constant groups to `typedef enum { ... } rt_xxx_t;` enums for compile-time
type safety. Affected: screen effect types, path-follow modes, easing types, string builder status,
input grow results, JSON token types, and XML node types.

#### Enum Naming Unification (Phase 5)

Normalized 7 existing enum types from mixed naming styles (Style A tagged enums, PascalCase names)
to a consistent Style B convention: anonymous `typedef enum { ... } rt_xxx_t;` with `_t` suffix.

#### Boolean Return Type Consistency (Phase 6)

Fixed 15 runtime functions that returned `bool` or `int` for boolean results to consistently return
`int8_t`, matching the IL `i1` type. Affected: `rt_string_is_handle`, `rt_output_is_batch_mode`,
`rt_is_main_thread`, `rt_type_is_a`, `rt_type_implements`, `rt_box_equal`, sprite overlap/contains
functions, and GUI shortcut checks.

#### Documentation Alignment (Phase 7)

Updated viperlib collection documentation to reflect all API additions from Phases 1-3. Added
missing methods to sequential, maps-sets, and specialized collection docs. Updated `runtime.def`
table of contents to cover all 80+ sections.

---

### Codegen Bug Fixes

- **Two-level namespace PAC crash**: Native linker produced Mach-O binaries with flat namespace
  (missing `MH_TWOLEVEL`), causing CoreAnimation PAC pointer authentication traps on macOS ARM64
  ~1 second after window creation. Fixed with proper two-level namespace support and per-symbol
  dylib ordinal assignment
- **EH subsystem cleanup**: Auto-pop exception handler at dispatch, `TrapKind` enum alignment across
  VM and codegen layers for consistent exception handling semantics
- **AArch64 MOVN**: Binary encoder now emits `MOVN` for simple negative immediates (e.g., -1 through
  -65536) instead of a multi-instruction `MOVZ`/`MOVK` sequence, reducing code size
- **String dedup safety**: `isCStringSection` flag prevents string deduplication from corrupting
  binary data sections (rodata containing non-string data)
- **Native linker launch fixes**: Correct dynamic symbol recognition for Zia/graphics demo
  executables, preventing false undefined-symbol errors at link time

### Runtime Bug Fixes

- **TextCenteredScaled**: Fix swapped `scale`/`color` parameters in `rt_canvas_text_centered_scaled`
  that caused enormous near-black rectangles covering the screen during level intro overlays

### Graphics

- **Present-before-events**: Reorder `vgfx_update` to present the frame before polling events,
  fixing visual glitches on rapid input during scene transitions
- **macOS resize alpha**: Set opaque alpha (0xFF) on framebuffer clear during window resize,
  preventing transparent flicker artifacts on macOS
- **Linux X11**: Fix 32-bit TrueColor visual via `XMatchVisualInfo`; add RGBA→BGRA swizzle in
  presentation buffer for correct color rendering
- **Linux linking**: Add `-lX11` for graphics demos
- **macOS**: Guard frameworks behind `#if defined(__APPLE__)` in AArch64 backend

---

### Project Statistics

| Metric              | v0.2.2    | v0.2.3 (draft) | Change     |
|---------------------|-----------|----------------|------------|
| Codebase (LOC)      | ~700,000  | ~700,000       | (net zero after cleanup) |
| Source Files         | 2,288     | 2,637          | +349       |
| Runtime Classes      | 226       | 272            | +46        |
| Test Count          | 1,261     | 1,307          | +46        |
| Commits             | —         | 82             | —          |
| Files Changed       | —         | 3,322          | —          |
| Lines Added         | —         | ~245K          | —          |
| Lines Removed       | —         | ~321K          | —          |

> Net LOC is roughly flat because ~1,100 stale files were deleted (obsolete test fixtures,
> dead demos, devdocs, zia-review) while ~349 new source files were added across 3D graphics,
> game engine, native toolchain, and test suites.

---

### Breaking Changes

1. **Directory restructure**: `demos/` consolidated into `examples/`. Update any hardcoded paths.
2. **devdocs/ removed**: All developer documentation now lives under `docs/`.
3. **Runtime API renames**: 40+ C functions renamed to `rt_<type>_<verb>` convention. Key changes:
   `Contains`→`Has`, `Count`→`Length`, `Size`→`Length`, plus `IsEmpty` property additions.
   `runtime.def` registrations updated accordingly.
4. **Collection .Len → .Length**: User-facing `.Len` property renamed to `.Length` across List, Map,
   and Set. `.Len` retained as alias for backward compatibility.
5. **Boolean return types**: 15 runtime functions changed from `bool`/`int` to `int8_t` (matching
   IL `i1`). Affects C FFI callers using these functions directly.

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
┌──────────┐    ┌──────────────┐ ┌───────────────┐
│  IL VM   │    │   x86-64     │ │    AArch64    │
│ Bytecode │    │ 4 Peephole   │ │  PassMgr (NEW)│
│    VM    │    │ CFG Liveness │ │  Coalescer    │ (NEW)
│  REPL    │    │   (NEW)      │ │  Scheduler    │ (NEW)
│  (NEW)   │    └──────┬───────┘ │  Loop Hoist   │ (NEW)
└──────────┘           │         │  6 Peephole   │ (NEW)
                       │         └──────┬────────┘
                       └────────┬───────┘
                                ▼
                  ┌───────────────────────────┐
                  │  Shared Infrastructure    │
                  │  DataflowLiveness (NEW)   │
                  │  PeepholeDCE/CopyProp     │
                  │  ParallelCopyResolver     │
                  └────────────┬──────────────┘
                               ▼
                  ┌───────────────────────────┐
                  │  Native Assembler (NEW)   │
                  │  MIR → Binary Encoder     │
                  │  → Object File Writer     │
                  │  (ELF / Mach-O / PE)      │
                  └────────────┬──────────────┘
                               ▼
                  ┌───────────────────────────┐
                  │  Native Linker (NEW)      │
                  │  Symbol Resolution        │
                  │  Section Merging + ICF    │ (NEW)
                  │  Dead Strip + String Dedup│
                  │  DWARF Debug Info         │ (NEW)
                  │  Branch Trampolines       │ (NEW)
                  │  Two-Level Namespace      │ (NEW)
                  │  → Executable Writer      │
                  └───────────────────────────┘
```

---

### Feature Comparison

| Feature | v0.2.2 | v0.2.3 |
|---------|--------|--------|
| **3D Graphics Engine** | No | 28-class engine: Metal/D3D11/OpenGL/software |
| **Native Assembler/Linker** | External as/ld/link | In-process, zero deps, DWARF v5 |
| **Interactive REPL** | No | Full REPL for Zia and BASIC |
| **Enum Types** | No | Zia + BASIC, match exhaustiveness |
| **Language Server** | No | Dual MCP/LSP (zia-server) |
| **Game Engine Framework** | No | GameBase/IScene + 7 runtime APIs |
| **Runtime Classes** | 226 | 272 (+46) |
| **IL Optimizer Passes** | 35 | 38 (+EH-Opt, LoopRotate, Reassoc) |
| **Test Count** | 1,261 | 1,307 (+46 net) |
| **Full O2 Pipeline** | O1-level only | All 10 missing passes restored |
| **Benchmark Results** | — | 24-87% improvement (Apple M4 Max) |
| **AArch64 Peephole** | Monolithic (2750 LOC) | 6 sub-passes + shared templates |
| **x86-64 Peephole** | Monolithic (1470 LOC) | 4 sub-passes in peephole/ |
| **x86-64 Liveness** | Unconditional spill | CFG-aware backward dataflow |
| **AArch64 Scheduler** | No | Post-RA list scheduler + coalescer |
| **SipHash** | FNV-1a | SipHash-2-4 with OS CSPRNG seed |
| **ECDSA P-256** | OpenSSL-dependent | Pure C, MSVC-compatible |
| **Website** | No | 66-page Solarized site |
| **Sidescroller Demo** | 1 level, rectangles | 5 levels, sprite art, full game |
| **Codebase Organization** | demos/ + devdocs/ | Unified examples/ + docs/ |

---

### v0.2.x Roadmap

Remaining v0.2.x focus areas:

- Zia debugger integration with breakpoints and watch expressions
- Native code generation coverage expansion
- RISC-V backend exploration
- GUI library maturation (accessibility, additional widget types)
- Runtime API stability and performance improvements
- Website Phase 2: convert docs to HTML, search, interactive code samples
- Self-hosting compiler groundwork

---

*Viper Compiler Platform v0.2.3 (Pre-Alpha) — DRAFT*
*Target: March 2026*
*Note: This is an early development release. Future milestones will define supported releases when appropriate.*
