# Viper Repository Audit: Top 10 Architectural Findings (Verified)

## Context

Deep architectural review of the Viper compiler toolchain — 80+ core files examined across three verification rounds. Each finding below has been confirmed against actual source code with exact file paths and line numbers. Six initial claims were refuted during verification and replaced.

**Codebase health highlights discovered during review:**
- VM has proper recursion guard (`kMaxRecursionDepth = 1000` at `VMConstants.hpp:28`, enforced at `VM.cpp:903`)
- IL verifier checks block label uniqueness at two levels (`FunctionVerifier.cpp:204-215` + `IRBuilder.cpp:179-189`)
- Pool allocator properly shuts down via atexit (`rt_heap.c:86` → `rt_pool_shutdown()`)
- Module linker's `m0$name` disambiguation uses `$` which is invalid in user identifiers — collision-proof by design
- x86-64 ISel has a proper `LoweringRules` registry pattern with `match`/`emit` function pointers
- Both backends generate tables from JSON specs (`x86_64_encodings.json` → `EncodingTable.inc` + `OpFmtTable.inc`; `aarch64_encodings.json` → `OpcodeDispatch.inc`)
- Relocation error handling is intentionally split: linker uses error returns (external input), encoders use asserts (internal invariants)

---

## 1. BACKEND ARCHITECTURE ASYMMETRY — The Scaling Wall

**Status: CONFIRMED** | `Backend.cpp` vs `CodegenPipeline.cpp`, `CallLowering.hpp` vs embedded, `FrameLowering.hpp` vs `FrameBuilder.hpp`

The two backends evolved independently with no shared interface. They solve the same problems using fundamentally different patterns:

| Aspect | x86-64 | AArch64 |
|--------|--------|---------|
| Pipeline | Monolithic per-function loop (`Backend.cpp:139`) | 6-phase PassManager (`CodegenPipeline.cpp:54-150`) |
| ISel | Separate `ISel.cpp` (1084 LOC) + rules registry in `LoweringRules.cpp` | Fused into `InstrLowering.cpp` (1911 LOC) with handler groups |
| Call lowering | Dedicated `CallLowering.cpp` with `CallLoweringPlan` struct (line 51) | Embedded inline in `InstrLowering.cpp:658-734` |
| Frame layout | Static utility functions (`FrameLowering.hpp:43-46`) | Stateful `FrameBuilder` class with incremental API (`FrameBuilder.hpp:60-162`) |
| RegAlloc | Allocator + Spiller + **Coalescer** (5 modules) | Monolithic `LinearAllocator` (**no Coalescer**, different pool strategy) |
| Spill slots | Fresh allocation only | Dead slot reuse (`ensureSpillWithReuse`) |

There is no `BackendInterface` or abstract base — adding a RISC-V backend has no template to follow. Each backend's innovations are invisible to the other (AArch64's spill reuse, x86's Coalescer).

**Fix:** Extract shared abstractions incrementally — unified `CallLoweringPlan`, shared `FrameInterface`, `LinearScanTemplate<ArchTraits>`.

**Files:** `src/codegen/x86_64/Backend.cpp`, `src/codegen/aarch64/CodegenPipeline.cpp`, `CallLowering.*`, `FrameLowering.*`, `FrameBuilder.*`

---

## 2. MIR OPCODES STILL REQUIRE ~5 MANUAL FILE EDITS PER NEW INSTRUCTION

**Status: CONFIRMED (corrected from 8-10)** | Both backends have partial JSON-driven generation but the enum, name table, and regalloc classification remain manual.

Both backends generate some tables from JSON specs (`docs/spec/x86_64_encodings.json`, `docs/spec/aarch64_encodings.json`), which is good. But adding a new MIR opcode still requires manually editing:

1. `MachineIR.hpp` — add enum value to `MOpcode` (82 opcodes in AArch64, `MachineIR.hpp:48-143`)
2. `MachineIR.cpp` — add to parallel name table (372 lines)
3. `OpcodeClassify.hpp` — add regalloc operand classification (AArch64, 164 lines)
4. `A64BinaryEncoder.cpp` or `X64BinaryEncoder.cpp` — add encoding logic (if not covered by generated tables)
5. Peephole passes — add optimization patterns (across multiple files under `peephole/`)

The generated tables handle assembly emission (`AsmEmitter.cpp` includes `OpcodeDispatch.inc`) and x86-64 encoding/format tables. But the MOpcode enum, the name table, and the regalloc classification are not generated — they must be manually kept in sync.

**Why it matters:** Missing an OpcodeClassify entry means the register allocator silently misclassifies operands. Missing a name table entry means the opcode prints as `<unknown>` in debug output.

**Fix:** Extend the JSON spec to also generate the enum definition and name table via CMake custom commands — same pattern already used for `EncodingTable.inc`. Add a build-time validation that every `MOpcode` enum value appears in the name table and classify table.

**Files:** `src/codegen/aarch64/MachineIR.hpp:48-143`, `MachineIR.cpp`, `OpcodeClassify.hpp`, `cmake/GenAArch64Dispatch.cmake`, `docs/spec/aarch64_encodings.json`

---

## 3. FRONTEND GLOBAL MUTABLE STATE BREAKS CONCURRENT COMPILATION

**Status: CONFIRMED** | Three instances at exact locations

**Instance A — Zia lambda counter:**
`src/frontends/zia/Lowerer_Expr_Lambda.cpp:40`:
```cpp
static int lambdaCounter = 0;
std::string lambdaName = "__lambda_" + std::to_string(lambdaCounter++);
```
Process-global, unsynchronized static counter. Two concurrent compilations (e.g., from zia-server handling multiple files) generate duplicate lambda names.

**Instance B — BASIC TypeRegistry:**
`src/frontends/basic/sem/TypeRegistry.cpp:111`:
```cpp
static TypeRegistry reg;
```
Singleton seeded once, never reset. Second compilation inherits stale type metadata.

**Instance C — BASIC RuntimePropertyIndex:**
`src/frontends/basic/sem/RuntimePropertyIndex.cpp:62`:
```cpp
static RuntimePropertyIndex idx;
```
Same singleton pattern. Accumulates state across compilations.

**Why it matters:** The zia-server (VSCode extension) and any embedded compiler use case (REPL, IDE, batch compilation) run multiple compilations in the same process. These globals cause duplicate symbols, type confusion, and non-deterministic behavior.

**Fix:** Move `lambdaCounter` to a Lowerer instance field (trivial). Convert TypeRegistry and RuntimePropertyIndex to per-compilation-context objects passed as parameters.

**Files:** `src/frontends/zia/Lowerer_Expr_Lambda.cpp:40`, `src/frontends/basic/sem/TypeRegistry.cpp:111`, `src/frontends/basic/sem/RuntimePropertyIndex.cpp:62`

---

## 4. AArch64 ABI INCOMPLETE — No Variadic Support, No PAC/BTI

**Status: CONFIRMED** | Verified by exhaustive grep across `src/codegen/aarch64/` — zero matches for `isVarArg`, `vararg`, `pac`, `PACIASP`, `bti`, `BTI`

**A. Variadic functions:** x86-64 has `isVarArg` in both `FunctionMetadata` (`MachineIR.hpp:185`) and `CallLoweringPlan` (`CallLowering.hpp:56`). AArch64 has nothing. Calling `printf` or any variadic C function from AArch64 native code is undefined behavior under AAPCS64.

**B. Pointer Authentication (PAC):** macOS ARM64 requires `PACIASP`/`AUTIASP` in function prologues/epilogues for Hardened Runtime. Without PAC, binaries may be rejected on macOS Sonoma+ with security features enabled. ~20 lines of code to emit.

**C. Branch Target Identification (BTI):** ARM64 CFI requires `BTI C` at indirect call targets. ~10 lines to emit.

**Fix:** Add `isVarArg` to AArch64 MFunction + call lowering (~200 LOC). Emit PAC/BTI gated on target flag (~30 LOC).

**Files:** `src/codegen/aarch64/MachineIR.hpp`, `src/codegen/aarch64/InstrLowering.cpp:658-734`, `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp`

---

## 5. INTERNAL COMPILER ERRORS VIA ASSERT INSTEAD OF DIAGNOSTICS

**Status: CONFIRMED** | 5+ assert sites that crash the compiler process

When upstream passes produce invalid MIR (unallocated vregs, unsupported opcodes, non-convergent dataflow), the compiler aborts with a raw C++ assertion failure instead of producing a diagnostic:

| File | Line | Assert | Consequence |
|------|------|--------|-------------|
| `X64BinaryEncoder.cpp` | 42 | `assert(reg.isPhys)` | Crash if vreg reaches encoder |
| `X64BinaryEncoder.cpp` | 414 | `assert(false && "unhandled MOpcode")` | Crash on unknown opcode |
| `A64BinaryEncoder.cpp` | 59 | `assert(op.reg.isPhys)` | Crash if vreg reaches encoder |
| `AsmEmitter.cpp` | 859 | `assert(op.reg.isPhys)` | Crash if vreg reaches emitter |
| `DataflowLiveness.hpp` | 82-86 | `assert(false && "did not converge")` | **Debug: crash. Release: silently wrong liveness → wrong register assignments → miscompilation** |

The DataflowLiveness case is particularly dangerous: in release builds, the `assert` is stripped and `break` exits the loop with incorrect liveness data, which feeds into the register allocator, producing silently wrong code.

**Fix:** Replace with `reportICE()` function that prints "internal compiler error: [context]", suggests filing a bug report, and exits cleanly. For DataflowLiveness, always check convergence (not just in debug).

**Files:** `src/codegen/x86_64/binenc/X64BinaryEncoder.cpp:42,414`, `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp:59`, `src/codegen/aarch64/AsmEmitter.cpp:859`, `src/codegen/common/ra/DataflowLiveness.hpp:82-86`

---

## 6. NO UNWIND METADATA IN MACH-O OUTPUT BINARIES

**Status: CONFIRMED** | Mach-O Reader explicitly skips unwind sections; Writer never generates them

The Mach-O reader (`MachOReader.cpp:226-227`) explicitly skips `__compact_unwind` and `__eh_frame` sections from input objects (marks them "Unmapped"). The writer never generates either section.

Additionally, the AArch64 encoder has a leaf-frame optimization (`A64BinaryEncoder.cpp:95-103`) that skips frame pointer setup for leaf functions:
```cpp
skipFrame_ = fn.isLeaf && fn.savedGPRs.empty() && fn.savedFPRs.empty() &&
             fn.localFrameSize == 0 && fn.name != "main";
```

**Combined effect:** No Viper-compiled function — leaf or non-leaf — has unwind metadata. Debuggers (`lldb`), profilers (`Instruments`), Apple's crash reporter, and C++ exception unwinding cannot traverse Viper-compiled frames. Stack traces are truncated at the first Viper function.

**Fix (staged):**
1. Always emit frame pointer on Apple targets (Apple's default, ~1 line change)
2. Generate `__compact_unwind` entries per function (~200-300 LOC in MachOExeWriter)
3. Long-term: `__eh_frame` for C++ exception interop

**Files:** `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp:95-103`, `src/codegen/common/linker/MachOExeWriter.cpp`, `src/codegen/common/linker/MachOReader.cpp:226-227`

---

## 7. std::string BLOAT IN BOTH VALUE (IL) AND MOPERAND (MIR)

**Status: CONFIRMED (corrected to 48 bytes)** | Same pattern at two levels of the IR hierarchy

**IL Level — Value struct** (`Value.hpp:33-67`):
```cpp
struct Value {
    Kind kind;           // 4 bytes
    union { i64; f64; id; }; // 8 bytes
    std::string str;     // 24 bytes ← only used by ConstStr + GlobalAddr
    bool isBool;         // 1 byte
};  // Total: 48 bytes (with alignment)
```
4 of 6 `Kind` variants never use `str`. Every Value copy default-constructs/copies/destroys a 24-byte `std::string` for nothing.

**MIR Level — MOperand struct** (`MachineIR.hpp:161-225`):
```cpp
struct MOperand {
    Kind kind;              // 4 bytes
    MReg reg;               // ~4 bytes
    long long imm;          // 8 bytes
    const char *cond;       // 8 bytes
    std::string label;      // 24 bytes ← only used by Kind::Label
};
```
Same pattern: register and immediate operands carry a dead 24-byte `std::string`. Each `MInstr` has a `std::vector<MOperand>` with 2-4 operands per instruction.

**Scale:** A module with 10K IL instructions → ~30K Values (~720KB wasted). After lowering to MIR with ~20K instructions × 3 operands → ~60K MOperands (~1.4MB wasted). Every copy touches the dead string's SSO buffer.

**Fix:** Replace inline `std::string` with `uint32_t strIndex` indexing a per-module/per-function string pool. Values shrink from 48 → 24 bytes; MOperands from ~56 → ~32 bytes. StringTable exists in the Zia lowerer — generalize to IL core.

**Files:** `src/il/core/Value.hpp:33-67`, `src/codegen/aarch64/MachineIR.hpp:161-225` (same pattern in x86_64)

---

## 8. NO SHARED REGISTER ALLOCATOR INFRASTRUCTURE

**Status: CONFIRMED** | Only DataflowLiveness.hpp shared; all other RA code duplicated

Both backends implement independent linear-scan register allocators:

| Component | x86-64 | AArch64 | Shared? |
|-----------|--------|---------|---------|
| Liveness solver | `ra/Liveness.cpp` | `ra/Liveness.cpp` | **Yes** (both call `DataflowLiveness.hpp`) |
| Live intervals | `ra/LiveIntervals.cpp` | (inline in Allocator) | No |
| Allocator core | `ra/Allocator.cpp` (1169 LOC) | `ra/Allocator.cpp` (716 LOC) | No |
| Spiller | `ra/Spiller.cpp` | (inline in Allocator) | No |
| Coalescer | `ra/Coalescer.cpp` | **MISSING** | N/A |
| Register pools | (inline) | `ra/RegPools.cpp` | No |

The algorithms are the same (linear scan with furthest-next-use spilling), but implementations diverge because they're parameterized on different register file definitions. AArch64 has call-aware spilling and spill slot reuse that x86-64 lacks. x86-64 has a dedicated Coalescer that AArch64 lacks.

**Why it matters:** Improving the allocator (live range splitting, better heuristics, the missing AArch64 coalescer) requires parallel development. When x86-64 gets better, AArch64 stays the same and vice versa.

**Fix:** Create `codegen/common/ra/LinearScanTemplate.hpp` — CRTP or traits-based template parameterized on `ArchTraits` (register file, calling convention, frame interface). Already proven: `PeepholeDCE.hpp` and `PeepholeCopyProp.hpp` use this exact template pattern.

**Files:** `src/codegen/common/ra/DataflowLiveness.hpp` (exists), `src/codegen/x86_64/ra/Allocator.cpp`, `src/codegen/aarch64/ra/Allocator.cpp`, `src/codegen/x86_64/ra/Coalescer.cpp` (AArch64 needs equivalent)

---

## 9. IL HAS NO THREAD-LOCAL STORAGE DESPITE LINKER HAVING TLS INFRASTRUCTURE

**Status: CONFIRMED** | Linker handles TLS relocations for Mach-O; IL and frontends cannot express thread-local semantics

The Mach-O linker already recognizes TLS:
- `RelocConstants.hpp:70-71` defines `kTlvpLoadPage21` and `kTlvpLoadPageOff12`
- `RelocApplier.cpp:235-268` handles `.tdata` sections and TLV descriptor bootstrap
- `RelocClassify.hpp:125-131` maps TLV relocation types to actions

But the IL layer has no way to express thread-local semantics:
- `Global.hpp` has no `thread_local` attribute (verified, lines 38-57)
- `Linkage.hpp` only defines `Internal`/`Export`/`Import` — no TLS variants
- `docs/il-guide.md` has zero mentions of TLS
- Neither frontend (Zia, BASIC) can declare thread-local variables

**Why it matters:** The runtime already uses TLS internally (`thread_local` trap tokens at `Trap.cpp:49-51`). But user programs compiled through IL → native cannot have per-thread state. This forces all concurrency patterns through the runtime threading API, preventing efficient per-thread caches, thread-local error buffers, or thread-local allocator state.

**Fix (staged):**
1. Add `thread_local` attribute to IL Global declarations
2. Frontend support: `thread var x: Integer` (Zia), `THREAD DIM x AS INTEGER` (BASIC)
3. x86-64: `%fs`-relative addressing for TLS globals
4. AArch64: `mrs TPIDR_EL0` + offset
5. ELF/PE writers: `.tdata`/`.tbss` sections (Mach-O already handled)

**Files:** `src/il/core/Global.hpp`, `src/il/core/Linkage.hpp`, `src/codegen/common/linker/RelocConstants.hpp:70-71`

---

## 10. LIVENESS NON-CONVERGENCE SILENTLY MISCOMPILES IN RELEASE BUILDS

**Status: CONFIRMED** | `DataflowLiveness.hpp:82-86`

```cpp
if (++iteration > maxIter)
{
    assert(false && "Liveness dataflow did not converge");
    break;  // In release: assert stripped, loop exits with WRONG data
}
```

In debug builds, this crashes (acceptable — fail-fast). In release builds, `assert` is compiled out. The `break` exits the fixed-point iteration with incomplete liveness data. This feeds into the register allocator, which makes allocation decisions based on incorrect live ranges — potentially assigning the same physical register to two simultaneously live virtual registers.

**Why it matters:** This is the only place in the codebase where a debug assert guards a correctness-critical invariant with a fallthrough that produces silently wrong results. A pathological CFG (deeply nested loops, irreducible control flow) could trigger non-convergence. The result is a native binary that silently computes wrong answers — the hardest class of compiler bug to diagnose.

**Fix:** Replace with unconditional error:
```cpp
if (++iteration > maxIter)
{
    // Always fail, not just in debug
    reportICE("liveness dataflow did not converge after " +
              std::to_string(maxIter) + " iterations");
    return;  // or throw
}
```

**Files:** `src/codegen/common/ra/DataflowLiveness.hpp:82-86`

---

## Priority Order

| # | Finding | Severity | Effort | Category |
|---|---------|----------|--------|----------|
| 1 | Backend architecture asymmetry | **Architectural** | 2-3 weeks | Scaling |
| 2 | MIR opcodes need ~5 manual edits | **Design** | 2-3 days | Maintainability |
| 3 | Frontend global mutable state | **Correctness** | 2-3 hours | Concurrent use |
| 4 | AArch64 ABI gaps (varargs, PAC, BTI) | **Compliance** | 3-5 days | Production ARM64 |
| 5 | ICE via assert (5+ sites) | **UX/Correctness** | 1-2 days | Error handling |
| 6 | No Mach-O unwind metadata | **Debugging** | 3-5 days | Deployability |
| 7 | std::string bloat in Value + MOperand | **Performance** | 2-3 days | Optimization |
| 8 | No shared regalloc template | **Architectural** | 1-2 weeks | Scaling |
| 9 | IL has no TLS (linker has partial support) | **Feature** | 2-3 weeks | Completeness |
| 10 | Liveness non-convergence → silent miscompile | **Correctness** | 1 hour | Critical bug |

## Quick Wins (< 1 day each)

- **#10** Liveness convergence — Replace `assert+break` with unconditional error. **1 hour, fixes a real miscompilation risk.**
- **#3** Frontend globals — Move lambdaCounter to instance, make registries non-singleton. **2-3 hours.**
- **#4B/C** PAC/BTI — Emit `PACIASP`/`AUTIASP`/`BTI C` in prologue. **~30 LOC, 2-3 hours.**

## Verification

After addressing findings:
1. `./scripts/build_viper.sh` — full build + all tests pass on both architectures
2. Debug + Release builds: Feed pathological CFG, verify liveness error instead of silent wrong code
3. Concurrent compilation test: Two Zia compilations in same process, verify no lambda name collisions
4. `objdump -d` on AArch64 binary: Confirm `paciasp`/`autiasp` in prologues
5. `lldb` on macOS: Verify stack unwinding through Viper-compiled functions
6. `sizeof(Value)` compile-time check: Verify 24 bytes after string pool refactor
7. MIR opcode: Add test opcode to JSON spec only, verify generated tables update
