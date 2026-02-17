# BASIC Frontend Exploration - Documentation Index

This directory contains comprehensive documentation of the BASIC frontend codebase exploration. Three detailed maps have
been created to help understand the structure, organization, and complexity of the frontend compiler stack.

## Documents Overview

### 1. BASIC_FRONTEND_SUMMARY.txt

**Quick reference guide** - Start here for a rapid overview.

- High-level subsystem breakdown
- Largest files by line count
- Complexity hotspots identification
- Quality observations (strengths & issues)
- Entry points & data flow
- Testing recommendations

**Best for**: Getting oriented quickly, understanding major components

### 2. BASIC_FRONTEND_MAP.md

**Comprehensive detailed map** - The most thorough analysis.

- Complete directory structure with purposes
- Detailed file inventory organized by category
- File size analysis and complexity ranking
- Key class responsibilities
- Integration points
- Quality analysis & recommendations

**Best for**: In-depth understanding, finding specific files, planning refactors

### 3. BASIC_FRONTEND_ARCHITECTURE.txt

**Visual architecture diagrams** - Understanding system design.

- Compilation pipeline diagram
- Subsystem organization boxes
- AST definition structure
- Data flow example (variable assignment)
- Key design patterns used
- Component relationships

**Best for**: Understanding how components interact, system design questions

## Quick Facts

| Metric                | Value                                             |
|-----------------------|---------------------------------------------------|
| **Total Lines**       | ~72,190 LOC                                       |
| **Total Files**       | 269 (114 headers, 155 implementations) + 1 .def   |
| **Subdirectories**    | 10 top-level + 4 lower/ subdirs                   |
| **Main Pipeline**     | Lexer → Parser → Semantic → Lowering → IL         |
| **Largest File**      | Lowerer.hpp (1,432 LOC)                           |
| **Largest Component** | Lowerer (split across 10+ files, ~9,000+ LOC)     |

## Navigation Guide

### For Understanding Core Pipeline

1. Read BASIC_FRONTEND_SUMMARY.txt → "PIPELINE FLOW" section
2. Read BASIC_FRONTEND_ARCHITECTURE.txt → "COMPILATION PIPELINE"
3. Examine source: `BasicCompiler.hpp` entry point

### For Exploring Specific Subsystem

1. Refer to BASIC_FRONTEND_MAP.md → Section 3-5 depending on component
2. Look up specific files in the detailed file inventory
3. Cross-reference with architecture diagram for context

### For Identifying Complexity Hotspots

1. Check BASIC_FRONTEND_SUMMARY.txt → "COMPLEXITY HOTSPOTS"
2. Reference BASIC_FRONTEND_MAP.md → Section 9 "Complexity Analysis"
3. Investigate specific large files mentioned

### For Planning Code Changes

1. Review BASIC_FRONTEND_MAP.md → Section 7-8 for organization patterns
2. Check Section 10 for quality observations
3. Examine BASIC_FRONTEND_ARCHITECTURE.txt for design patterns
4. Consider layering & dependencies (Section 11)

## Key Insights

### Architecture Strengths

- Clean separation: Lexer → Parser → Semantic → Lowering
- Well-organized by statement/feature category
- Heavy use of visitor pattern for AST traversal
- Registry pattern for statement/builtin dispatch
- Two-pass semantic analysis for procedure handling

### Problem Areas to Watch

- **Large centralized files**: Lower_OOP_Emit.cpp (1,102 LOC), lower/builtins/Common.cpp (1,035 LOC)
- **Scattered complexity**: SELECT CASE handling spans parsing, semantic, lowering
- **Expression lowering**: Split across 5+ files with multiple passes
- **OOP implementation**: Spread across 13+ files in lower/oop/ with name mangling complexity

### Recommended Investigation Areas

1. **OOP lowering** - 1,100+ LOC in Lower_OOP_Emit.cpp (refactoring candidate)
2. **Builtin handling** - 1,000+ LOC centralized in lower/builtins/Common.cpp
3. **SELECT CASE** - Complex multi-file implementation (maintainability question)
4. **Expression lowering** - Multiple passes may hide optimization opportunities
5. **Test coverage** - Need to verify test coverage for sem/ and lower/ subsystems

## File Organization Patterns

The codebase uses consistent naming conventions:

```
SemanticAnalyzer.*.cpp        → Semantic analysis phase
Parser_Stmt_*.cpp             → Statement parsing (by category)
Lower*.cpp / lower/Lower*.cpp → IL generation/lowering
sem/Check_*.cpp               → Semantic checking rules
lower/builtins/*.cpp          → Builtin function lowering
constfold/Fold*.cpp           → Constant folding by operation
```

## Integration Points

### External Dependencies

- `viper/il/` - IL Core (Module, IRBuilder, instruction types)
- `support/` - Diagnostics, SourceManager, error handling
- `il/runtime/` - RuntimeSignatures for extern calls

### Internal Pipeline

```
Lexer → Parser → SemanticAnalyzer → ConstFolder (opt) → Lowerer → IL Module
```

## How to Use These Documents

**First Time?**
→ Read BASIC_FRONTEND_SUMMARY.txt, then BASIC_FRONTEND_ARCHITECTURE.txt

**Need Specific File?**
→ Use BASIC_FRONTEND_MAP.md file inventory with search

**Understanding Component Interaction?**
→ Review BASIC_FRONTEND_ARCHITECTURE.txt diagrams

**Planning Changes?**
→ Check BASIC_FRONTEND_MAP.md for patterns & dependencies

**Investigating Issues?**
→ Use BASIC_FRONTEND_SUMMARY.txt for hotspots section

## Source Code Locations

All files referenced in these documents are located in:

```
src/frontends/basic/
```

Subdirectories:

- `ast/` - AST node definitions (9 files)
- `builtins/` - Builtin function definitions (4 files)
- `constfold/` - Constant folding (9 files)
- `detail/` - Internal OOP semantic declarations (1 file)
- `diag/` - BASIC-specific diagnostics (1 file)
- `lower/` - IL lowering machinery (19 files)
- `lower/builtins/` - Builtin function lowering (6 files)
- `lower/common/` - Shared lowering infrastructure (4 files)
- `lower/detail/` - Lowering helper implementations (5 files)
- `lower/oop/` - OOP IL lowering (13 files)
- `passes/` - Compiler passes (2 files)
- `print/` - AST printing utilities (5 files)
- `sem/` - Semantic checking (25+ files)
- `types/` - BASIC to IL type mapping (2 files)

## Next Steps

1. **For code changes**: Review applicable section in MAP, check patterns in ARCHITECTURE
2. **For new features**: Identify analogous features, follow existing patterns
3. **For bug fixes**: Use MAP to understand file responsibilities, check ARCHITECTURE for design
4. **For refactoring**: Review SUMMARY hotspots, check complexity analysis in MAP

---

**Generated**: November 11, 2025
**Last Reviewed**: 2026-02-17
**Exploration Level**: Very Thorough
**Total Documentation**: ~886 lines across 3 files
