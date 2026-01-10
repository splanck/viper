# Performance Analysis Report: Viper Compiler Toolchain

**Date:** 2026-01-09
**Last Updated:** 2026-01-09
**Scope:** Full codebase (~409K LOC)
**Focus:** Performance anti-patterns, O(n²) algorithms, inefficient data structures

---

## Executive Summary

Analysis of the Viper compiler toolchain identified several performance improvement opportunities. The codebase is generally well-engineered with thoughtful optimizations already in place (computed goto dispatch, pre-reserved vectors, cached flags). However, some hot paths contain suboptimal patterns.

**Status:** 6 of 8 issues have been resolved (see individual issue status below).

---

## Critical Issues

### 1. SCCP - O(n²) Use Replacement ✅ FIXED

**Status:** RESOLVED - The SCCP solver now uses the `uses_` map for O(1) replacement lookups.

**Location:** `src/il/transform/SCCP.cpp:1329-1344`

```cpp
void replaceAllUses(unsigned id, const Value &replacement)
{
    for (auto &block : function_.blocks)      // O(blocks)
        for (auto &instr : block.instructions) // O(instrs)
            for (auto &operand : instr.operands)
                if (operand.kind == Value::Kind::Temp && operand.id == id)
                    operand = replacement;
}
```

**Problem:** For each constant found, entire function is traversed. With k constants: O(k × n).

**Fix:** Solver already builds `uses_` map. Use it for direct replacement:
```cpp
void replaceAllUses(unsigned id, const Value &replacement)
{
    auto it = uses_.find(id);
    if (it == uses_.end()) return;
    for (Instr *instr : it->second)
    {
        for (auto &op : instr->operands)
            if (op.kind == Value::Kind::Temp && op.id == id)
                op = replacement;
    }
}
```

**Priority:** HIGH - Affects every optimization pass run

---

### 2. Liveness Analysis - Inefficient Bitsets ✅ FIXED

**Status:** RESOLVED - Liveness analysis now uses `uint64_t` chunk-based bitsets with SIMD-friendly merge operations.

**Location:** `src/il/transform/analysis/Liveness.cpp:260`

```cpp
std::vector<bool> defs(valueCount, false);
std::vector<bool> uses(valueCount, false);
```

**Problem:** `std::vector<bool>` uses bit-packing with poor cache behavior. The `mergeBits()` function iterates element-by-element without SIMD.

**Fix:** Use `uint64_t` chunk-based bitset:
```cpp
class FastBitset {
    std::vector<uint64_t> chunks_;
public:
    void merge(const FastBitset &other) {
        for (size_t i = 0; i < chunks_.size(); ++i)
            chunks_[i] |= other.chunks_[i];  // SIMD-friendly
    }
};
```

**Priority:** HIGH - Liveness computed iteratively for every function

---

### 3. Register Allocator - Linear Scans in Active Sets ✅ FIXED

**Status:** RESOLVED - Active sets now use `std::unordered_set<uint16_t>` for O(1) insert/erase operations.

**Location:** `src/codegen/x86_64/ra/Allocator.cpp:177-194`

```cpp
void LinearScanAllocator::addActive(RegClass cls, uint16_t id)
{
    auto &active = activeFor(cls);
    if (std::find(active.begin(), active.end(), id) == active.end())
        active.push_back(id);
}
```

**Problem:** O(n) membership check per register operation.

**Fix:** Use `std::unordered_set<uint16_t>`:
```cpp
std::unordered_set<uint16_t> activeGPR_;
void addActive(RegClass cls, uint16_t id) {
    activeFor(cls).insert(id);  // O(1) average
}
```

**Priority:** MEDIUM - Bounded by register count (~15)

---

## Medium Priority Issues

### 4. Verifier - Triple Traversal ✅ FIXED

**Status:** RESOLVED - The verifier now collects EhPush targets and label references during the `verifyBlock()` pass, then validates afterward in two O(n) passes over the collected items.

**Location:** `src/il/verify/FunctionVerifier.cpp:213-244`

Three separate passes over all blocks:
1. `verifyBlock()` per block
2. Check EhPush targets
3. Validate all label references

**Fix:** Collect targets and references during `verifyBlock()`, validate afterward.

---

### 5. Caller-Saved Register Check ✅ FIXED

**Status:** RESOLVED - The allocator now uses `std::bitset<32>` members (`callerSavedGPRBits_`, `callerSavedXMMBits_`) initialized at construction for O(1) caller-saved register lookup.

**Location:** `src/codegen/x86_64/ra/Allocator.cpp:366-370`

```cpp
auto isCallerSaved = [this](PhysReg reg, RegClass cls) {
    return std::find(regs.begin(), regs.end(), reg) != regs.end();
};
```

**Fix:** Pre-compute `std::bitset<16>` of caller-saved registers during initialization.

---

### 6. AArch64 Opcode Mapping Lookups ✅ FIXED

**Status:** RESOLVED - `lookupBinaryOp()` and `lookupCondition()` now use switch statements for O(1) opcode dispatch instead of linear table scans.

**Location:** `src/codegen/aarch64/OpcodeMappings.hpp:88-103`

Sequential linear scans through mapping tables.

**Fix:** Use `std::array` indexed by opcode enum value, or switch statement.

---

## Low Priority Issues

### 7. String Allocations in Error Paths

**Location:** `src/vm/VM.cpp:336-342`

String construction for error messages even in paths that may not report.

**Fix:** Lazy construction with lambdas.

---

### 8. Map Double-Lookups

Pattern across codebase:
```cpp
if (map.find(key) != map.end())
    auto value = map[key];  // Second lookup
```

**Fix:** Use iterator from find:
```cpp
auto it = map.find(key);
if (it != map.end())
    use(it->second);
```

---

## Positive Patterns (Already Optimized)

1. **VM Dispatch:** Computed goto with label tables (ThreadedDispatchDriver)
2. **Block Maps:** `std::unordered_map` for O(1) label lookups
3. **Pre-reserved Vectors:** `.reserve()` before push_back loops
4. **Cached Flags:** VM caches `tracingActive_` avoiding per-instruction calls
5. **Move Semantics:** Proper `std::move()` in instruction rewriting
6. **Static Handler Tables:** Opcode handlers in static arrays, no runtime lookup

---

## Recommendations Summary

| Issue | Location | Priority | Complexity | Status |
|-------|----------|----------|------------|--------|
| SCCP use replacement | SCCP.cpp:1329 | HIGH | LOW | ✅ FIXED |
| vector<bool> bitsets | Liveness.cpp | HIGH | MEDIUM | ✅ FIXED |
| RA active set scans | Allocator.cpp:177 | MEDIUM | LOW | ✅ FIXED |
| Verifier triple pass | FunctionVerifier.cpp | MEDIUM | MEDIUM | ✅ FIXED |
| Caller-saved lookup | Allocator.cpp:366 | MEDIUM | LOW | ✅ FIXED |
| Opcode mapping lookup | OpcodeMappings.hpp | LOW | LOW | ✅ FIXED |
| Error path strings | VM.cpp:336 | LOW | LOW | Open |
| Map double-lookups | Various | LOW | LOW | Open |

---

## Next Steps

1. ~~Address HIGH priority issues first (SCCP, Liveness)~~ ✅ Done
2. ~~Address MEDIUM priority issues (RA, Verifier, Caller-saved, AArch64)~~ ✅ Done
3. Benchmark before/after with representative programs
4. Consider profiling with `perf` or Instruments on real workloads
5. Monitor regression in future changes
6. Address remaining LOW priority issues (error path strings, map double-lookups) as time permits

