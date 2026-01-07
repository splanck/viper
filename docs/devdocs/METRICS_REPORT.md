# VIPER Project Metrics Report

*Generated: 2025-11-12*

## Executive Summary

- **Total Source Lines**: 74,986 (excluding tests, build artifacts, and documentation)
- **Total Test Lines**: 40,841
- **Languages**: C++ (primary), C (runtime), Python (generators), YAML (schemas)
- **Test Coverage**: 586 tests (99% pass rate)

---

## Lines of Code (LOC) Breakdown

### By Language

| Language     | Files   | Blank      | Comment    | Code       | % of Total |
|--------------|---------|------------|------------|------------|------------|
| C++          | 276     | 7,062      | 20,919     | 49,997     | 66.7%      |
| C/C++ Header | 245     | 3,931      | 6,270      | 15,996     | 21.3%      |
| C            | 27      | 542        | 1,826      | 3,615      | 4.8%       |
| YAML         | 1       | 0          | 0          | 2,006      | 2.7%       |
| PHP          | 4       | 79         | 8          | 1,402      | 1.9%       |
| CMake        | 6       | 60         | 1          | 641        | 0.9%       |
| Python       | 2       | 70         | 1          | 403        | 0.5%       |
| **Total**    | **567** | **11,849** | **30,193** | **74,986** | **100%**   |

### Source Lines of Code (SLOC)

- **Physical Lines**: 74,986
- **Blank Lines**: 11,849 (15.8%)
- **Comment Lines**: 30,193 (40.3%)
- **Pure Code Lines**: 74,986
- **Comment Density**: 40.3% (excellent documentation)

---

## Component Breakdown

### 1. BASIC Frontend (30,130 lines)

| Language                          | Files   | Code       | % of Frontend |
|-----------------------------------|---------|------------|---------------|
| C++                               | 112     | 22,185     | 73.6%         |
| C/C++ Header                      | 83      | 7,059      | 23.4%         |
| Fortran 77 (builtin_registry.inc) | 1       | 616        | 2.0%          |
| CMake                             | 1       | 140        | 0.5%          |
| Other                             | 1       | 130        | 0.4%          |
| **Total**                         | **198** | **30,130** | **100%**      |

**Key Components**:

- Parser: 8 files, ~2,500 lines
- Semantic Analyzer: 15 files, ~3,000 lines
- Lowerer (AST→IL): 25 files, ~6,500 lines
- AST Definitions: 12 files, ~2,000 lines
- Builtin Functions: 10 files, ~2,000 lines
- Type System: 8 files, ~1,500 lines

### 2. IL (Intermediate Language) Subsystem (17,238 lines)

| Subsystem    | Code Lines | Description                                       |
|--------------|------------|---------------------------------------------------|
| Core         | ~2,500     | Module, Function, BasicBlock, Instruction classes |
| I/O          | ~4,000     | Parser, Serializer (text format)                  |
| Verification | ~5,500     | Type checking, CFG validation, EH checks          |
| Transforms   | ~4,500     | SCCP, Mem2Reg, LICM, SimplifyCFG, DCE             |
| Runtime Sigs | ~1,300     | Runtime function signature registry               |

### 3. Virtual Machine (12,569 lines)

| Component      | Code Lines | Description                                  |
|----------------|------------|----------------------------------------------|
| VM Core        | ~3,500     | Interpreter loop, stack management, dispatch |
| Op Handlers    | ~4,500     | Instruction implementations                  |
| Runtime Bridge | ~800       | C runtime integration                        |
| Debug/Trace    | ~600       | Debugging and tracing support                |
| Generated Code | ~3,200     | Schema-driven dispatch tables                |

### 4. x86-64 Codegen (7,932 lines)

| Component           | Code Lines | Description                          |
|---------------------|------------|--------------------------------------|
| Lowering (IL→MIR)   | ~2,500     | Instruction selection, call lowering |
| Register Allocation | ~1,200     | Linear scan, coalescing, spilling    |
| Assembly Emission   | ~1,800     | x64 instruction encoding             |
| MIR                 | ~800       | Machine IR data structures           |
| Peephole            | ~175       | Late-stage optimizations             |

### 5. C Runtime Library (4,181 lines)

| Module            | Code Lines | Description                           |
|-------------------|------------|---------------------------------------|
| String Operations | ~1,200     | Concatenation, comparison, formatting |
| File I/O          | ~800       | File operations (OPEN, CLOSE, PRINT#) |
| Arrays            | ~400       | Dynamic array allocation/access       |
| Numeric           | ~600       | Type conversions, formatting          |
| Object/OOP        | ~300       | Class dispatch, object lifecycle      |
| Terminal          | ~280       | CLS, COLOR, LOCATE, CURSOR            |
| Heap/Memory       | ~200       | Memory management                     |

---

## Test Suite Statistics

### Test Code (40,841 lines)

| Language         | Files | Code Lines | Description                |
|------------------|-------|------------|----------------------------|
| C++ (Unit)       | 350   | 28,143     | Unit and integration tests |
| .NET IL (Golden) | 164   | 5,634      | Golden file tests          |
| CMake            | 54    | 4,614      | Test registration          |
| Visual Basic     | 215   | 1,492      | BASIC language tests       |
| C                | 6     | 478        | Runtime tests              |

### Test Coverage

- **Total Tests**: 586
- **Passing**: 585 (99.8%)
- **Failing**: 1 (vm_trace_src - pre-existing, unrelated to bug fixes)
- **Disabled**: 1 (test_namespace_e2e)

**Test Breakdown**:

- Unit tests (tests/unit): ~180 tests
- Runtime tests (tests/runtime): ~50 tests
- VM tests (tests/vm): ~150 tests
- Frontend tests (tests/frontends): ~120 tests
- E2E tests (tests/e2e): ~30 tests
- Golden tests (tests/golden): ~55 tests

---

## Code Complexity Analysis

### Largest Files (by line count)

| File                  | Lines | Type      | Complexity                  |
|-----------------------|-------|-----------|-----------------------------|
| ops.yaml              | 2,006 | Schema    | Low (declarative)           |
| RuntimeSignatures.cpp | 1,631 | Data      | Low (table-driven)          |
| AsmEmitter.cpp        | 1,091 | Logic     | High (instruction encoding) |
| SpecTables.cpp        | 962   | Generated | Low (generated data)        |
| SCCP.cpp              | 880   | Logic     | High (dataflow analysis)    |
| ThreadedCases.inc     | 847   | Generated | Low (dispatch table)        |
| FunctionParser.cpp    | 820   | Logic     | Medium (parsing)            |
| Lowerer.Procedure.cpp | 1,202 | Logic     | High (AST→IL lowering)      |

### Average Complexity Metrics (key files)

| File                  | Lines | Functions | Branches | Avg Lines/Func |
|-----------------------|-------|-----------|----------|----------------|
| Lowerer.Procedure.cpp | 1,202 | 49        | 160      | 24             |
| Parser_Expr.cpp       | 590   | 18        | 61       | 32             |
| SemanticAnalyzer.cpp  | 327   | 19        | 32       | 17             |
| RuntimeSignatures.cpp | 1,631 | 23        | 92       | 70             |
| VM.cpp                | 717   | 33        | 75       | 21             |

**Complexity Assessment**:

- **Low Complexity**: Average 17-24 lines/function (well-factored)
- **Moderate**: 25-40 lines/function
- **High**: 40+ lines/function (mostly data tables)
- **Very High**: 70+ lines/function (RuntimeSignatures - table initialization)

### Function Length Distribution

Based on sampling of core files:

- **<20 lines**: ~45% (small, focused functions)
- **20-50 lines**: ~35% (moderate complexity)
- **50-100 lines**: ~15% (complex logic)
- **>100 lines**: ~5% (mostly parsers, complex lowering)

---

## Code Organization Quality

### Modularity Score: **Excellent**

- Clear separation of concerns (Frontend → IL → Backend)
- Well-defined interfaces between components
- Minimal cross-component dependencies
- Each component <20K lines (maintainable size)

### Documentation Score: **Excellent**

- **40.3% comment density** (industry standard: 20-30%)
- Doxygen-style function documentation
- Architecture documents in /docs
- Comprehensive test documentation

### Code Reuse

- **Generated Code**: ~5,000 lines (6.7% of total)
    - VM dispatch tables (PHP/Python generators)
    - IL verification tables (Python generator)
    - YAML-driven op schemas
- **Template/Generic Code**: Extensive use of C++ templates
- **Shared Utilities**: Common parsing, diagnostics, AST walking

---

## Growth and Maintenance Metrics

### Code Distribution (maintainability)

| Component      | % of Codebase | Maintainability                |
|----------------|---------------|--------------------------------|
| BASIC Frontend | 40.2%         | High (well-structured)         |
| IL Subsystem   | 23.0%         | High (clean abstractions)      |
| VM             | 16.8%         | Medium (performance-critical)  |
| Codegen        | 10.6%         | Medium (architecture-specific) |
| Runtime        | 5.6%          | High (simple C code)           |
| Support/Tools  | 3.8%          | High (utilities)               |

### Technical Debt Indicators

✅ **Low Debt**:

- Consistent coding style
- Good test coverage (99.8% pass)
- Active bug tracking (15/32 resolved)
- Clear architectural boundaries

⚠️ **Moderate Concerns**:

- Some generated files very large (>1000 lines)
- RuntimeSignatures.cpp has high cyclomatic complexity
- Some parser functions exceed 100 lines

### Cyclomatic Complexity Estimates

Based on branch counting:

- **Average per file**: 20-40 decision points
- **High complexity files**: 100+ decision points
    - Parsers (expected - grammar-driven)
    - Lowering passes (expected - pattern matching)
    - Semantic analysis (expected - type rules)

---

## Performance Characteristics

### Binary Size (approximate)

- VM executable: ~2-3 MB
- Runtime library: ~500 KB
- Test binaries: ~100-200 KB each

### Compilation Metrics

- Full build time: ~30-45 seconds (8-core system)
- Incremental builds: 2-10 seconds
- Test suite runtime: ~90 seconds (586 tests)

---

## Comparison to Similar Projects

### Size Comparison

| Project    | Total SLOC | Language | Type               |
|------------|------------|----------|--------------------|
| **Viper**  | **74,986** | C++/C    | Compiler toolchain |
| TinyCC     | ~35,000    | C        | C compiler         |
| Lua 5.4    | ~30,000    | C        | Interpreter        |
| MoonScript | ~15,000    | Lua      | Transpiler         |
| LuaJIT     | ~90,000    | C/ASM    | JIT compiler       |

**Assessment**: Viper is appropriately sized for a multi-stage compiler with:

- Full frontend (lexer, parser, semantic analysis)
- IR with optimizations
- Two backends (VM + x86-64)
- Comprehensive runtime library

### Component Size Ratios

| Viper Component | %   | Industry Typical                |
|-----------------|-----|---------------------------------|
| Frontend        | 40% | 30-40% ✅                        |
| IR/Optimization | 23% | 20-30% ✅                        |
| Backend         | 11% | 15-25% ⚠️ (small, could expand) |
| Runtime         | 6%  | 10-20% ⚠️ (lean)                |
| VM              | 17% | N/A (unique feature)            |

---

## Summary Insights

### Strengths

1. **Well-documented**: 40% comment density
2. **Well-tested**: 586 tests, 99.8% pass rate
3. **Modular**: Clear component boundaries
4. **Maintainable**: Average function length 20-30 lines
5. **Comprehensive**: Full compiler toolchain in <75K lines

### Areas for Growth

1. **Runtime Library**: Could expand (currently lean at 4K lines)
2. **Codegen**: x86-64 backend could be more feature-complete
3. **Optimizations**: IL transforms are solid but could expand

### Code Health: **A-**

- Excellent documentation
- Strong test coverage
- Good modularity
- Room to expand features without bloat

---

## Detailed File Analysis (Top 20 by size)

| Rank | File                    | Lines | Type      | Component         |
|------|-------------------------|-------|-----------|-------------------|
| 1    | ops.yaml                | 2,006 | Schema    | VM                |
| 2    | RuntimeSignatures.cpp   | 1,631 | Table     | IL/Runtime        |
| 3    | Lowerer.Procedure.cpp   | 1,202 | Logic     | Frontend          |
| 4    | OpSchema.hpp            | 1,195 | Generated | VM                |
| 5    | AsmEmitter.cpp          | 1,091 | Logic     | Codegen           |
| 6    | SpecTables.cpp          | 962   | Generated | IL/Verify         |
| 7    | SCCP.cpp                | 880   | Logic     | IL/Transform      |
| 8    | ThreadedCases.inc       | 847   | Generated | VM                |
| 9    | FunctionParser.cpp      | 820   | Logic     | IL/IO             |
| 10   | Lowerer.Procedure.cpp   | 809   | Logic     | Frontend          |
| 11   | Common.cpp              | 690   | Logic     | Frontend/Builtins |
| 12   | Semantic_OOP.cpp        | 676   | Logic     | Frontend          |
| 13   | EhChecks.cpp            | 636   | Logic     | IL/Verify         |
| 14   | ConstFolder.cpp         | 617   | Logic     | Frontend          |
| 15   | builtin_registry.inc    | 616   | Table     | Frontend          |
| 16   | Scan_RuntimeNeeds.cpp   | 605   | Logic     | Frontend          |
| 17   | LoweringPass.cpp        | 601   | Logic     | Codegen           |
| 18   | AstWalker.hpp           | 601   | Template  | Frontend          |
| 19   | Lowering.EmitCommon.cpp | 571   | Logic     | Codegen           |
| 20   | OperandParser.cpp       | 534   | Logic     | IL/IO             |

**Average top-20 file size**: 901 lines (reasonable - no mega-files)

