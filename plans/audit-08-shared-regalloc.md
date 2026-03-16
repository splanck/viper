# Audit Finding #8: Shared Register Allocator Template

## Problem
Both backends implement independent linear-scan allocators (~1900 LOC total). Only `DataflowLiveness.hpp` is shared. AArch64 lacks Coalescer; x86-64 lacks call-aware spilling and spill slot reuse.

## Current Shared Infrastructure
- `codegen/common/ra/DataflowLiveness.hpp` — backward dataflow solver, templated on `VregId`
- `codegen/common/PeepholeDCE.hpp` — traits-based template (proven pattern)
- `codegen/common/PeepholeCopyProp.hpp` — traits-based template (proven pattern)
- `codegen/common/TargetInfoBase.hpp` — shared target info base struct

## Implementation Plan

### Phase 1: Define ArchTraits Interface (1-2 days)

Create `src/codegen/common/ra/ArchTraits.hpp`:
```cpp
template <typename Derived>
struct ArchTraitsBase {
    // Types
    using MInstr   = ...;  // Target MInstr type
    using MBlock   = ...;  // Target MBasicBlock type
    using MFunc    = ...;  // Target MFunction type
    using PhysReg  = ...;  // Target PhysReg enum
    using RegClass = ...;  // GPR/FPR/XMM

    // Register file queries
    static bool isCallerSaved(PhysReg r, const TargetInfo &ti);
    static RegClass regClassOf(PhysReg r);
    static std::span<const PhysReg> allocatableGPRs(const TargetInfo &ti);
    static std::span<const PhysReg> allocatableFPRs(const TargetInfo &ti);

    // Operand access (same pattern as PeepholeCopyProp traits)
    static std::pair<bool, bool> classifyOperand(const MInstr &, size_t idx);  // (isUse, isDef)
    static uint16_t vregId(const MInstr &, size_t idx);
    static RegClass vregClass(const MInstr &, size_t idx);
    static void rewriteVreg(MInstr &, size_t idx, PhysReg phys);

    // Spill/reload instruction creation
    static MInstr makeSpill(PhysReg src, int fpOffset, RegClass cls);
    static MInstr makeReload(PhysReg dst, int fpOffset, RegClass cls);
    static MInstr makeMove(PhysReg dst, PhysReg src, RegClass cls);
};
```

### Phase 2: Extract Shared Linear Scan Core (1 week)

Create `src/codegen/common/ra/LinearScanCore.hpp`:
```cpp
template <typename Traits>
class LinearScanCore {
    using MFunc = typename Traits::MFunc;
    using PhysReg = typename Traits::PhysReg;

    // Shared algorithms:
    void buildLiveIntervals(const MFunc &fn, const DataflowResult &liveness);
    PhysReg selectVictim(RegClass cls);  // furthest-next-use
    void insertSpill(PhysReg r, int slot, ...);
    void insertReload(int slot, PhysReg r, ...);
    void expireOldIntervals(unsigned currentPos);

    // Hooks for arch-specific behavior (called via Traits):
    // - Frame slot allocation (FrameBuilder vs FrameInfo)
    // - Call-aware spilling (AArch64 has it, x86 will get it)
    // - Spill slot reuse (AArch64 has it, x86 will get it)
};
```

### Phase 3: Port Both Backends to Template (3-5 days)

1. Define `X86ArchTraits` implementing the traits interface
2. Define `AArch64ArchTraits` implementing the traits interface
3. Replace `x86_64/ra/Allocator.cpp` with `LinearScanCore<X86ArchTraits>`
4. Replace `aarch64/ra/Allocator.cpp` with `LinearScanCore<AArch64ArchTraits>`
5. Port x86-64 Coalescer to work with the template (should be arch-independent since it operates on physical registers)

### Phase 4: Propagate Best-of-Both Features (2-3 days)
With shared core, features become available to both:
- Call-aware spilling → x86-64 gets it via template
- Spill slot reuse → x86-64 gets it via template
- Coalescer → AArch64 gets it via shared Coalescer pass

### Files to Create
- `src/codegen/common/ra/ArchTraits.hpp` — trait interface
- `src/codegen/common/ra/LinearScanCore.hpp` — shared allocator template

### Files to Modify
- `src/codegen/x86_64/ra/Allocator.cpp` — use LinearScanCore<X86ArchTraits>
- `src/codegen/aarch64/ra/Allocator.cpp` — use LinearScanCore<AArch64ArchTraits>
- `src/codegen/x86_64/ra/Coalescer.cpp` — generalize to work with both backends

### Verification
1. `./scripts/build_viper.sh` — all tests pass on both architectures
2. Compare register allocation quality (spill count) before/after on benchmarks
3. Verify AArch64 now coalesces moves (new capability)
4. Verify x86-64 now reuses spill slots (new capability)
