# Viper Project - Lines of Code Report

**Generated:** November 25, 2025

---

## Executive Summary

| Metric                 | Value                                          |
|------------------------|------------------------------------------------|
| **Total Source Lines** | 215,726                                        |
| **Total Files**        | 2,469                                          |
| **Primary Languages**  | C++ (49%), Markdown (14%), BASIC (14%), C (4%) |
| **Test Coverage**      | 49,395 SLOC (23% of codebase)                  |

---

## By Language

| Language     |     Files |      Blank |    Comment | Code (SLOC) | % of Total |
|--------------|----------:|-----------:|-----------:|------------:|-----------:|
| C++          |       829 |     15,296 |     32,975 |     104,595 |      48.5% |
| Markdown     |       112 |      9,052 |          3 |      29,529 |      13.7% |
| Visual Basic |       801 |      5,152 |        887 |      29,435 |      13.6% |
| C/C++ Header |       348 |      5,481 |     13,921 |      20,479 |       9.5% |
| C            |        54 |      1,486 |      3,723 |       7,844 |       3.6% |
| CMake        |        77 |      1,125 |        468 |       6,990 |       3.2% |
| Viper IL     |       185 |        182 |          7 |       6,279 |       2.9% |
| YAML         |         6 |         46 |         24 |       2,522 |       1.2% |
| Other        |        57 |        863 |      1,657 |       8,053 |       3.7% |
| **TOTAL**    | **2,469** | **38,683** | **53,665** | **215,726** |   **100%** |

---

## By Subsystem

### Core IL Layer

| Component    |   Files | Code (SLOC) | Description                              |
|--------------|--------:|------------:|------------------------------------------|
| IL Core      |      23 |         934 | IR types, opcodes, modules, functions    |
| IL Build     |       2 |         288 | IR builder for programmatic construction |
| IL I/O       |      20 |       3,453 | Text parser/serializer                   |
| IL Verify    |      48 |       5,114 | Verifier (types, control flow, EH)       |
| IL Transform |      45 |       5,340 | Optimization passes                      |
| IL Analysis  |       5 |         496 | CFG, dominators, alias analysis          |
| IL Runtime   |      17 |       3,342 | Runtime signature metadata               |
| **IL Total** | **160** |  **18,967** |                                          |

### Execution Layer

| Component           |   Files | Code (SLOC) | Description                           |
|---------------------|--------:|------------:|---------------------------------------|
| Virtual Machine     |      58 |      12,867 | Bytecode interpreter, dispatch, debug |
| Runtime (C)         |      63 |       5,344 | Strings, arrays, I/O, OOP, memory     |
| **Execution Total** | **121** |  **18,211** |                                       |

### Frontends

| Component          |   Files | Code (SLOC) | Description                                |
|--------------------|--------:|------------:|--------------------------------------------|
| BASIC Frontend     |     232 |      39,782 | Lexer, parser, semantic analysis, lowering |
| **Frontend Total** | **232** |  **39,782** |                                            |

### Native Code Generation

| Component         |  Files | Code (SLOC) | Description                        |
|-------------------|-------:|------------:|------------------------------------|
| x86-64 Backend    |     61 |       7,888 | Linear scan regalloc, System V ABI |
| ARM64 Backend     |     18 |       4,465 | AAPCS64, Apple Silicon support     |
| Codegen Common    |      2 |          65 | Shared utilities                   |
| **Codegen Total** | **81** |  **12,418** |                                    |

### Tools & Libraries

| Component       |   Files | Code (SLOC) | Description                           |
|-----------------|--------:|------------:|---------------------------------------|
| CLI Tools       |      26 |       2,362 | vbasic, ilrun, ilc, il-verify, il-dis |
| Support         |      19 |         648 | Arena, diagnostics, source manager    |
| TUI             |     106 |       6,768 | Terminal UI library                   |
| Graphics        |      29 |       7,817 | ViperGFX 2D graphics                  |
| **Tools Total** | **180** |  **17,595** |                                       |

### Tests & Documentation

| Component            |     Files | Code (SLOC) | Description                       |
|----------------------|----------:|------------:|-----------------------------------|
| Tests                |     1,004 |      49,395 | Unit, golden, e2e tests           |
| Documentation        |       255 |      28,692 | Markdown docs, examples           |
| Examples             |        70 |       1,220 | Example programs                  |
| Demos                |        15 |       2,530 | Demo applications (Frogger, etc.) |
| **Tests/Docs Total** | **1,344** |  **81,837** |                                   |

---

## Subsystem Breakdown Chart

```
BASIC Frontend     ████████████████████████████████████████  39,782 (18.4%)
Tests              ██████████████████████████████████████████████████  49,395 (22.9%)
Documentation      ████████████████████████████  28,692 (13.3%)
IL Layer           ██████████████████  18,967 (8.8%)
Execution (VM+RT)  ██████████████████  18,211 (8.4%)
Codegen            ████████████  12,418 (5.8%)
Tools & Libraries  █████████████████  17,595 (8.2%)
Other              ███████████████████████████████  30,666 (14.2%)
```

---

## Production Code vs Test Code

| Category        |    SLOC | Percentage |
|-----------------|--------:|-----------:|
| Production Code | 166,331 |      77.1% |
| Test Code       |  49,395 |      22.9% |

**Test Ratio:** 1 line of test code per 3.4 lines of production code

---

## Code Quality Metrics

| Metric             | Value                            |
|--------------------|----------------------------------|
| Comment Density    | 24.9% (comments / code+comments) |
| Average File Size  | 87 SLOC                          |
| Largest Component  | BASIC Frontend (39,782 SLOC)     |
| Smallest Component | Codegen Common (65 SLOC)         |

---

## Notes

- **SLOC** = Source Lines of Code (excludes blank lines and comments)
- **Visual Basic** entries are Viper BASIC source files (`.bas`)
- **Viper IL** entries are intermediate language files (`.il`)
- Generated using `cloc v2.06`
- Excludes build directories, node_modules, and .git
