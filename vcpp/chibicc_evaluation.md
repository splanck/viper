# Comprehensive Evaluation: chibicc as Bootstrap Foundation for Viper C++ Compiler

## Executive Summary

This report evaluates chibicc and cproc as potential bootstrap foundations for the Viper C++ compiler project, which targets AArch64 exclusively.

---

## 1. chibicc Overview

### Repository & Status
- **Repository:** [https://github.com/rui314/chibicc](https://github.com/rui314/chibicc)
- **Author:** Rui Ueyama (creator of 8cc, original author of LLVM lld linker)
- **Stars:** 10.7k+ stars, 986+ forks
- **License:** MIT License (permissive)
- **Status:** Educational reference implementation, development appears complete

### C Standard Support
- **C11:** Almost all mandatory features + most optional features
- **C17:** Not explicitly mentioned (C17 is largely a bugfix release of C11)
- **GCC Extensions:** Several supported

### Supported Features
- Preprocessor with macro expansion
- Floating-point types (float, double, long double with x87)
- Bit-fields
- Variable-length arrays (VLAs)
- Compound literals
- Thread-local variables
- Atomic variables
- Designated initializers
- Multiple string literal encodings (L, u, U, u8)
- Struct pass-by-value per x86-64 SystemV ABI
- Variadic functions (va_list, va_start, etc.)

### Missing Features
- **Complex numbers** (intentionally excluded)
- **K&R-style function prototypes** (intentionally excluded)
- **GCC-style inline assembly** (NOT SUPPORTED)
- **Digraphs and trigraphs** (intentionally excluded)
- **Optimization passes** (none - emits "terrible code, probably twice or more slower than GCC")

### Architecture Support
- **Original:** x86-64 only (Linux, Ubuntu 20.04 development platform)
- **AArch64 Fork:** [xhackerustc/chibicc-aarch64](https://github.com/xhackerustc/chibicc-aarch64)
  - Created September 2022
  - Maintains same commit history, only codegen ported
  - 6 stars, minimal community engagement
  - Uses QEMU for testing

### Code Size & Complexity
- **Estimated:** ~9,000-10,000 lines of C code
- **Files:** ~14 source files (tokenize.c, preprocess.c, parse.c, codegen.c, type.c, etc.)
- **Language:** 96.9% C, 2.8% shell scripts
- **Design:** Multi-pass compiler (unlike TCC which is single-pass)

### Real-World Validation
Successfully compiles without modifications:
- Git
- SQLite
- libpng
- chibicc itself (self-hosting)

---

## 2. cproc Overview

### Repository & Status
- **Repository:** [https://git.sr.ht/~mcf/cproc](https://git.sr.ht/~mcf/cproc) (GitHub mirror: [michaelforney/cproc](https://github.com/michaelforney/cproc))
- **Author:** Michael Forney
- **License:** ISC License (permissive)
- **Backend:** QBE (separate project)

### C Standard Support
- **C11:** Full implementation
- **C23:** Some features supported
- **GCC Extensions:** Several supported

### Architecture Support (via QBE)
- **x86_64** (Linux musl/gnu, FreeBSD)
- **aarch64** (Linux musl/gnu) - **Native AArch64 support!**
- **riscv64** (Linux gnu)

### Missing Features
- **Variable-length arrays (VLAs)** - Not implemented
- **volatile-qualified types** - Not implemented
- **long double** - Not implemented
- **Inline assembly** - Not implemented
- **Built-in preprocessor** - Requires external preprocessor
- **Position-independent code** - No shared libraries, PIEs

### Code Size
- **cproc:** ~7,000 lines of C99
- **QBE backend:** ~14,000 lines of C99
- **Total:** ~21,000 lines

### Code Quality
- Generates code "about on par with -O2, sometimes -O1" (much better than chibicc)
- QBE aims for "70% of LLVM performance in 10% of the code"

### Real-World Validation
Successfully builds:
- GCC 4.7
- binutils
- mcpp
- cproc itself (self-hosting)

---

## 3. Viper OS Requirements Analysis

The OS codebase has these critical C/C++ compiler requirements:

### Critical Requirements for Viper OS

| Requirement | chibicc | cproc | Notes |
|-------------|---------|-------|-------|
| **AArch64 target** | Fork only | Native | Viper targets AArch64 exclusively |
| **Inline assembly** | NO | NO | Used heavily in syscall.hpp, crt0.c |
| **GCC register variables** | Partial | Partial | `register x8 asm("x8")` syntax |
| **__attribute__((packed))** | Unknown | Unknown | Used in ViperFS tools |
| **__builtin_* functions** | NO | Unknown | Used in libc (clz, ctz, popcount, bswap, etc.) |
| **Variadic functions** | YES | YES | Used in stdio, syslog |
| **C11 features** | YES | YES | |
| **Self-hosting** | YES | YES | Both can compile themselves |

### Inline Assembly Usage in Viper OS

The Viper OS uses inline assembly extensively for:
1. **System calls** (`os/user/syscall.hpp`, `os/kernel/include/syscall.hpp`)
2. **CRT0** (`os/user/libc/src/crt0.c`)
3. **Exception handling** (`os/kernel/arch/aarch64/exceptions.cpp`)
4. **CPU operations** (`os/kernel/arch/aarch64/cpu.cpp`)

**This is a critical blocker for both chibicc and cproc.**

---

## 4. Comparison: chibicc vs cproc

| Aspect | chibicc | cproc | Winner |
|--------|---------|-------|--------|
| **AArch64 Support** | Fork (uncertain quality) | Native via QBE | cproc |
| **Code Quality** | No optimization | ~O1-O2 level | cproc |
| **Code Size** | ~10,000 LOC | ~21,000 LOC (with QBE) | chibicc |
| **Readability** | Excellent (educational) | Good | chibicc |
| **Self-hosting** | Yes | Yes | Tie |
| **Inline Assembly** | No | No | Neither |
| **Preprocessor** | Built-in | External required | chibicc |
| **VLAs** | Yes | No | chibicc |
| **Community** | Larger (10.7k stars) | Smaller | chibicc |
| **Active Development** | Appears complete | Ongoing | cproc |
| **License** | MIT | ISC | Both permissive |

---

## 5. Evaluation for Viper C++ Compiler Project

### Option A: Use chibicc as Foundation

**Pros:**
- Extremely readable, educational codebase
- Smaller codebase (~10k LOC) easier to understand and modify
- Well-documented with incremental commit history
- Built-in preprocessor
- MIT license very permissive
- Good C11 support

**Cons:**
- **No inline assembly support** (critical for Viper OS)
- **AArch64 support only via third-party fork** (low community engagement)
- No optimization (2x+ slower than GCC)
- No builtin function support (__builtin_clz, etc.)
- Would need significant work to add:
  - AArch64 codegen (or adopt fork)
  - Inline assembly
  - __builtin_* functions
  - __attribute__ extensions
  - C++ support (massive undertaking)

### Option B: Use cproc as Foundation

**Pros:**
- **Native AArch64 support** via QBE
- Better code quality (O1-O2 level optimization)
- Separable backend (QBE) allows focusing on frontend
- Has built GCC 4.7 (proof of serious capability)

**Cons:**
- **No inline assembly support** (critical for Viper OS)
- Larger combined codebase (~21k LOC)
- Requires external preprocessor
- No VLA support
- Two-project dependency (cproc + QBE)
- Would still need significant work for C++

### Option C: Build from Scratch

**Pros:**
- Complete control over architecture
- Can design for AArch64 from day one
- Can include inline assembly from the start
- Can design C++ support into the architecture
- No legacy constraints

**Cons:**
- Significantly more initial effort
- No proven real-world validation
- Higher risk of bugs
- Longer time to first working compiler

---

## 6. Recommendation

### For Viper C++ Compiler Bootstrap: **Hybrid Approach**

Given that both chibicc and cproc lack inline assembly support (critical for Viper OS), I recommend a **hybrid approach**:

#### Phase 1: Learn from chibicc
- Study chibicc's architecture and incremental design
- Use it as a reference implementation for C frontend
- Its educational structure makes it ideal for understanding compiler construction

#### Phase 2: Use QBE as Backend
- QBE provides native AArch64 support with reasonable optimization
- At ~14,000 lines, it's manageable
- Already proven to work with cproc

#### Phase 3: Build Custom Frontend
Based on learnings from chibicc, build a custom frontend that:
1. Targets QBE's intermediate language
2. Includes inline assembly support from day one
3. Supports __builtin_* functions needed by Viper OS
4. Is designed with C++ extension points in mind

#### Phase 4: Add C++ Features Incrementally
Following chibicc's incremental approach, add C++ features:
- Classes and member functions
- Constructors/destructors
- Templates (subset)
- RAII
- Namespaces

### Alternative: Modify chibicc-aarch64

If faster bootstrap is priority:
1. Fork chibicc-aarch64
2. Add inline assembly support (significant effort)
3. Add required __builtin_* functions
4. Add required __attribute__ extensions
5. This gets you a working C compiler for Viper OS faster
6. Then extend toward C++

### Decision Matrix

| If your priority is... | Choose... | Because... |
|------------------------|-----------|------------|
| Fastest to working C compiler | Extend chibicc-aarch64 | Smallest codebase (~10k LOC) |
| Best code quality | cproc + QBE | O1-O2 level output |
| Best C++ foundation | Custom frontend + QBE | Design for C++ from start |
| Maximum learning | Study chibicc, build custom | Educational value |

**Recommended for Viper:** Custom frontend with custom backend (see QBE limitation below)

### QBE Inline Assembly Limitation (Critical)

**QBE does not support inline assembly.** From QBE documentation:

> QBE does not have inline assembly. Assembly must be in separate `.s` files.

This is a significant limitation for Viper OS, which uses inline assembly extensively. If you use QBE as a backend, you must either:

1. **Extract inline asm to separate functions** — Call out to hand-written `.s` files
2. **Post-process QBE output** — Inject inline asm after QBE generates code
3. **Extend QBE with inline asm support** — Significant work (~2-4 weeks)
4. **Use a different backend** — Custom or LLVM

Given this limitation, the recommendation changes:

| Backend Option | Inline Asm Support | Optimization | Complexity |
|----------------|-------------------|--------------|------------|
| QBE (unmodified) | ❌ No | O1-O2 | Low |
| QBE (extended) | ⚠️ Would need work | O1-O2 | Medium |
| Custom backend | ✅ Full control | O0-O1 | High |
| LLVM IR emission | ✅ Full support | O3 | Medium (large dep) |

**Updated Recommendation:** If inline assembly is critical path (it is for Viper), either:
- Build a custom backend with inline asm from the start, OR
- Use LLVM IR emission (best optimization, full inline asm support)

### Inline Assembly Implementation Estimate

| Subtask | Effort | Complexity |
|---------|--------|------------|
| Parser for `asm()` statements | 1-2 weeks | Medium |
| Constraint letter mapping | 1 week | Low |
| Register allocation integration | 2-4 weeks | **High** |
| AArch64 instruction passthrough | 1-2 weeks | Medium |
| **Total** | **5-9 weeks** | |

The register allocation integration is the hardest part — the compiler must coordinate virtual register assignment with explicit register constraints.

---

## 7. Critical Path Items

Regardless of approach, these features MUST be implemented for Viper OS:

1. **Inline Assembly** - Required for syscalls, exception handling
   - GCC-style: `asm("instruction" : outputs : inputs : clobbers)`
   - Register variables: `register int x asm("x0")`

2. **__builtin_* Functions** (from Viper libc):
   - `__builtin_clz`, `__builtin_ctz`, `__builtin_popcount`
   - `__builtin_bswap16/32/64`
   - `__builtin_huge_val`, `__builtin_nan`, `__builtin_inf`
   - `__builtin_trap`
   - `__builtin_offsetof`
   - `__builtin_memcpy`

3. **__attribute__ Extensions**:
   - `__attribute__((packed))`
   - `__attribute__((noreturn))`

4. **AArch64 ABI Compliance**:
   - AAPCS64 calling convention
   - Stack alignment
   - Register usage

---

## 8. Estimated Effort

| Approach | C Compiler | C++ Extension | Total |
|----------|------------|---------------|-------|
| Extend chibicc-aarch64 | 3-6 months | 12-18 months | 15-24 months |
| Use cproc + extend | 2-4 months | 12-18 months | 14-22 months |
| Custom frontend + QBE | 6-9 months | 12-18 months | 18-27 months |
| Fully from scratch | 12-18 months | 18-24 months | 30-42 months |

---

## Sources

- [GitHub - rui314/chibicc: A small C compiler](https://github.com/rui314/chibicc)
- [GitHub - xhackerustc/chibicc-aarch64: A fork of chibicc, ported to aarch64](https://github.com/xhackerustc/chibicc-aarch64)
- [GitHub - michaelforney/cproc: C11 compiler (mirror)](https://github.com/michaelforney/cproc)
- [~mcf/cproc - C11 compiler - sourcehut git](https://git.sr.ht/~mcf/cproc)
- [QBE - Compiler Backend](https://c9x.me/compile/)
- [FOSDEM 2022 - Introduction to QBE](https://archive.fosdem.org/2022/schedule/event/lg_qbe/)
- [OpenBSD has two new C compilers: chibicc and kefir](https://briancallahan.net/blog/20220629.html)
- [Lobsters - chibicc can bootstrap cproc](https://lobste.rs/s/fcbwvi/chibicc_can_bootstrap_cproc)

---

*Evaluation completed: 2026-01-01*
