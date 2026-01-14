# Viper Performance Deep Dive Analysis

**Date:** 2025-01-13
**Revision:** Comprehensive investigation with code-level analysis
**Scope:** VM execution, Native codegen, IL optimization, Runtime overhead
**Benchmark:** Recursive fibonacci fib(45)

## Update (Jan 2026)

### Optimizations Implemented

1. **Immediate operand optimization** - COMPLETED
   - Add/sub instructions now use immediate form when RHS is a small constant (0-4095)
   - Eliminates unnecessary `mov x, #imm` instructions before arithmetic
   - Example: `sub x15, x13, #1` instead of `mov x14, #1; sub x15, x13, x14`

2. **Comparison immediate optimization** - COMPLETED
   - Comparisons against small constants use `cmp x, #imm` instead of `cmp x, y`
   - Reduces register pressure and instruction count
   - Example: `cmp x8, #1` instead of `mov x10, #1; cmp x8, x10`

3. **MIR-level DCE** - COMPLETED
   - Dead code elimination in peephole pass removes unused instructions
   - Removes dead `cset` instructions, unused `mov` to non-argument registers
   - Conservative about loads, argument registers, and address computations
   - Successfully removes instructions like `cset x11, le` when unused

4. **Call-aware register allocation** - COMPLETED
   - Values that survive function calls are now preferentially allocated to callee-saved registers
   - Avoids unnecessary spilling around calls
   - The key insight: check if vreg's LAST use (not first) is after a call
   - Implementation: `nextUseAfterCall()` checks if there's a call between current position and latest use

### Current Performance
- Benchmark: fib(45) = **7.50s** (20% improvement from 9.35s baseline)
- Call-aware allocation reduced callee-saved register saves from 5 to 4
- Key values like `n` now survive both recursive calls without spilling

### Generated Code Improvements
Before optimizations:
```asm
mov x10, #1
cmp x8, x10
cset x11, le     ; x11 never used
cmp x0, #1
```

After optimizations:
```asm
cmp x8, #1       ; immediate form
cmp x0, #1       ; cset removed by DCE
```

### Next Steps for Further Optimization
1. **Live range splitting** - Split long-lived values to reduce register pressure
2. **Cross-block liveness for better DCE** - Currently DCE is conservative about cross-block values
3. **Rematerialization** - Regenerate constants instead of spilling them
4. **Global register allocation** - Function-wide allocation instead of per-block

---

---

## Executive Summary

Viper native code is **3.7x slower than C -O2** and **1.8x slower than C -O0** (improved from 4.9x and 2.4x). The VM is **~750x slower than native**. This analysis identifies the root causes and provides prioritized recommendations.

### Performance Hierarchy (fib(45))

| Implementation | Time | vs C -O2 |
|----------------|------|----------|
| C -O2 | 2.02s | 1.0x |
| C -O0 | 4.11s | 2.0x |
| **Viper Native** | **7.50s** | **3.7x** |
| Python | 83.08s | 41x |
| Viper VM | ~1500s* | ~750x |

*Estimated from fib(35) scaling

---

## Part 1: Native Codegen Issues (PRIMARY BOTTLENECK)

### 1.1 Generated Code Quality Analysis

Comparing Viper's fib() output to Clang -O2:

| Metric | Viper | Clang -O2 | Impact |
|--------|-------|-----------|--------|
| Stack frame | 112 bytes | 32 bytes | 3.5x more stack pressure |
| Callee-saved regs | 5 (x19-x23) | 2 (x19-x20) | 2.5x more saves/restores |
| Stack stores in hot path | 10 | 0 | Major latency |
| Redundant instructions | ~8 | 0 | Wasted cycles |

### 1.2 Specific Code Issues

**Viper's fib() prologue/epilogue (25 instructions):**
```asm
_fib:
  stp x29, x30, [sp, #-16]!      ; Save FP/LR
  mov x29, sp
  sub sp, sp, #64                 ; 64-byte frame!
  stp x19, x20, [sp, #-16]!      ; Save 5 callee-saved
  stp x21, x22, [sp, #-16]!
  str x23, [sp, #-16]!
  ; ... function body ...
  ldr x23, [sp], #16              ; Restore all 5
  ldp x21, x22, [sp], #16
  ldp x19, x20, [sp], #16
  add sp, sp, #64
  ldp x29, x30, [sp], #16
  ret
```

**Clang -O2 fib() prologue/epilogue (8 instructions):**
```asm
_fib:
  stp x20, x19, [sp, #-32]!       ; Save only 2 callee-saved
  stp x29, x30, [sp, #16]
  add x29, sp, #16
  ; ... function body (converted to loop!) ...
  ldp x29, x30, [sp, #16]
  ldp x20, x19, [sp], #32
  ret
```

### 1.3 Critical Issues Identified

#### Issue 1: Excessive Register Spilling
```asm
; Viper stores EVERY intermediate to stack:
entry_0_fib:
  str x0, [x29, #-24]           ; Store argument
  ldr x8, [x29, #-24]           ; Load it back immediately!
  str x8, [x29, #-8]            ; Store again
  str x8, [x29, #-16]           ; And again!
```
**Cost:** 4 memory operations where 0 are needed = **~20 cycles wasted**

#### Issue 2: Dead Code Not Eliminated
```asm
  mov x10, #1
  cmp x8, x10
  cset x11, le                  ; x11 IS NEVER USED!
  cmp x0, #1                    ; Redundant comparison
```
**Cost:** 3 instructions doing nothing = **~5 cycles wasted**

#### Issue 3: No Immediate Operand Optimization
```asm
  mov x14, #1                   ; Load 1 into register
  sub x15, x13, x14             ; Then subtract
```
Should be:
```asm
  sub x15, x13, #1              ; Direct immediate
```
**Cost:** 1 extra instruction per subtraction = **~2 cycles per call**

#### Issue 4: Callee-Saved Register Overuse
The function saves x19-x23 but uses them as scratch registers that don't survive calls. Should use caller-saved registers (x9-x15) instead.

**Cost:** 10 extra save/restore instructions = **~15 cycles per call**

#### Issue 5: No Tail-Call Optimization
The second recursive call `return fib(n-1) + fib(n-2)` could use tail-call for fib(n-2), but doesn't.

**Cost:** Full frame setup/teardown for every call

#### Issue 6: No Loop Conversion
Clang converts one recursive branch to a loop (LBB0_3). Viper does naive double recursion.

**Potential savings:** 30-40% for this specific pattern

---

## Part 2: Register Allocator Analysis

### 2.1 Current Implementation

**Algorithm:** Linear-scan with furthest-end-point victim selection
**Location:** `src/codegen/aarch64/RegAllocLinear.cpp` (1,113 lines)

**Characteristics:**
- Per-basic-block allocation (resets at block boundaries)
- Cross-block values always spilled
- Dirty tracking reduces redundant stores (good)
- Furthest-end-point heuristic for spill selection (good)

### 2.2 Problems

1. **Per-block scope is too narrow**
   - Values crossing block boundaries are always spilled
   - No global live range analysis

2. **No live range splitting**
   - Long-lived values hog registers for entire function
   - Can't split a value's lifetime to reduce pressure

3. **Caller-saved registers underutilized**
   - Allocator prefers callee-saved registers
   - Results in excessive prologue/epilogue overhead

4. **No rematerialization**
   - Constants are spilled instead of recomputed
   - `mov x14, #1` should never be spilled - just regenerate it

### 2.3 Recommendations

| Priority | Fix | Expected Gain |
|----------|-----|---------------|
| **P0** | Use caller-saved regs for call-local values | 15-20% |
| **P0** | Eliminate dead cset/redundant cmp | 5-10% |
| **P1** | Implement rematerialization for constants | 10-15% |
| **P1** | Global register allocation (function-wide) | 20-30% |
| **P2** | Live range splitting | 10-15% |

---

## Part 3: IL Optimization Analysis

### 3.1 Current Pipeline (O2)

The IL optimizer has 19 passes including:
- ✅ SCCP (constant propagation)
- ✅ DCE (dead code elimination)
- ✅ Inlining (with cost model)
- ✅ LICM (loop-invariant code motion)
- ✅ GVN (global value numbering)
- ✅ Loop unrolling
- ✅ Strength reduction (indvars)

### 3.2 Critical Missing Optimizations

| Optimization | Impact | Effort |
|--------------|--------|--------|
| **Tail-call optimization** | 20-50% for recursive code | High (new IL opcode) |
| **Copy propagation (enabled)** | 5-10% | Low (already implemented, disabled) |
| **Better inlining thresholds** | 10-20% | Medium |

### 3.3 IL-Level Observations

The IL for fib() looks reasonable:
```
func @fib(i64 %n) -> i64 {
entry(%n:i64):
  %cmp = scmp_le %n, 1
  cbr %cmp, base(%n), recurse(%n)
base(%n1:i64):
  ret %n1
recurse(%n2:i64):
  %nm1 = isub.ovf %n2, 1
  %r1 = call @fib(%nm1)
  %nm2 = isub.ovf %n2, 2
  %r2 = call @fib(%nm2)
  %sum = iadd.ovf %r1, %r2
  ret %sum
}
```

**The problem is not at IL level - it's in codegen lowering.**

---

## Part 4: VM Performance Analysis

### 4.1 Architecture

- **Dispatch:** Computed goto (threaded) - optimal
- **Architecture:** Register-based with SSA values
- **Call overhead:** SmallVector<8> for args - good

### 4.2 Why VM is 750x Slower

1. **Interpretation overhead:** ~10-20 cycles per IL instruction
2. **No JIT:** Every instruction interpreted every time
3. **Function call overhead:** Full frame setup per call

### 4.3 Recommendations

| Priority | Fix | Expected Gain |
|----------|-----|---------------|
| **P2** | Baseline JIT for hot loops | 10-100x |
| **P3** | Inline caching for calls | 20-30% |
| **P3** | Trace compilation | 50-200x |

The VM is fundamentally limited without JIT. For interpreted execution, it's already well-optimized.

---

## Part 5: Runtime Overhead Analysis

### 5.1 Per-Call Overhead

| Operation | Cost |
|-----------|------|
| rt_get_current_context() | 1-3 cycles |
| rt_set_current_context() | 3-5 cycles (common case) |
| String ref/unref | 3-15 cycles (atomic) |
| Pool allocation | 20-50 cycles |

**Runtime overhead is minimal** - not a significant factor.

### 5.2 Memory Management

- Pool allocator for small strings (lock-free CAS)
- Reference counting with dirty tracking
- Small-string optimization (SSO) for ≤63 bytes

**This is well-optimized.**

---

## Part 6: Prioritized Recommendations

### Tier 1: Quick Wins (1-2 days each, 30-50% total gain)

1. **Fix register allocation to prefer caller-saved registers**
   - File: `RegAllocLinear.cpp`
   - Change: Modify `selectFurthestVictim()` to avoid callee-saved for call-local values
   - Expected: 15-20% improvement

2. **Eliminate dead code at MIR level**
   - File: `Peephole.cpp`
   - Add: Remove unused `cset` instructions, redundant comparisons
   - Expected: 5-10% improvement

3. **Use immediate operands for small constants**
   - File: `InstrLowering.cpp`
   - Change: Emit `sub x, y, #imm` instead of `mov tmp, #imm; sub x, y, tmp`
   - Expected: 5-10% improvement

4. **Enable copy propagation**
   - Already implemented but disabled
   - Expected: 2-5% improvement

### Tier 2: Medium Effort (1-2 weeks each, 20-40% additional)

5. **Implement rematerialization**
   - Don't spill constants - regenerate them
   - Expected: 10-15% improvement

6. **Global register allocation**
   - Replace per-block allocation with function-wide
   - Expected: 20-30% improvement

7. **Tail-call optimization**
   - New IL opcode + codegen support
   - Expected: 20-50% for recursive code

### Tier 3: Major Investment (1+ months)

8. **Baseline JIT for VM**
   - Compile hot loops to native code
   - Expected: 10-100x VM improvement

9. **Advanced instruction selection**
   - Pattern matching for fused operations (madd, msub)
   - Better addressing modes
   - Expected: 10-20% native improvement

---

## Part 7: Root Cause Summary

The **primary bottleneck** is the native codegen, specifically:

1. **Register allocator is too conservative** - spills everything crossing blocks
2. **Dead code not eliminated at MIR level** - IL DCE runs but codegen adds dead code
3. **No immediate operand usage** - loads constants into registers unnecessarily
4. **Callee-saved register overuse** - causes expensive prologue/epilogue

The IL optimizer and runtime are **not the problem**. The VM is slow but that's inherent to interpretation.

---

## Appendix: Benchmark Comparison

### fib() Instruction Counts

| Implementation | Instructions in hot path |
|----------------|-------------------------|
| C -O2 | 14 |
| C -O0 | 28 |
| **Viper** | **52** |

### Stack Memory per Call

| Implementation | Bytes |
|----------------|-------|
| C -O2 | 32 |
| C -O0 | 48 |
| **Viper** | **112** |

---

## Conclusion

Viper native performance can realistically reach **1.5-2x of C -O0** (currently 2.4x slower) with Tier 1 fixes. Reaching C -O2 parity would require Tier 2-3 investments.

**Recommended immediate action:** Start with register allocator fixes (Tier 1, items 1-3) for fastest ROI.
