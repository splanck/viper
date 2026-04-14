# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.4 — Pre-Alpha (2026-04-13)

### Overview

v0.2.4 is the largest release in the project's history. The work falls into a handful of themes:

- **GPU backend maturity** — Metal and D3D11 reach feature parity with the software renderer; OpenGL gains shared infrastructure.
- **Native toolchain end-to-end** — A from-scratch PE/COFF linker and ELF dynamic linker eliminate the last `clang`/`link.exe` dependency for native compilation on all three platforms.
- **Worker-VM execution model** — Bytecode VMs spawned for `Thread.Start`, `Async.Run`, and HTTP callbacks now own snapshotted execution state and can outlive their parent.
- **3D engine + asset pipeline** — Procedural terrain, Gerstner water, vegetation, PBR materials, glTF/STL/FBX loaders, navigation agents, 3D audio, video playback, and a unified `Model3D` asset container.
- **2D game engine APIs** — Ten new high-level systems (Entity, Behavior, LevelData, SceneManager, etc.) with the asset-embedding VPA pipeline.
- **Cross-platform & packaging** — `PlatformCapabilities.hpp`, unified Unix build scripts, capability-gated CMake, the new `viper install-package` toolchain installer, and the documentation/release-notes site refresh.
- **Demos & docs** — XENOSCAPE Metroid-style sidescroller (17K LOC), 3D bowling (3.1K LOC), 3D world starter, baseball season simulator (plans 10–21), ViperSQL, ViperIDE, Chess, plus a comprehensive 186-file documentation audit and a full Doxygen sweep across the runtime (~12,400 `/// @brief` tags across `src/runtime/`).

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|---|---|---|---|
| Commits | — | 119 | +119 |
| Source files | 2,671 | 2,869 | +198 |
| Production SLOC | ~348K | ~450K | +102K |
| Test SLOC | ~165K | ~183K | +18K |
| Test count | 1,351 | 1,454 | +103 |
| Demo SLOC | — | ~177K | — |

Counts produced by `scripts/count_sloc.sh` (`Production SLOC` = `src/` minus `src/tests/`).

---

### Table of Contents

1. [GPU Backends](#gpu-backends)
2. [Native Toolchain](#native-toolchain)
3. [VM & Runtime](#vm--runtime)
4. [Compiler & Codegen](#compiler--codegen)
5. [3D Engine](#3d-engine)
6. [2D Game Engine](#2d-game-engine)
7. [Asset Embedding (VPA)](#asset-embedding-vpa)
8. [GUI](#gui)
9. [Graphics Foundation (vgfx)](#graphics-foundation-vgfx)
10. [Media Codecs](#media-codecs)
11. [Networking](#networking)
12. [I/O Runtime](#io-runtime)
13. [Cross-Platform & Packaging](#cross-platform--packaging)
14. [Tests](#tests)
15. [Bug Fixes](#bug-fixes)

---

## GPU Backends

### Metal — Feature-Complete (14 plans)

All 14 implementation plans shipped, taking Metal from 47% to 94% feature parity with the software renderer.

| Plan | Feature |
|---|---|
| MTL-01 | Lit textures (sample `baseColor` before lit/unlit branch) |
| MTL-02 | Spot lights (smoothstep cone via `inner_cos`/`outer_cos`) |
| MTL-03 | Per-frame texture cache keyed by Pixels pointer |
| MTL-04 | Normal mapping (TBN with Gram-Schmidt orthonormalization) |
| MTL-05 | Specular maps (texture slot 2) |
| MTL-06 | Emissive maps (additive, slot 3) |
| MTL-07 | Linear distance fog |
| MTL-08 | Wireframe (`MTLTriangleFillModeLines`) |
| MTL-09 | GPU skinning (4-bone weighted, palette buffer 3) |
| MTL-10 | Morph targets (vertex shader delta accumulation, 4 KB inline limit) |
| MTL-11 | Post-FX (bloom, FXAA, Reinhard/ACES tonemap, vignette, color grade) |
| MTL-12 | Shadow mapping (`Depth32Float` + comparison sampler + PCF) |
| MTL-13 | Instanced rendering (`submit_draw_instanced` vtable hook) |
| MTL-14 | Terrain splatting (4-layer weight blend) |

**Architecture extensions:**
- Direct vs GPU-postfx window paths split. Direct path draws straight into the `CAMetalLayer` drawable; postfx path renders the scene into an HDR `RGBA16F` target, records overlays into a separate UNORM color/motion/depth triplet, then composites at `Flip`.
- `VGFXMetalRenderTargetCacheEntry` caches offscreen textures + in-flight command buffers per `vgfx3d_rendertarget_t` with lazy CPU sync — eliminates the per-frame `getBytes` stall at `End`.
- `VGFXMetalMorphCacheEntry` caches packed delta / normal-delta buffers keyed by `(morph_key, morph_revision, shape_count, vertex_count)` with age-based pruning.
- Skybox rewritten as a fullscreen triangle generated from `vertex_id`. The fragment shader reconstructs the world-space view direction via `inverseProjection * clip` → normalize → `inverseViewRotation`. `mtl_skybox_params_t` now carries inverse matrices; the cube vertex buffer is gone.
- macOS 26 (Tahoe) compatibility: offscreen `MTLStorageModeManaged` texture readback replaces direct `CAMetalLayer` presentation.

### D3D11 — 20 Features Implemented

3,173-line HLSL+C rewrite covering all 20 D3D11 plans.

| Plan | Feature |
|---|---|
| D3D-01 | Diffuse texture + SRV/PS slot binding |
| D3D-02 | Normal matrix in per-object cbuffer |
| D3D-03 | Per-frame `ID3D11ShaderResourceView` cache |
| D3D-04 | Spot lights (HLSL cone attenuation) |
| D3D-05 | Wireframe + per-material cull |
| D3D-06 | Linear distance fog |
| D3D-07 | Normal mapping (TBN + Gram-Schmidt) |
| D3D-08 | Specular + emissive maps |
| D3D-09 | Render-to-texture (`ID3D11RenderTargetView` + resolve) |
| D3D-10 | GPU skinning + morph targets |
| D3D-11 | Post-FX (bloom, FXAA, tonemap, vignette, color grade) |
| D3D-12 | Shared VBO/IBO pool with sub-allocation |
| D3D-13 | Systematic `FAILED(hr)` validation |
| D3D-14 | Shadow mapping (`COMPARISON_LESS_EQUAL` + PCF) |
| D3D-15 | Instanced rendering (per-instance structured buffer) |
| D3D-16 | Terrain splatting (4-layer weight blend) |
| D3D-17 | `TextureCube` skybox (depth write disabled) |
| D3D-18 | Environment reflections (cubemap + fresnel) |
| D3D-19 | Morph normal deltas |
| D3D-20 | Advanced post-FX (DOF, motion blur, SSAO, chromatic aberration) |

A Windows-only `windows-d3d11` CI job validates the build and tests in isolation.

### Shared Backend Infrastructure

Per-backend "shared" `.c/.h` pairs factor out constant-buffer layouts, packed morph weight encoding, blend mode / target kind enums, and frame-history tracking:

- `vgfx3d_backend_metal_shared.{c,h}` — 190 + 98 LOC
- `vgfx3d_backend_d3d11_shared.{c,h}` — 180 + 117 LOC
- `vgfx3d_backend_opengl_shared.{c,h}` — 126 + 77 LOC
- `vgfx3d_backend_utils.{c,h}` — RGBA unpack, cubemap face decode, mip-count math, normal-matrix derivation, 4×4 inverse

The main backend files (`vgfx3d_backend_metal.m`, `_d3d11.c`, `_opengl.c`) were rewritten against these shared headers (~900–1400 lines changed each). New unit tests (`test_vgfx3d_backend_{metal,d3d11,opengl}_shared.{c,m}`) exercise the helpers without a live GPU context.

### Cross-Backend Hardening

- **Generation-aware texture caching** — `Pixels` and `CubeMap3D` carry a `uint64_t generation` counter. Every in-place mutation bumps it via `pixels_touch()`. GPU caches compare cached vs current generation to decide whether to reuse the cached texture or re-upload. Replaces per-frame cache invalidation; fixes the "modified texture not updating on screen" bug class on all backends.
- **Morph target generation tracking** — `rt_morphtarget3d_get_payload_generation()` + `morph_revision` field on draw commands; cached morph buffers stay valid across frames.
- **Canvas3D PostFX state latching** — `canvas3d_latch_gpu_postfx_state` / `apply_gpu_postfx_state` snapshot post-FX configuration at frame boundaries so mid-frame edits don't desync.
- **Window resize handling** — `vgfx_set_resize_callback` propagates OS resize events to Canvas3D; `Poll()` resize events also trigger `backend->resize()`.
- **GPU screenshot readback** — `rt_canvas3d_screenshot()` supports three paths: render-target color-buffer copy, GPU `backend->readback_rgba()`, software framebuffer fallback.
- **InstanceBatch3D memory safety** — `realloc` replaced with `calloc` + `memcpy` + `free`; swap-remove copies all three parallel arrays (`transforms`, `current_snapshot`, `prev_transforms`).
- **Mesh3D normal transform** — `rt_mesh3d_transform()` now uses inverse-transpose of the upper 3×3, fixing lighting on non-uniformly scaled meshes.
- **Pixels mutation tracking** — Every `Set`/`Fill`/`Clear`/`Copy`/`DrawBox`/`DrawDisc`/`FloodFill`/`BlendPixel` bumps the generation counter.

---

## Native Toolchain

### PE/COFF Linker

Full Windows native linking pipeline. `viper build` now goes from Zia source to `.exe` with zero external tools on all three platforms.

| Component | Description |
|---|---|
| `ArchiveReader` | Reads `.lib` archives, extracts object files and symbol tables |
| `SymbolResolver` | Resolves symbols across object files and archives with on-demand archive member inclusion |
| `SectionMerger` | Merges `.text`/`.rdata`/`.data`/`.bss` with alignment padding |
| `DeadStripPass` | Removes sections unreachable from entry-point roots |
| `ICF` | Identical Code Folding — deduplicates sections with matching content/relocations |
| `RelocApplier` | Applies `IMAGE_REL_AMD64_*` relocations (ADDR64, ADDR32NB, REL32, SECTION, SECREL) |
| `PeExeWriter` | PE32+ executables with `.idata` import tables, section alignment, optional headers |

### ELF Dynamic Linking

`ElfExeWriter` rewritten from static-only to full dynamic-link support: `PT_INTERP`, `PT_DYNAMIC`, `.dynsym`, `.dynstr`, `.hash` (SYSV), `.rela.dyn` with `R_X86_64_GLOB_DAT`. Linux x86_64 programs natively link against libc/libm/libpthread/libX11/libasound without system-linker fallback. `planLinuxImports()` classifies dynamic symbols into shared libraries via prefix/exact-match tables; `generateDynStubsX8664()` emits 6-byte `jmpq *__got_sym(%rip)` stubs.

### Linker Hardening

- **BranchTrampoline** — Boundary-based AArch64 trampoline placement. Queries `.text` chunk boundaries via `collectChunkBoundaries()`, picks the nearest reachable boundary via `chooseReachableBoundary()`, validates ±128 MB displacement at the instruction level.
- **`SectionMerger::assignSectionVirtualAddresses()`** — Public API; eliminates the duplicate VA-assignment algorithm that BranchTrampoline previously maintained.
- **Platform-aware symbol resolution** — `SymbolResolver::resolveSymbols()` takes a `LinkPlatform` parameter. Dynamic symbol prefix tables split per platform (macOS: `CF`/`CG`/`NS`/`objc_`/…; Windows: `__imp_`; common: `__libc_`/`__stack_chk_`).
- **`writeCheckedRel32()`** — Validates PC-relative relocations fit in signed 32 bits before patching; out-of-range produces a diagnostic instead of silent truncation.
- **Multi-section COFF writer** — `CoffWriter::write(path, vector<CodeSection>, rodata)` overload produces COFF objects with per-function `.text.funcName` sections + cross-section `.xdata`/`.pdata` references.
- **Mach-O ObjC section flags** — `S_CSTRING_LITERALS`, `S_LITERAL_POINTERS`, `S_ATTR_NO_DEAD_STRIP`, `S_COALESCED` for the appropriate metadata sections; section alignment as log2 power-of-two.
- **Mach-O symbol-name fallback** — `findWithMachoFallback` searches plain, underscore-stripped, and underscore-prefixed names. `normalizeMacFrameworkSymbol` strips `OBJC_CLASS_$_` / `OBJC_METACLASS_$_` / `OBJC_EHTYPE_$_` prefixes for framework-rule matching.
- **Mach-O symbol-index sentinel fix** — `MachOWriter` tracks "have I assigned a symbol index?" via an explicit `haveSymIdx` flag; symbols legitimately resolved to slot 0 are no longer treated as "not found."
- **Windows ARM64 import stubs** — `generateWindowsImports()` emits AArch64 `ADRP`/`LDR`/`BR` sequences with `kPageRel21`/`kPageOff12L` relocations alongside x86_64 `JMP [rip+disp32]` stubs.
- **Windows ARM64 native link gating** — Returns an early diagnostic for `LinkPlatform::Windows` + `LinkArch::AArch64`; COFF object emission is supported but PE startup/import/unwind generation remains x86_64-specific.
- **Table-driven import plans** — macOS and Windows planners restructured around `MacImportRule` / `dllForImport` tables. `DynamicSymbolPolicy.hpp` extracted from `SymbolResolver`.
- **Proper zerofill** — `OutputSection::zeroFill` propagates through merging, dead-strip, and ICF. Mach-O emits `S_ZEROFILL` / `S_THREAD_LOCAL_ZEROFILL`; ELF emits `SHT_NOBITS` with zero file size.
- **Dead-strip all objects** — Liveness analysis applies to every input object, not just archive extracts. Only entry points, TLS, ObjC metadata, and runtime roots are unconditional.
- **String dedup compaction** — Cstring sections covered entirely by symbolized strings are physically compacted post-dedup.
- **ICF cross-object resolution** — Address-taken detection in `foldIdenticalCode()` resolves through the global symbol table.

### Native Exception Handling Lowering

New `NativeEHLowering` pass (683 LOC) rewrites structured IL EH markers (`EhPush`/`EhPop`/`ResumeSame`/`ResumeNext`/`TrapErr`) into ordinary calls and branches before backend lowering. Both x86_64 and AArch64 `CodegenPipeline` invoke the pass before MIR lowering. 171-line unit test validates all opcode transformations.

---

## VM & Runtime

### Worker VM Execution Environment

Threading, async, and HTTP-callback dispatch all need to spawn child VMs whose lifetime can outlive the parent VM that scheduled them. Worker-relevant state is now a first-class snapshot.

```cpp
struct ExecutionEnvironment {
    bool runtimeBridgeEnabled = false;
    bool useThreadedDispatch = true;
    std::unordered_map<std::string, NativeHandler> nativeHandlers;
};

[[nodiscard]] ExecutionEnvironment captureExecutionEnvironment() const;
void applyExecutionEnvironment(const ExecutionEnvironment &env);
void copyExecutionEnvironmentFrom(const BytecodeVM &other);
```

`Thread.Start`, `Async.Run`, and the HTTP-server request dispatcher all call `copyExecutionEnvironmentFrom(parent)` on the worker before it runs, so the worker owns its own copy of every native handler and runtime-bridge toggle.

### Per-VM Extern Registry

`RuntimeBridge`'s extern registry is now a real type instances can own:

- **Resolution order:** per-VM registry > process-global registry > built-in `RuntimeDescriptor` table.
- Per-registry mutex for both registration and lookup.
- `PerVMExternRegistryTests` covers per-VM-only registrations, override of process-global names, and isolation between sibling VMs.

### Resumable Trap State

`BytecodeVM::TrapRecord` snapshots full execution state at trap time — kind, error code, fault PC, next PC, fault line, value/call/EH stacks, and live `alloca` storage. A trap that escapes a `try/catch` in one frame can be re-raised cleanly in the caller without unwinding into VM-corruption territory.

### Per-VM Interrupt Epoch

`VM::requestInterrupt` previously stored a `std::atomic<bool>` flag; the first VM to observe Ctrl-C cleared it for everyone. The flag is now a monotonic `std::atomic<uint64_t>` epoch and each `VM` tracks its own observed cursor:

```cpp
bool VM::consumePendingInterrupt() noexcept {
    const uint64_t cleared = s_interruptClearedEpoch.load(std::memory_order_acquire);
    if (lastObservedInterruptEpoch_ < cleared)
        lastObservedInterruptEpoch_ = cleared;
    const uint64_t epoch = s_interruptEpoch.load(std::memory_order_acquire);
    if (epoch == 0 || epoch <= cleared || epoch == lastObservedInterruptEpoch_)
        return false;
    lastObservedInterruptEpoch_ = epoch;
    return true;
}
```

Multiple coexisting VMs (REPL + background thread, embedding host + child eval) each observe Ctrl-C independently. `clearInterrupt` advances the cleared-epoch watermark instead of zeroing a shared boolean.

### Object Lifetime Hardening

- **Zeroing weak references** — `rt_weak_load`/`rt_weak_store` route runtime-managed heap objects through `rt_weakref_*` zeroing handles. Raw non-runtime pointers are stored as-is for compatibility, distinguished via `rt_weakref_is_handle`.
- **`Object.ToString` defensive path** — Returns a fresh `rt_string` reference when the target is a string, validates the heap header before dereferencing the vptr (returns `"Object"` on unmanaged or freed targets).
- **`MessageBus.Publish` snapshot semantics** — Subscribers can unsubscribe themselves from inside their callback without disturbing the in-progress publish. The bus is now GC-managed.

### Bytecode VM CALL_NATIVE Expansion

Instruction encoding widened to support the growing runtime:

- **Before:** `[opcode:8][nativeIdx:8][argCount:8]` — max 255 native functions.
- **After:** `[opcode:8][argCount:8][nativeIdx:16]` — max 65,535 native functions.

Bytecode format version bumped 1 → 2. `BytecodeCompiler` validates argument count ≤ 255 and native index ≤ 65,535 with clear error messages.

### Trap & Error Plumbing

- `rt_trap_set_ip()` / `rt_trap_get_ip()` store and retrieve the native instruction pointer associated with the most recent trap. `TrapGetIp` added to `runtime.def`.
- `rt_err_to_trap_kind()` maps `RtError` codes to trap kinds (overflow=1, invalid cast=2, domain=3, bounds=4, file-not-found=5, …).
- `rt_trap_error_make()` and `rt_trap_raise_error()` combine message storage, field classification, and trap invocation.

### Runtime Stub Audit & Bug Fixes

| Issue | Severity | Fix |
|---|---|---|
| `rt_exc_is_exception()` returned true for any non-null pointer | P0 | Proper `rt_obj_class_id() == RT_EXCEPTION_CLASS_ID` check |
| Destructor chaining missing (derived dtor never called base) | P0 | `emitClassDestructor` emits `call @Base.__dtor` |
| OOP refcount imbalance (NEW objects never reached refcount 0) | P0 | Release NEW temporary after assignment in `lowerLet` |
| TLS RSA-PSS only supported SHA-256 | P1 | Added SHA-384/SHA-512 via CommonCrypto (macOS) and dlopen'd EVP_Digest (Linux) |
| Bytecode VM missing `LOAD_I32`, `LOAD_STR_MEM`, `STORE_STR_MEM` | P1 | Added threaded-dispatch labels |
| POSIX process isolation had no timeout | P1 | WNOHANG poll loop with `clock_gettime` + `SIGKILL` on timeout |
| Windows `Viper.Threads` test disabled but runtime implemented | P1 | Removed `#ifdef _WIN32 return 0` guard |

### Input Subsystem Refinements

- **Caps Lock platform query** — `Keyboard.CapsLock` queries real OS state every call: `GetKeyState(VK_CAPITAL)` (Windows), `CGEventSourceFlagsState` (macOS), `XkbGetIndicatorState` (Linux). Replaces the previous toggle-counting scheme that could desync from the LED. Test hook (`rt_input_set_caps_lock_query_hook`) for deterministic tests.
- **Mouse cursor warp** — `Mouse.SetPos(x, y)` calls `vgfx_warp_cursor` which dispatches to `CGWarpMouseCursorPosition` / `SetCursorPos` / `XWarpPointer`.
- **Double-precision wheel deltas** — `Mouse.WheelXF`/`WheelYF` accessors return raw `double`. Legacy integer `Wheel.X`/`Wheel.Y` round to the nearest tick.
- **Canvas window destruction** detaches input bindings via `rt_keyboard_clear_canvas_if_matches` / `rt_mouse_clear_canvas_if_matches`, eliminating stale dangling-window pointers.

---

## Compiler & Codegen

### Zia Language Features

Seven new features:

- **Type aliases** — `type Name = TargetType;` resolved during semantic analysis.
- **Variadic parameters** — `func sum(nums: ...Integer)` collected as `List[Integer]`. Only the last parameter may be variadic.
- **Shift operators** — `<<`, `>>` (arithmetic right shift) with correct precedence between additive and comparison.
- **Compound bitwise assignments** — `<<=`, `>>=`, `&=`, `|=`, `^=` desugared via read-op-store.
- **Single-expression functions** — `func f(x: T) -> R = expr;`.
- **Lambda expressions** — `(x: T) => expr`; zero-arg lambdas use `() => expr`.
- **Polymorphic `is`** — `obj is Base` matches any subclass via `collectDescendants()` walk.

`entity` → `class`, `value` → `struct` rename across all source, tests, REPL, LSP, runtime GUI, docs, VS Code extension, and website.

### Zia Frontend

- **Runtime extern parameter types** — `rtgen` emits full ABI-shaped signatures, not just return types. String-returning runtime methods (e.g., `LevelData.ObjectType()`) now compare correctly via `Viper.String.Equals` instead of pointer-`ICmpEq`.
- **Typed return metadata** — `runtime.def` annotated with concrete return classes (`obj<ClassName>`, `seq<str>`) for 100+ factory/conversion/collection methods. New `concreteRuntimeReturnClassQName` API + BASIC `inferObjectClassQName` recursive type tracer.
- **Runtime property setters** — `ctrl.VY = value` calls the setter symbol instead of writing raw memory.
- **`final` enforcement** — Sema rejects reassignment of `final` variables, for-in loop variables, and match pattern bindings (`error[V3000]: Cannot reassign final variable 'x'`). Lowerer safety net prevents SSA corruption if enforcement is bypassed.
- **`catch(e)` binding** — New `rt_throw_msg_set`/`rt_throw_msg_get` TLS runtime functions and `ErrGetMsg` IL opcode propagate the throw message to handlers.
- **`String.Contains()`** — Method alias mapping to `StrHas`.
- **`main()` entry point parity** — `main()` now receives interface itable initialization (`__zia_iface_init`) and global initializer emission, just like `start()`.
- **Optional struct boxing** — `emitOptionalWrap` boxes struct payloads to the heap (via `emitBoxValue`) instead of returning a raw stack pointer.
- **Completion runtime APIs** — `Viper.Zia.Completion.Check(source)` / `.Hover(source, line, col)` / `.Symbols(source)` expose the analysis pipeline to Zia programs via `parseAndAnalyze()` (error-tolerant partial compilation).

### BASIC Frontend

- **Constant folding for builtins** — `FoldBuiltins.cpp` evaluates `ABS`, `INT`, `SGN`, `SQR`, `LOG`, `EXP`, `SIN`, `COS`, `TAN`, `ATN`, `ASC`, `CHR$`, `LEN`, `LEFT$`, `RIGHT$`, `MID$`, `STR$`, `VAL`, `STRING$`, `SPACE$`, `LCASE$`, `UCASE$`, `LTRIM$`/`RTRIM$`/`TRIM$` at compile time when arguments are constant.

### x86_64 Backend

- **CoffWriter cross-section symbols** — Rodata symbols (`.LC_str_*`) processed first; text relocations redirect to defined entries (fixes LNK2001).
- **X64BinaryEncoder runtime mapping** — `mapRuntimeSymbol()` translates IL names (`Viper.Terminal.PrintStr`) to C runtime names (`rt_print_str`).
- **Operand materialisation** — Immediate operands for `TESTrr` (select/cond_br) and `call.indirect` callees materialised into registers (no more `bad_variant_access`).
- **SETcc REX prefix** — Correct emission for SPL/BPL/SIL/DIL.
- **SSE RIP-relative loads** — `MOVSD` encoding for xmm RIP-relative labels.
- **Spill slot reuse disabled** — Cross-block liveness wasn't accounted for; all three reuse sites now use the safe `ensureSpillSlot` path.
- **Branch relaxation** — Short JMP (`EB`, 2 bytes) and short Jcc (`75`/`74`/…, 2 bytes) replace always-near forms.
- **Pipeline decomposition** — `legalizeModuleToMIR` / `allocateModuleMIR` / `optimizeModuleMIR` / `emitMIRToAssembly` / `emitMIRToBinary` public APIs. Each phase invokable independently.
- **Call ABI refactor** — `CallArgLayout` extracted as shared utility for SysV x86-64; `FrameLayoutUtils` for common frame patterns.
- **System linker shell-out removed** — ~330 lines of `cc`/`ld` invocation gone. Native linker is the default; `--system-link` deprecated.

### AArch64 Backend

- **Register allocator protected-use eviction** — `protectedUseGPR_`/`protectedUseFPR_` sets prevent evicting source operands of the currently-allocating instruction. Fixes the GEP base-register clobber that broke `BowlingGame.init` in 3D bowling.
- **OperandRoles fix** — `isUseDefImmLike` opcodes (AddRI, SubRI, …) classify operand 0 as DEF-only instead of USE+DEF.
- **FPR load/store classification** — `isMemLd`/`isMemSt` include `LdrFprFpImm`, `LdrFprBaseImm`, `StrFprFpImm`, `StrFprBaseImm`, `StrFprSpImm`.
- **Clean FPR spill slot reuse** — Values loaded from rodata that survive a call in caller-saved FPRs get spill slots even when not marked dirty.
- **Live-out spill fix** — `isLiveOut()` query prevents premature register release for vregs live across block boundaries. End-of-block spill scan corrected from reverse to forward.
- **Trap message forwarding** — `TrapErr` materialises the message string into x0 and passes it to `rt_trap()`, so catch handlers see the user's throw message in native executables.
- **Error field extraction via TLS** — `ErrGetKind`/`ErrGetCode`/`ErrGetLine` call `rt_trap_get_kind/code/line` instead of returning hardcoded 0. Enables typed `catch(e: DivideByZero)` in native code.
- **Apple M-series scheduler tuning** — FP divide 3→10 cycles, integer divide 3→7, FP multiply 3→4 — matches Firestorm latencies.
- **Join-phi coalescing peephole** (Pass 4.88) — Eliminates stack round-trips at CFG join blocks by proving all predecessor edges store the same physical registers to the same frame offsets, then replacing the successor's load prefix with topologically-ordered register moves. Cycles bail out conservatively.
- **CBR edge block generalization** — Edge blocks (`Ledge_true_N`/`Ledge_false_N`) emit whenever branch arguments are present, not only when both arms target the same block (fixes miscompilation in branch ladders and multi-level if/else with phi values).
- **Loop phi-spill multi-block fix** — `eliminateLoopPhiSpills` handles multi-block loops where the latch (not the split body) carries phi-slot stores.
- **Pipeline decomposition** — `PassManager`-based composition. Scheduler and BlockLayout passes added at O1+. EH-sensitive modules bypass IL optimizations. Virtual register space partitioned into general (`kFirstVirtualRegId=1`), phi-inserted (`kPhiVRegStart=40000`), and cross-block spill keys (`kCrossBlockSpillKeyStart=50000`).
- **Branch relaxation** — `computeFunctionLabelOffsets` + `measureInstructionSize` + `estimateFunctionSize`. New `A64CondBr19` relocation kind for conditional branches that exceed ±1 MB.
- **`FrameBuilder` spill epoch guards** — Cross-block reuse guarded by `(lifetime epoch == current epoch || lifetime still live)` to prevent stale-offset reuse.

### IL Optimizer Correctness

- **EarlyCSE / GVN textual ordering** — Replacements track defining block via `AvailableExpr{value, block}`. `isTextuallyAvailable()` prevents replacements that would create textual use-before-def.
- **Mem2Reg single-predecessor fix** — `renameUses` no longer inserts unbindable synthetic block parameters in entry blocks (no preds → typed zero constant) or single-predecessor blocks (forward from predecessor directly). Fixes verifier failures under heavy inlining.
- **SCCP preserves analyses** — Declares dominators, loop info, and CFG as preserved instead of conservatively invalidating everything. `test_il_pass_manager` validates.

---

## 3D Engine

### Terrain System

- **Procedural generation** — `Terrain3D.GeneratePerlin(noise, scale, octaves, persistence)` writes the heightmap directly from a PerlinNoise object.
- **LOD** — Three resolution levels per chunk (step 1/2/4) selected by camera distance. `SetLODDistances(near, far)`. Chunks: 578/162/50 vertices at LOD 0/1/2.
- **Frustum culling** — Per-chunk AABBs + Gribb-Hartmann plane extraction.
- **Skirts** — Downward triangles along chunk edges hide T-junction cracks at LOD seams.
- **16-bit heightmap** — R+G channels for 65,536 height levels.
- **4-layer splat blending** — `SetSplatMap` + per-layer textures with UV tiling.

### Water System

- **Gerstner waves** — `Water3D.AddWave(dirX, dirZ, speed, amplitude, wavelength)` (up to 8 waves). Multi-wave sum produces realistic ocean displacement with proper derivative-based normals.
- **Material wiring** — `SetTexture`, `SetNormalMap`, `SetEnvMap`, `SetReflectivity` forward to the underlying Material3D.
- **Configurable resolution** — `SetResolution(n)` (8–256, default 64).

### Vegetation (New)

- **Cross-billboard blades** — Two perpendicular quads per blade (no popping from any angle).
- **Terrain population** — `Populate(terrain, count)` scatters blades, optionally weighted by a density map.
- **Wind animation** — Per-blade Y-shear via `sin(position + time)`. `SetWindParams(speed, strength, turbulence)`.
- **Distance LOD** — Progressive thinning between near/far thresholds; hard cull beyond far.
- **GPU instancing** — `submit_draw_instanced` vtable hook for single-draw-call rendering.

### Asset Loaders

- **glTF 2.0** — `.gltf` (JSON + external buffers) and `.glb` (binary container). Mesh + tangents, PBR metallic-roughness, skeletal animation with joint hierarchy and inverse bind matrices, morph targets.
- **STL** — Binary and ASCII auto-detection.
- **OBJ .mtl** — Material parser with Kd/Ks/Ns/d, texture path resolution, up to 64 materials.
- **FBX** — Texture path extraction via Texture-node parsing and connection tracing. Morph target extraction from BlendShape/Shape nodes.
- **`Scene3D.Save`** — JSON serialization of node hierarchy.

### Model3D & AnimController3D

- **Model3D** (`rt_model3d.c`, 513 LOC) — Unified asset container. `Load(path)` routes by extension (.vscn, .fbx, .gltf, .glb) and builds an internal resource collection. `Instantiate()` clones the node tree with shared resources. `InstantiateScene()` creates a fresh `Scene3D`. `MeshCount`/`MaterialCount`/`AnimationCount` accessors.
- **AnimController3D** (`rt_animcontroller3d.c`, 871 LOC) — High-level state controller with named states, crossfade transitions, play/pause/stop, speed control, loop modes (once/loop/pingpong), event frame callbacks. `SceneNode3D.BindAnimator` + `Scene3D.SyncBindings(dt)` for automatic skeletal pose updates.

### Physics3D

- **Quaternion orientation** — `Body3D.Orientation` (get/set), `AngularVelocity` (get/set), `ApplyTorque`, `ApplyAngularImpulse`, `LinearDamping`/`AngularDamping`.
- **Body modes** — Dynamic / static / kinematic. Kinematic bodies collide but aren't affected by forces.
- **Sleep system** — Bodies tracking sub-threshold velocity for `PH3D_SLEEP_DELAY` seconds enter sleep. `CanSleep`, `Sleeping`, `Wake()`, `Sleep()`.
- **Continuous collision detection** — `UseCCD` enables substep sweeps (`PH3D_MAX_CCD_SUBSTEPS = 16`) for fast-moving bodies.
- **Mass-only constructor** — `Body3D.New(mass)` creates a collider-ready body without legacy shape parameters.
- **World queries** — `Raycast`, `RaycastAll`, `SweepSphere`, `SweepCapsule`, `OverlapSphere`, `OverlapAABB`. `PhysicsHit3D` and `PhysicsHitList3D` accessors.
- **Collision events** — `CollisionCount`, `GetCollisionBodyA/B`, `GetCollisionNormal/Depth`, `CollisionEvent3D` with contact manifold (`ContactPoint3D` array).
- **Joints** — `DistanceJoint3D` and `SpringJoint3D` with 6-iteration sequential impulse solver.

### Collider3D

New runtime class (`rt_collider3d.c/h`, 822 LOC) with 7 reusable shape types: box, sphere, capsule, convex hull (from Mesh3D), triangle mesh, heightfield (from Terrain3D), compound (parent + child transforms). Shapes decouple geometry from body instances — `Body3D.SetCollider` attaches a shape, AABB and narrow-phase dispatch use the attached collider.

### NavAgent3D

Autonomous pathfinding agent on NavMesh3D surfaces (`rt_navagent3d.c`, 548 LOC). A* path query, string-pulling corridor smoothing, configurable speed/acceleration/stopping distance. `SetDestination(x,y,z)` triggers path computation; `Update(dt)` advances along the smoothed corridor. `BindNode(node)` for automatic `SceneNode3D` sync.

### 3D Audio

- **AudioListener3D** — Position, forward, velocity. `BindNode`/`BindCamera` for automatic spatial tracking.
- **AudioSource3D** — Positional emitter with inner/outer cone attenuation, min/max distance, inverse-distance rolloff, looping, pitch, gain.
- **Audio3D.SyncBindings(dt)** — Batch-updates all bound positions from scene nodes.

### PBR Materials

`Material3D.NewPBR(metallic, roughness, ao)` with metallic-roughness workflow. Albedo, metallic-roughness, AO, normal, emissive texture map slots. `Clone`/`MakeInstance` for shared-base + per-instance overrides. `AlphaMode` (opaque/mask/blend), `DoubleSided`, `NormalScale`. Cook-Torrance BRDF with GGX distribution and Schlick fresnel implemented across all 4 GPU backends.

### Material Shader Hooks

- **Shading models** — `Material3D.SetShadingModel(model)`: 0=BlinnPhong, 1=Toon, 4=Fresnel, 5=Emissive.
- **Custom parameters** — `Material3D.SetCustomParam(index, value)` passes 8 floats per material to the shader.
- Implemented in Metal MSL and software rasterizer; OpenGL/D3D11 receive the uniforms (shader-side switch deferred).

### Other 3D Improvements

- **Light3D.NewSpot** — Position, direction, inner/outer cone angles, smoothstep attenuation.
- **Camera3D.NewOrtho** — Orthographic camera for isometric/strategy games.
- **Mesh3D.Clear()** — Reset vertex/index counts without freeing backing arrays.
- **Sprite3D use-after-free fix** — Per-frame allocation replaced with cached instances + GC temp buffer.
- **Cubemap bilinear filtering** — 4-texel interpolation replaces nearest neighbor.
- **VIPER_3D_BACKEND env var** — `VIPER_3D_BACKEND=software` forces the software renderer.
- **53 backend implementation plans** — Across all 4 renderers (SW: 7, Metal: 14, OpenGL: 16, D3D11: 16).

---

## 2D Game Engine

### 10 New Game Engine APIs

| API | Runtime Class | Key Functions | LOC |
|---|---|---|---|
| Entity | `Viper.Game.Entity` | `New`, `ApplyGravity`, `MoveAndCollide`, `UpdatePhysics`, `Overlaps`, `AtEdge`, `PatrolReverse` | 282 |
| Behavior | `Viper.Game.Behavior` | `AddPatrol`, `AddChase`, `AddGravity`, `AddEdgeReverse`, `AddShoot`, `AddSineFloat`, `Update` | 227 |
| Raycast2D | `Viper.Game.Raycast` | `HasLineOfSight`, `Collision.LineRect`, `Collision.LineCircle` | 124 |
| LevelData | `Viper.Game.LevelData` | `Load` (JSON), `ObjectCount`, `ObjectType`, `ObjectX/Y`, `PlayerStartX/Y`, `Theme` | 222 |
| SceneManager | `Viper.Game.SceneManager` | `Add`, `Switch`, `SwitchTransition`, `Update`, `Current`, `IsScene` | 183 |
| Camera.SmoothFollow | `Viper.Graphics.Camera` | `SmoothFollow(targetX, targetY, speed)`, `SetDeadzone(w, h)` | 45 |
| AnimStateMachine | `Viper.Game.AnimStateMachine` | `AddNamed`, `Play(name)`, `StateName`, `SetEventFrame`, `EventFired` | 76 |
| MenuList.HandleInput | `Viper.Game.UI.MenuList` | `HandleInput(up, down, confirm)` | — |
| Config.Load | `Viper.Game.Config` | `Load(path)`, `GetString`, `GetInt`, `GetBool`, `GetFloat`, `Has`, `Keys` | 114 |
| Tilemap.SetTileAnim | `Viper.Graphics.Tilemap` | `SetTileAnim`, `SetTileAnimFrame`, `UpdateAnims`, `ResolveAnimTile` | 76 |

### Other Game Runtime Additions

- **Timer ms-mode** — `StartMs(durationMs)`, `UpdateMs(dt)`, `ElapsedMs`, `RemainingMs` for delta-time-independent cooldowns.
- **Lighting2D** — Darkness overlay with pulsing player light and pooled dynamic point lights.
- **PlatformerController** — Jump buffering, coyote time, variable jump height, ground/air acceleration, apex gravity bonus.
- **AchievementTracker** — Up to 64 achievements (bitmask), 32 stat counters, animated slide-in popups.
- **Typewriter** — Character-by-character text reveal with configurable ms-per-character rate.
- **GameButton** — Styled button widget (`Viper.Game.UI.GameButton`, 132 LOC) with customizable colors, border, text.

Game engine classes moved from `src/runtime/collections/` to `src/runtime/game/` (36 files; no API changes).

---

## Asset Embedding (VPA)

Compile-time asset packaging for zero-file-dependency native executables. Assets are compiled into a binary blob at build time and embedded into the executable's `.rodata`.

**Project directives** in `viper.project`:

- `embed <path>` — embed file as raw bytes
- `pack <path>` — pack with VPA container framing
- `pack-compressed <path>` — pack with DEFLATE compression

**Toolchain components:**

- `AssetCompiler` (233 LOC) — reads project directives, resolves paths, invokes `VpaWriter`.
- `VpaWriter` (230 LOC) — produces VPA binary: magic header, file table (name + offset + size + flags), concatenated data, optional DEFLATE.
- `VpaReader` (`rt_vpa_reader.c`, 375 LOC) — reads VPA archives at runtime; supports mounting multiple archives.
- Asset blob injection — Both x86_64 and AArch64 `CodegenPipeline` inject VPA blobs as `viper_asset_blob` / `viper_asset_blob_size` globals in `.rodata`.
- `Path.ExeDir()` — Cross-platform executable directory resolution (`_NSGetExecutablePath` / `/proc/self/exe` / `GetModuleFileName`).

**Runtime API** (`Viper.IO.Assets`):

| Method | Signature | Description |
|---|---|---|
| `Load(name)` | `obj(str)` | Load asset as String |
| `LoadBytes(name)` | `obj(str)` | Load asset as Bytes |
| `Exists(name)` | `i64(str)` | Check if asset exists |
| `Size(name)` | `i64(str)` | Get asset size in bytes |
| `List()` | `obj()` | List all asset names |
| `Mount(path)` | `i64(str)` | Mount additional VPA archive |
| `Unmount(path)` | `i64(str)` | Unmount VPA archive |

Asset resolution order: embedded blob → mounted VPA archives → filesystem fallback relative to executable directory. 9 VPA format tests + 9 asset manager tests.

---

## GUI

### Theme & App State

- **Per-app theme ownership** — Each `rt_gui_app_t` owns a private scaled theme copy; the built-in `vg_theme_dark()`/`vg_theme_light()` singletons are no longer mutated.
- **Widget runtime state save/restore** — Focus, keyboard capture, and tooltip state save per-app and restore on activation.
- **Modal dialog stack** — Routes via the actual `dialog_stack` array with `rt_gui_sync_modal_root`, replacing the parallel event path.

### Platform Text Input

- **`VGFX_EVENT_TEXT_INPUT`** — Carries translated Unicode text from the OS input method (macOS `interpretKeyEvents:`/`insertText:`, Win32 `WM_CHAR`, X11 `XLookupString`).
- **`KEY_CHAR` delivery** — `vg_event_from_platform` converts to `VG_EVENT_KEY_CHAR`, replacing US-layout ASCII synthesis (broke on non-QWERTY keyboards and dead keys).

### Widget Tree, Hover, and Overlay Pass

- **Hover tracking** — `vg_widget_runtime_state_t` gains `hovered_widget`. Mouse-move dispatch emits `VG_EVENT_MOUSE_ENTER`/`LEAVE` on hover transitions across capture, modal, and normal hit-test paths.
- **Modal keyboard fallback** — When a modal is active and no widget has focus, keyboard events route to the modal root instead of being dropped.
- **`vg_widget_set_focus(NULL)` clears focus** — Releases the previously focused widget, clears `VG_STATE_FOCUSED`, calls `on_focus(false)`, marks for repaint.
- **Two-pass painting** — Normal tree walk followed by an overlay tree walk. Widgets that paint children internally (`ScrollView`, custom `paint_overlay`) act as render boundaries.
- **`FloatingPanel` reparented children** — Children added to the real widget tree (previously a private array). Hit testing, focus, and destruction follow standard widget machinery.
- **`Dropdown` overlay** — Open popup moved into `paint_overlay`; popup hit testing uses screen bounds via `vg_widget_get_screen_bounds`.
- **`ScrollView` clipping** — Children measured before content size is computed; rendering clips via `vgfx_set_clip`; `scroll_to_widget` walks the parent chain.

### Layout — Justify Content

- **`VBox`/`HBox`/`Flex`** distribute leftover main-axis space according to `justify` mode: `START`, `CENTER`, `END`, `SPACE_BETWEEN`, `SPACE_AROUND`, `SPACE_EVENLY`. Shared `compute_justify_distribution()` helper.
- **`vg_container_set_spacing`** — Polymorphic setter dispatches to the right container kind.

### Native macOS Menu Bar

554-line Objective-C bridge (`rt_gui_macos_menu.m`) mirrors Viper GUI menubars to the native macOS application menu bar:

- Special item relocation — About → app menu, Preferences → app menu (Cmd+,), Quit → app menu (Cmd+Q).
- Standard app menu items — auto-generated About, Services submenu, Hide/Hide Others/Show All, Quit.
- Keyboard accelerator translation — Viper `Ctrl+` → macOS `Cmd+`; full function/arrow/modifier mapping.
- Menu bar suppression — when `native_main_menu` is active, the Viper-rendered menubar collapses to zero height.
- Window/Help menu registration with `NSApp` for standard macOS behavior.

### Other GUI Hardening

- Dropdown placeholder strings copied (no use-after-free on freed temporaries).
- Dismissed notifications compacted immediately.
- Command palette UTF-8 query path completed.
- MessageBox prompt/builder flows honor default/cancel button semantics.
- Font inheritance applied consistently at construction.
- `vg_codeeditor.c` substantially expanded.
- `SplitPane.New(parent, horizontal)` second arg now treated as `horizontal != 0` (1 → horizontal split, 0 → vertical).

---

## Graphics Foundation (vgfx)

### Two-Phase Event/Present Model

- **`vgfx_pump_events(window)`** — Drains the native event queue without flipping the framebuffer. `rt_canvas_poll()` calls it first so input is available before any rendering happens that frame.
- **Event payload structs** documented through `vgfx.h` — `VGFX_EVENT_TEXT_INPUT` (`codepoint`), `VGFX_EVENT_SCROLL` (`delta_x`/`delta_y`/`x`/`y`), `VGFX_EVENT_FOCUS_GAINED`/`VGFX_EVENT_FOCUS_LOST`. Stable shape across all backends.
- **Shared `vgfx_internal_resize_framebuffer()`** — Factored out of macOS; Linux, Win32, and the mock backend share one `posix_memalign`-and-clear path.

### Linux X11 Backend (~150 LOC)

- Scroll wheel (`Button4`/`5`/`6`/`7`) → `VGFX_EVENT_SCROLL`.
- `XLookupString` → `VGFX_EVENT_TEXT_INPUT` (UTF-8 decoded codepoints).
- `FocusIn`/`FocusOut` → focus events with `is_focused` sync.
- `ConfigureNotify` → shared resize helper.
- Key-state map maintained alongside the event stream.

### Windows Win32 Backend (~270 LOC)

- `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` → `VGFX_EVENT_SCROLL` with high-resolution wheel deltas.
- `WM_CHAR` → `VGFX_EVENT_TEXT_INPUT` with surrogate-pair handling (UTF-16 → codepoint).
- `WM_SETFOCUS` / `WM_KILLFOCUS` → focus events.
- `WM_SIZE` → shared resize helper.
- Raw-input keyboard state for `vgfx_key_down()`.

### macOS Backend

- Removed duplicate inline resize body (shared helper now owns it).
- Explicit `is_focused` updates in `windowDidBecomeKey:` / `windowDidResignKey:`.

### HiDPI Refinements

- **Dynamic backing scale factor** — Queried per frame instead of cached at window creation. Windows dragged between mixed-scale displays report the correct scale immediately.
- **`vgfx_get_monitor_size`** — Uses the window's actual current monitor when the backend can determine it; primary-monitor fallback only when no window is bound.
- **Resize event dual dimensions** — `VGFX_EVENT_RESIZE` carries both physical-pixel `width`/`height` and logical `logical_width`/`logical_height`.

### Mock Backend Test Helpers

- `vgfx_mock_inject_text_input(win, codepoint)`
- `vgfx_mock_inject_scroll(win, dx, dy, x, y)`
- `vgfx_mock_inject_focus(win, focused)`

---

## Media Codecs

### Image Formats

| Format | Capability | Details |
|---|---|---|
| JPEG | Load | Baseline DCT, 8-bit, YCbCr/grayscale, 4:4:4/4:2:0/4:2:2, EXIF orientation, restart markers |
| PNG | Load + Save | All 5 color types, 1/2/4/8/16-bit depths, PLTE/tRNS, Adam7 interlace, round-to-nearest 16→8 |
| GIF | Load | GIF87a/89a, LZW, multi-frame animation (≤64 frames), all 4 disposal methods, interlacing, per-frame delay |
| BMP | Load + Save | 24-bit uncompressed |

`Pixels.Load(path)` auto-detects format from magic bytes. `Sprite.FromFile(path)` extended to all 4 formats; animated GIFs load all frames with timing.

### Audio Formats

| Format | Sound FX | Music Stream | Details |
|---|---|---|---|
| WAV | Yes | Yes | 8/16/24/32-bit PCM + 32-bit IEEE float, any sample rate (resampled to 44100 Hz) |
| OGG Vorbis | Yes | Yes (streaming) | Vorbis I codec: codebook VQ, floor type 1, multi-pass residue (types 0/1/2), FFT-based IMDCT, stereo coupling |
| MP3 | Yes | Yes (streaming) | MPEG-1/2/2.5 Layer III, ID3v2 + ID3v1 tag handling, bit reservoir |
| VAF | Yes | Block-based | IMA ADPCM 4:1 |

Music streaming for OGG and MP3 uses the same triple-buffer architecture as WAV (~96 KB regardless of track length). Seamless looping for all formats.

### Audio Streaming Overhaul

- **Multi-stream OGG** — `ogg_reader_next_packet_ex()` returns per-packet `ogg_packet_info_t` (serial number, granule position, BOS/EOS flags). Vorbis stream selection identifies the first Vorbis BOS packet and filters subsequent reads — enables correct extraction from `.ogv` (Theora + Vorbis).
- **Unified seek/rewind** — `vaud_music_seek_output_frame()` handles seek/rewind/loop-restart for all formats.
- **`source_sample_rate` separation** — Tracks original file sample rate independently from mixer rate. Fixes duration reporting and resampling.
- **OGG duration from granule** — Last-page granule scan during load provides accurate `frame_count`.
- **Loop restart consolidation** — Mixer loop restart delegates to `vaud_music_seek_output_frame(0)`, eliminating 40+ lines of format-specific logic.

### Video Playback

- **VideoPlayer runtime class** — `Open`, `Play`, `Pause`, `Stop`, `Seek`, `Update`, `SetVolume`, `Width`, `Height`, `Duration`, `Position`, `IsPlaying`, `Frame`.
- **AVI/MJPEG decoder** — RIFF chunk tree walker, MJPEG DHT injection (420-byte standard JPEG Annex K tables before SOS), `rt_jpeg_decode_buffer` extracted from file-based loader.
- **Theora codec infrastructure** — Identification (0x80) / comment (0x81) / setup (0x82) header parsing, YCbCr 4:2:0 → RGBA conversion (BT.601, separate `rt_ycbcr.c`), OGG multi-stream demux. Reference Y/Cb/Cr plane buffers allocated; full DCT/motion compensation is a follow-up.
- **VideoWidget (GUI)** — `VideoWidget.New(parent, path)` creates an image widget, loads via VideoPlayer; `Update(dt)` advances and refreshes. GUI Image widget paint vtable added.

| Container | Video | Audio | Status |
|---|---|---|---|
| AVI (RIFF) | MJPEG | PCM WAV | Full decode |
| OGG | Theora | Vorbis | Infrastructure (headers + YCbCr + audio handoff) |

---

## Networking

- **HttpServer runtime class** — `Listen`, `Accept`, `Respond`, `Close`, `Method`, `Path`, `Header`, `Body`. Wired through bytecode VM and both Zia/BASIC frontends.
- **Per-VM extern + worker-VM execution environment** — HTTP callbacks dispatched to worker VMs inherit parent runtime-bridge toggles and native handler registrations safely (see [VM & Runtime](#vm--runtime)).

---

## I/O Runtime

- **SaveData** — Migrated `SaveEntry` from raw `char*` to `rt_string` keys/values. JSON parse error forwarding via `rt_json_stream_error()`. Versioned format with future migration support.
- **Glob pattern matching** — Character classes (`[a-z]`, `[0-9]`, `[!abc]`), case-insensitive matching on Windows, `**` recursive descent, correct path separator handling (`*` doesn't cross `/` or `\`).
- **File watcher** — Debounced event coalescing, single-file watch via parent-directory + leaf-name filter, Windows `OVERLAPPED.hEvent` leak fix.
- **TempFile** — Atomic `O_CREAT|O_EXCL` (POSIX) / `CREATE_NEW` (Windows) with collision retry; eliminates TOCTOU race.
- **Archive extraction** — Path traversal validation; rejects `../` directory escape entries (zip-slip).

---

## Cross-Platform & Packaging

### `PlatformCapabilities.hpp`

Shared C++ header in `src/common/` with `VIPER_HOST_*`/`VIPER_COMPILER_*`/`VIPER_CAN_*` capability macros. Replaces ad-hoc raw `_WIN32`/`__APPLE__`/`__linux__` checks in codegen, tools, and tests.

### CMake Capability Gate

`VIPER_GRAPHICS_MODE` and `VIPER_AUDIO_MODE` cache variables with AUTO/REQUIRE/OFF modes. AUTO uses the feature if deps are available, REQUIRE fails configure if deps are missing (with install instructions), OFF explicitly disables. Replaces silent `return()` in library `CMakeLists.txt` that could produce broken binaries.

### Generated `RuntimeComponentManifest.hpp`

Machine-checked archive name → component mapping generated from `runtime.def`, replacing hand-maintained string tables. Drift between `runtime.def` and linker discovery is now a build error.

### Platform Import Planners

`NativeLinker.cpp` monolith (~1100 lines removed) split into `PlatformImportPlanner.hpp`, `MacImportPlanner.cpp`, `LinuxImportPlanner.cpp`, `WindowsImportPlanner.cpp`. Each planner owns its platform's symbol → dylib/DLL classification.

### Unified Build Scripts

- `build_viper_unix.sh` replaces near-identical `build_viper_mac.sh` / `build_viper_linux.sh` with platform detection via `uname -s`. Old scripts retained as thin wrappers.
- Standardized env vars: `VIPER_BUILD_DIR`, `VIPER_BUILD_TYPE`, `VIPER_SKIP_INSTALL`.
- `scripts/lint_platform_policy.sh` flags raw `_WIN32`/`__APPLE__`/`__linux__` outside approved adapter files.
- `scripts/run_cross_platform_smoke.sh` detects host capabilities and runs the appropriate ctest label slice + example smoke probes.
- `PlatformSkip.h` + CTest `SKIP_RETURN_CODE 77` make test skips visible in CI output.

### Toolchain Packaging Foundation

- **`ToolchainInstallManifest`** (`src/tools/common/packaging/`) — Shared data model for packaging Viper itself from a staged `cmake --install` tree (distinct from `PackageConfig` which packages apps built WITH Viper).
- **`viper install-package`** CLI with `--target`, `--arch`, `--stage-dir`, `--build-dir`, `--verify-only`, `--no-verify`, `--keep-stage-dir`, `--stage-only`, `--metadata-file`.
- Platform builders extended with toolchain entry points: `buildWindowsToolchainInstaller`, `buildMacOSToolchainPkg`, `buildLinuxToolchainPackages`.
- `scripts/build_installer.sh` and `.cmd` wrappers stage via `cmake --install` then invoke the CLI.
- `InstallPackageTarballSmoke.cmake` and `InstalledViperConfigSmoke.cmake` CTest integration tests.
- Installer plans revised at `misc/plans/installer/` with prerequisite fixes, verification matrix, and Phase 7 signing/release scaffolding.

### Installed Runtime Library Discovery

- **`LinkerSupport.cpp` layered search** — `VIPER_LIB_PATH` env → exe-relative `../lib/` → platform standard paths (`/usr/lib/viper`, `/usr/local/viper/lib`, etc.) → build-tree fallback.
- **Companion library resolvers** — `vipergfx`, `viperaud`, `vipergui` threaded through both x86_64 and AArch64 `CodegenPipeline` so an installed Viper compiles native executables without a build tree.
- **`test_linker_support.cpp`** covers all four discovery tiers.
- **`test_runtime_surface_audit.cpp`** validates that every runtime-component archive the discovery code looks for is actually produced by the build.
- **Exported target set** in `ViperTargets.cmake` reconciled with the actual built runtime libraries; downstream `find_package(Viper CONFIG REQUIRED)` consumers see the complete runtime.

### Linux Native Build

- ELF writer extended with GOT/PLT-style dynamic relocation support.
- ELF / COFF / Mach-O writers hardened for cross-platform emission edge cases (alignment, section flags, symbol ordering).
- OpenGL 3D backend Linux init fixes, PBR uniform forwarding, shader compilation guards.
- Software 3D backend platform-gated overrides to prevent non-graphics builds from pulling GPU symbols.
- x86_64 LowerDiv IDIV encoding fix for Linux ABI (RDX clobber handling).
- Runtime ALSA, crypto/TLS, threading, and network test portability fixes.

---

## Tests

### New Test Coverage

- **GPU paths** — `test_rt_canvas3d_gpu_paths` (595 LOC), `test_vgfx3d_backend_utils` (224 LOC), `test_vgfx3d_backend_{metal,d3d11,opengl}_shared.{c,m}`.
- **Game engine APIs** — `test_rt_entity`, `test_rt_behavior`, `test_rt_raycast_2d`, `test_rt_scene_manager`, `test_rt_animstate_named`, `test_rt_camera_enhance`, `test_rt_game_menu`, `test_rt_tilemap_anim`, `test_rt_vpa_format`, `test_rt_asset_manager`, `test_rt_path_exe_dir`.
- **Native linker** — `test_native_linker`, `test_branch_trampoline` (boundary placement), `test_reloc_applier` (COFF AArch64 BRANCH26), `test_symbol_resolver` (platform-aware), `test_coff_writer` (multi-section), `test_elf_writer` (`STT_OBJECT`), `test_pe_writer`.
- **Codegen** — 4 new AArch64 CBR edge block tests, x86_64 call ABI tests, AArch64 frame spill reuse regression, AArch64 block-layout / scheduler unit tests, Mem2Reg non-entry-block test.
- **VM** — `PerVMExternRegistryTests`, `HttpServerRuntimeTests` and `ThreadsRuntimeTests` cover environment-copy semantics for child VMs, bytecode trap-resume coverage.
- **Runtime** — `RTArgs`, `RTArchive` (path traversal + round-trip), `RTGlob`, `RTSaveData`, `RTWatcher`, `RTFileExt`, `RTGC`, `RTLineWriter`, `RTMouse` (`clear_canvas_if_matches`), `RTMsgBus`, `RTObjectIntrospect`.
- **Smoke probes** — 6 new Zia probes (`zia_smoke_paint`, `zia_smoke_viperide`, `zia_smoke_vipersql`, `zia_smoke_3dbowling`, `zia_smoke_chess`, `zia_smoke_xenoscape`) exercise real example app/game module stacks deterministically.
- **vgfx input** — `test_input.c` T22–T25 (pump-events-without-present, text-input event, scroll event, focus state sync).

### Windows Test Infrastructure

- ProcessIsolation framework reworked: `registerChildFunction()` with indexed dispatch (`--viper-child-run=N`) replaces direct pointer passing across `CreateProcess`. `dispatchChild()` added to `TEST_WITH_IL` and 16 VM/conformance tests. Failures reduced from 48 to 4.
- Codegen test assertions accept `.rdata` (Windows COFF) alongside `.rodata` (ELF), `cmovneq` → `cmovne` suffix, platform-adaptive paths.
- `rtgen` wraps generated `ZiaRuntimeExterns.inc` calls in per-namespace lambdas to dodge MSVC Debug stack overflow.
- `WinDialogSuppress.c` linked into viper/vbasic/zia executables to suppress crash dialogs in CI.

---

## Bug Fixes

Fixes are grouped by subsystem. Where a single behavior was discussed in detail in a feature section above, the bug-fix entry is a one-liner pointing at the cause.

### Compiler / Frontends

- Zia string bracket-index crash — `lowerIndex()` missing String case fell through to List path; emit `Substring(base, idx, 1)`.
- Zia `List[Boolean]` unboxing — `kUnboxI1` returned i64; added `Trunc1`.
- Zia `catch(e)` empty binding — throw now stores message via TLS, catch reads as String.
- Zia `String.Contains()` missing — added method alias to existing `StrHas`.
- Zia `main()` entry point parity — receives interface itable init + global initializer emission.
- Zia optional struct return — `emitOptionalWrap` now boxes to heap (was returning a dangling stack pointer).
- Zia `final` enforcement — reassignment of `final` variables/for-in vars/match bindings now compile-error.
- Zia runtime extern signatures — full param types so string-returning methods compare via `Viper.String.Equals`.
- Zia `List[Object].Push()` — correct extern arity for user-defined class instances.
- Zia `hide final` in class bodies — clear error instead of generic parse error.
- Zia non-constant `final` initializers — V3202 instead of silent drop.
- BASIC runtime property setter calls on runtime classes (sema symbol lookup).
- Particle emitter renders zero-alpha `Color.RGB()` values as opaque (alpha byte 0 = opaque).

### Codegen

- AArch64 `i1` boolean parameter corruption (now masked with AND 1 at function entry).
- AArch64 string store redundant refcount injection (ownership belongs in IL layer).
- AArch64 regalloc current-instruction source operand evicted while allocating same instruction's def — corrupted GEP base pointers under register pressure (broke `BowlingGame.init`).
- AArch64 regalloc `operandRoles` for immediate-ALU ops classified operand 0 as USE+DEF instead of DEF-only.
- AArch64 regalloc FPR loads/stores not recognized by `isMemLd`/`isMemSt`.
- AArch64 regalloc clean FPR values dropped across calls without spilling.
- AArch64 regalloc live-out vregs prematurely released; end-of-block spill insertion scanned backward.
- AArch64 CBR terminator dropped branch arguments on different-target branches — now emits edge blocks whenever args present.
- AArch64 loop phi-spill multi-block latch handling.
- AArch64 `TrapErr` discarded message string operand — native `throw "msg"` produced empty diagnostics.
- AArch64 `ErrGetKind/Code/Line` returned hardcoded 0 — typed catch always fell through.
- AArch64 scheduler FP/int divide latencies wrong (3 cycles instead of 7–10).
- Windows x86_64 CoffWriter rodata symbols emitted as undefined — LNK2001.
- Windows x86_64 BinaryEncoder external calls used raw IL names, not C runtime names.
- Windows x86_64 operand materialisation (`bad_variant_access` on TESTrr / call.indirect immediates).
- Windows x86_64 SETcc REX prefix wrong for SPL/BPL/SIL/DIL.
- Windows x86_64 missing MOVSD encoding for xmm RIP-relative loads.
- Windows x86_64 spill slot reuse missed cross-block liveness — overwrote live values.
- IL EarlyCSE / GVN textual-ordering bug: dominator-ordered replacement substituted a temp from a textually-later block.
- IL Mem2Reg renamer inserted unbindable synthetic block parameters in entry / single-pred blocks (verifier failures under heavy inlining).
- DllImport aggregate initializer missing `importNames` field.

### Native Linker

- Mach-O ObjC metadata sections emitted with generic flags instead of `S_CSTRING_LITERALS` / `S_LITERAL_POINTERS` / `S_ATTR_NO_DEAD_STRIP`.
- Mach-O section alignment always emitted as 0 instead of log2 of actual alignment.
- Mach-O `findWithMachoFallback` only searched plain and prefixed names — added stripped fallback.
- Mach-O ObjC class symbols (`OBJC_CLASS_$_…`) with varying underscore counts failed framework rule matching.
- Mach-O `MachOWriter` symbol-index sentinel: symbols resolved to slot 0 were treated as "not found."
- Mach-O `rt_audio_shutdown` exported as dynamic symbol but defined as weak stub — link failures on some configurations.
- ELF writer `STT_OBJECT` for rodata (was `STT_FUNC`).
- COFF `validateCoffRelocationAddend()` diagnostic for unsupported addends; `.pdata` function length field emitted (was 0).
- Native linker missing `libviper_rt_game.a` archive (symbol-not-found crash).
- `-lshell32` added to Windows linker command for `DragQueryFile`/`DragAcceptFiles`.

### Runtime

- `rt_exc_is_exception()` accepted any non-null pointer as exception (type safety violation).
- OOP destructor chaining: derived dtors never called base dtors.
- OOP refcount imbalance: `NEW` objects had refcount 2 after assignment (creation ref never released).
- TLS RSA-PSS SHA-384/SHA-512 hashing used SHA-256 for content hash.
- Bytecode VM `LOAD_I32` / `LOAD_STR_MEM` / `STORE_STR_MEM` routed to unimplemented trap handler.
- POSIX `ProcessIsolation::runIsolated()` blocked forever on hanging tests (no timeout).
- `Pixels.Blur` and `Pixels.Resize` unpacked channels as ARGB instead of RGBA — R and A swapped, producing visibly wrong colors with non-opaque alpha.
- `Mouse.SetPos` only updated internal tracking — now warps OS cursor.
- `Keyboard.CapsLock` desync from LED state — now queries OS each call.
- SaveData raw `char*` keys leaked on error paths and didn't integrate with GC — migrated to `rt_string`.
- SaveData silently produced empty results from malformed JSON — now forwards parse errors.
- Glob `*` matched path separators — now stops at directory boundaries.
- Glob case-sensitive on Windows — now uses `tolower` normalization.
- File watcher leaked Windows `OVERLAPPED.hEvent` kernel handle.
- File watcher single-file watch returned empty event paths.
- TempFile TOCTOU race — replaced with atomic `O_CREAT|O_EXCL` / `CREATE_NEW`.
- Archive extraction accepted `../` paths (zip-slip).
- Cipher `rt_cipher_decrypt` didn't fall back to legacy HKDF when PBKDF2-derived key failed.
- `rt_safe_i64.c` stale comment claiming Windows SafeI64 not implemented.
- PNG 16-bit sample downscaling discarded LSB — now round-to-nearest.
- MP3 ID3v1 tags at end of file could corrupt last decoded frame.

### Graphics

- Sprite3D use-after-free (per-frame allocation → cached instances + GC temp buffer).
- Metal lit texture: diffuse only sampled in unlit path.
- Metal spot lights fell through to ambient (no cone attenuation).
- Metal texture + sampler recreated every draw call — now cached per-frame.
- Metal CAMetalLayer presentation broken on macOS 26 Tahoe — replaced with offscreen texture readback.
- Metal clear-color alpha was 0.0 when PostFX inactive — now always 1.0.
- Software 3D backend `vgfx3d_show_gpu_layer` duplicate symbol crash — vtable dispatch.
- macOS `drawRect:` CGImage blit covered Metal layer content every frame.
- Water3D wave normals: single-sine `dydz = dydx` (identical derivatives) — Gerstner model corrects per-direction.
- GUI Image widget `vg_image_t` had no `paint` vtable — content never rendered.
- MJPEG AVI decode failed without DHT — now injects standard Annex K tables.
- GPU texture caches (Metal/D3D11/OpenGL) served stale textures when Pixels modified in-place — generation counter detects mutations.
- InstanceBatch3D `realloc` could leave partial state on allocation failure — `calloc`+copy+free pattern aborts cleanly.
- InstanceBatch3D swap-remove only moved `transforms` array — now copies all three parallel arrays.
- Mesh3D `rt_mesh3d_transform()` used direct multiply for normals — now inverse-transpose of upper 3×3.
- Canvas3D `screenshot()` returned NULL on GPU backends — now supports RTT and `backend->readback_rgba()`.
- Canvas3D didn't propagate OS resize events — added resize callback + `VGFX_EVENT_RESIZE` handling in `Poll()`.
- macOS ViperGFX windows had no application menu — auto-generated default menu.

### GUI

- Theme singletons mutated in-place — now per-app private scaled copies.
- Modal dialog routing used parallel event path that desynced — now follows real `dialog_stack`.
- Overlay timing used last-input-event timestamps — now wall-clock via `rt_gui_now_ms`.
- Dropdown placeholder used freed temporary C string — now copies to owned storage.
- Notification manager accumulated dismissed entries — now compacts immediately.
- Platform text input on non-QWERTY keyboards produced wrong characters — `VGFX_EVENT_TEXT_INPUT` replaces ASCII synthesis.
- macOS Cmd+key shortcuts not consumed by native menu bar — arrow keys triggered system beep.
- GUI menubar accelerator table leaked on destroy.

### Networking

- Network test on macOS: `getaddrinfo(NULL, ...)` with `AF_UNSPEC` prefers IPv6 — test now accepts `"::"` or `"0.0.0.0"`.
- `test_rt_network_highlevel` SSE chunked mock server: wrong hex chunk size (`0x14` for 23-byte payload, should be `0x17`) and missing trailing `\r\n` per RFC 7230 §4.1.

### Packaging

- DEFLATE double-free crash in VAPS.
- `.lnk` shortcuts missing LinkInfo structure.

### Documentation

- Bible chapters showed stale error-message format (`error:` instead of `error[V3000]:`).
- Networking examples used non-existent structured response API instead of actual `Http.Get`/`HttpReq`/`HttpRes`.
- Bible appendix still used old `entity`/`value` terminology instead of `class`/`struct`.
- ViperSQL demo: 9 bug fixes (modulo operator, REPLACE parsing, JOIN IS NULL, window PARTITION BY, LIKE case sensitivity, ALTER TABLE locking, CSV validation, persistence NULL crash, distinct hash function).
- PlatformerController velocity desync after damage knockback, death bounce, enemy stomp.
- Windows `CrossLayerArithTests` missing `dispatchChild()` guard caused infinite process recursion.
- Windows crash dialogs suppressed via `SetErrorMode` + `_set_abort_behavior` in `rt_init_stack_safety`.
