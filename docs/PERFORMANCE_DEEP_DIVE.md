# Viper Performance Deep Dive Report

**Date:** January 2026
**Scope:** VM Execution, Native Codegen, IL Optimization, Runtime Library
**Goal:** Identify and prioritize performance bottlenecks

---

## Executive Summary

This report presents findings from a comprehensive performance investigation of the Viper compiler and runtime. The codebase is well-engineered with many optimizations already in place. The remaining opportunities are moderate efficiency issues (1-8% each) rather than architectural flaws.

### Current Performance Status
- **Native fib(45):** 7.50s (3.7x slower than C -O2, comparable to C#)
- **VM fib(45):** ~1500s estimated (750x slower than native)

### Key Findings Summary

| Component | Issues Found | Potential Gain | Priority |
|-----------|--------------|----------------|----------|
| VM Call Handler | Binding allocation overhead | 2-4% | HIGH |
| VM Fast Path | String comparison overhead | 2-5% | MEDIUM |
| Native Codegen | Missing MADD instruction | 2-5% | HIGH |
| Native Codegen | No CSEL (conditional select) | 2-8% | HIGH |
| Native Codegen | Copy propagation disabled | 2-5% | MEDIUM |
| IL Optimizer | Cross-block DSE not called | 2-5% | HIGH |
| IL Optimizer | Conservative inlining | 5-10% | MEDIUM |

---

## 1. Virtual Machine Performance

### 1.1 Architecture Overview

The VM supports three dispatch strategies:
- **Threaded (computed goto):** Fastest, GCC/Clang only
- **Switch dispatch:** Good performance, universal
- **Function table:** Moderate, universal

The dispatch infrastructure is well-optimized with:
- ✅ 8-byte Slot union (trivially copyable)
- ✅ SmallVector<8> for arguments (no heap for ≤8 args)
- ✅ Cached strategy properties (no virtual calls per iteration)
- ✅ Branch hints for fast-path layout

### 1.2 Critical Issues

#### Issue VM-1: Unconditional Binding Vector Allocation (HIGH)

**Location:** `src/vm/ops/Op_CallRet.cpp:252-274`

```cpp
// PROBLEM: Allocated for EVERY non-native call
struct ArgBinding { /* ... */ };
SmallVector<ArgBinding, 8> bindings;
bindings.reserve(in.operands.size());
SmallVector<Slot, 8> originalArgs;
originalArgs.reserve(in.operands.size());
```

**Impact:** 2-4% overhead for call-heavy code
**Fix:** Defer allocation to error path (only when runtime signature exists)

#### Issue VM-2: String Comparisons for Fast Path (MEDIUM)

**Location:** `src/vm/ops/Op_CallRet.cpp:166-238`

```cpp
if (in.callee == "rt_inkey_str") { /* ... */ }
if (in.callee == "rt_term_locate_i32" && args.size() >= 2) { /* ... */ }
// ... 6 more string comparisons
```

**Impact:** 2-5% overhead for general programs
**Better Approach:** Hash-based lookup or opcode hint from IL

#### Issue VM-3: Signature Lookup on Every Call (MEDIUM)

**Location:** `src/vm/ops/Op_CallRet.cpp:285-330`

Every runtime call performs signature lookup just to check for mutations.

**Impact:** 1-3% overhead
**Fix:** Cache signatures per callsite or flag immutable-only calls

### 1.3 Memory Layout

The Frame structure is larger than ideal for cache efficiency:
- Current: ~200+ bytes with multiple vector indirections
- Hot fields (func, sp, regs) scattered across cache lines

**Recommendation:** Pack hot fields into first 64-byte cache line.

---

## 2. IL Optimization Passes

### 2.1 Current Pipeline (O2)

The O2 pipeline runs 19+ passes including:
- ✅ Loop simplify, indvars, unroll
- ✅ Mem2reg (SSA construction)
- ✅ SCCP (dual: pre and post inline)
- ✅ Inline with cost model
- ✅ LICM, GVN, EarlyCSE, DSE
- ✅ Peephole (57 patterns)
- ✅ DCE (runs 3×)

### 2.2 Critical Issues

#### Issue IL-1: Cross-Block DSE Not Enabled (HIGH)

**Location:** `src/il/transform/DSE.cpp`

The `runCrossBlockDSE()` function exists but is not called in the pipeline.

**Impact:** 2-5% for memory-heavy code
**Fix:** Add to O2 pipeline after GVN

#### Issue IL-2: Conservative Inlining (MEDIUM)

**Current limits:**
- Max 32 instructions per callee
- Max 4 basic blocks
- Max 2 nesting depth

Most real functions exceed these limits.

**Impact:** 5-10% for call-heavy code
**Fix:** Increase to 64-128 instructions, 8 blocks

#### Issue IL-3: No Tail Call Optimization (DEFERRED)

Recursive functions grow stack indefinitely. Would require:
- New IL opcode (`tailcall`)
- VM trampoline implementation
- Codegen support

**Impact:** 5-10% for recursive code + stack safety
**Status:** Deferred due to architectural scope

### 2.3 Missing Optimizations

| Optimization | Impact | Complexity |
|--------------|--------|------------|
| Loop unswitching | 3-8% | Medium |
| Reassociation | 2-5% | Low-Medium |
| Function cloning | 3-10% | High |
| Vectorization | 2-4× for loops | Very High |

---

## 3. Native ARM64 Codegen

### 3.1 Recent Improvements (Already Implemented)

- ✅ Call-aware register allocation (callee-saved preference)
- ✅ Furthest-end-point spilling
- ✅ Dirty flag optimization (avoid redundant stores)
- ✅ Immediate operand optimization (cmp/add/sub)
- ✅ MIR-level DCE
- ✅ Block reordering (cold paths to end)

### 3.2 Critical Issues

#### Issue CG-1: No MADD (Multiply-Accumulate) Pattern (HIGH)

**Location:** `src/codegen/aarch64/InstrLowering.cpp`

```viper
// Current: 2 instructions
%1 = mul.i64 %a, %b
%2 = add.i64 %1, %c

// Optimal: 1 instruction
MADD dst, a, b, c  // dst = a*b + c
```

**Impact:** 2-5% on arithmetic-heavy code
**Fix:** Add pattern recognition in instruction lowering

#### Issue CG-2: No CSEL (Conditional Select) (HIGH)

**Location:** Missing entirely

```viper
// Current: Branch + 2 moves
if (cond) result = val_true; else result = val_false;

// Optimal: 1 instruction
CSEL result, val_true, val_false, cond
```

**Impact:** 2-8% on branch-heavy code
**Fix:** Add if-conversion pass or pattern in lowering

#### Issue CG-3: Copy Propagation Disabled (MEDIUM)

**Location:** `src/codegen/aarch64/Peephole.cpp:1255`

```cpp
// TODO: Disabled pending investigation of correctness issues
// propagateCopies(instrs, stats);
```

**Impact:** 2-5%
**Fix:** Re-enable and update golden tests

#### Issue CG-4: Missing Addressing Modes (MEDIUM)

ARM64 supports rich addressing modes not currently used:
- `ldr x0, [x1, x2, lsl#3]` (indexed with shift)
- Post-increment: `ldr x0, [x1], #8`

**Impact:** 1-3% for memory-intensive code

### 3.3 Register Allocation Issues

- Cross-block liveness is overly conservative
- FPR allocation doesn't distinguish V8-V15 (callee-saved lower 64 bits)
- Callee-saved pool search is O(n²) in hot cases

---

## 4. Runtime Library

### 4.1 Optimizations Already Implemented

- ✅ String concatenation in-place (when sole owner)
- ✅ Array copy-on-write
- ✅ Pool allocator for small strings (64-512 bytes)
- ✅ Lock-free freelist (atomic CAS)
- ✅ String search with memchr (SIMD)
- ✅ String replace single-pass
- ✅ Unchecked array access API
- ✅ Immortal strings (skip refcount)

### 4.2 Remaining Issues

- String interning only for empty string and literals
- Always-atomic refcounting (no single-threaded fast path)
- Codegen doesn't use unchecked array APIs

---

## 5. Priority Implementation Plan

### Phase 1: Quick Wins (1-2 days)

| Item | File | Change | Expected Gain |
|------|------|--------|---------------|
| Enable cross-block DSE | PassManager.cpp | Add pass to O2 | 2-5% |
| Defer binding allocation | Op_CallRet.cpp | Move to error path | 2-4% |
| Add MADD pattern | InstrLowering.cpp | Pattern match mul+add | 2-5% |

### Phase 2: Medium Effort (3-5 days)

| Item | File | Change | Expected Gain |
|------|------|--------|---------------|
| CSEL pattern | Lowering or new pass | If-conversion | 2-8% |
| Re-enable copy prop | Peephole.cpp | Enable + fix tests | 2-5% |
| Increase inline budget | Inline.cpp | 64 instr, 8 blocks | 5-10% |

### Phase 3: Longer Term

| Item | Effort | Expected Gain |
|------|--------|---------------|
| Tail call optimization | High | 5-10% recursive |
| Loop unswitching | Medium | 3-8% |
| VM fast-path hash | Medium | 2-5% |

---

## 6. Benchmarking Recommendations

### Micro-benchmarks Needed

1. **Arithmetic-heavy:** Test MADD optimization
2. **Branch-heavy:** Test CSEL optimization
3. **Call-heavy:** Test binding allocation fix
4. **Memory-heavy:** Test cross-block DSE
5. **Recursive:** Baseline for tail call (future)

### Existing Benchmark

- `fib(45)`: Good for call overhead, less useful for SIMD/memory

---

## Appendix: File Reference

| Component | Key Files |
|-----------|-----------|
| VM Dispatch | `src/vm/DispatchStrategy.cpp`, `VM.cpp` |
| VM Ops | `src/vm/ops/Op_CallRet.cpp`, `int_ops_*.cpp` |
| IL Passes | `src/il/transform/PassManager.cpp`, `DSE.cpp` |
| Codegen | `src/codegen/aarch64/InstrLowering.cpp` |
| Peephole | `src/codegen/aarch64/Peephole.cpp` |
| RegAlloc | `src/codegen/aarch64/RegAllocLinear.cpp` |
| Runtime | `src/runtime/rt_string_ops.c`, `rt_array.c` |
