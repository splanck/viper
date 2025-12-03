# BASIC Frontend Codebase - Comprehensive Map

## Executive Summary
The BASIC frontend in `/Users/stephen/git/viper/src/frontends/basic/` is a substantial compiler front-end with **~44,260 lines of code** across **151 files** organized into 9 logical subsystems. The codebase follows a clean pipeline architecture: **Lexing → Parsing → Semantic Analysis → Lowering → IL Emission**.

### Key Statistics
- **Total Lines**: 44,260 LOC
- **Total Files**: 151 (107 headers, 44 implementations)
- **Main Subdirectories**: 8 (ast, builtins, constfold, lower, lower/builtins, lower/common, print, sem)
- **Largest Files**: Lowerer.Procedure.cpp (1,147 LOC), ConstFolder.cpp (889 LOC), AST.cpp (928 LOC)
- **Compilation Pipeline**: Lexer → Parser → SemanticAnalyzer → Lowerer → IL Module

---

## 1. Directory Structure

```
src/frontends/basic/
├── Core Components (root level - 80+ files)
│   ├── Lexer/Parser/Semantic/Lowering
│   ├── AST & Type System
│   ├── Name Mangling & Symbol Tables
│   └── Diagnostics & Utilities
│
├── ast/ (9 files - AST node definitions)
│   ├── ExprNodes.hpp (421 lines) - expression node types
│   ├── StmtBase.hpp (190 lines) - statement base & visitors
│   ├── StmtControl.hpp (342 lines) - control flow statements
│   ├── StmtExpr.hpp (414 lines) - expression & assignment statements
│   ├── StmtDecl.hpp (281 lines) - declarations (procedures, types, etc)
│   └── Support headers (NodeFwd, DeclNodes, StmtNodes)
│
├── builtins/ (4 files - BASIC builtin definitions)
│   ├── MathBuiltins.cpp/hpp - Math functions (ABS, SQR, SIN, etc)
│   └── StringBuiltins.cpp/hpp - String functions (LEN, INSTR, MID, etc)
│
├── constfold/ (7 files - compile-time constant folding)
│   ├── Dispatch.cpp/hpp - entry point for folding
│   ├── FoldArith.cpp - arithmetic operations
│   ├── FoldCompare.cpp - comparison operations
│   ├── FoldLogical.cpp - logical operations
│   ├── FoldStrings.cpp - string operations
│   ├── FoldCasts.cpp - type conversions
│   └── Value.hpp & ConstantUtils.hpp - value representation
│
├── lower/ (22 files - IL lowering machinery)
│   ├── Emitter.cpp/hpp (737 LOC) - IL instruction emission
│   ├── Lowerer_Expr.cpp (726 LOC) - expression lowering
│   ├── Lowerer_Stmt.cpp (494 LOC) - statement lowering
│   ├── Scan_RuntimeNeeds.cpp (797 LOC) - runtime function analysis
│   ├── Scan_ExprTypes.cpp (474 LOC) - expression type scanning
│   ├── Lower_If.cpp, Lower_Loops.cpp, Lower_Switch.cpp, Lower_TryCatch.cpp
│   ├── Lowerer_Errors.cpp (147 LOC) - error handling lowering
│   ├── BuiltinCommon.cpp/hpp - shared builtin lowering utilities
│   ├── AstVisitor.hpp - visitor pattern for lowering
│   │
│   ├── builtins/ (6 files - builtin function lowering)
│   │   ├── Common.cpp (1,021 LOC) - large centralized builtin lowering
│   │   ├── Array.cpp, IO.cpp, Math.cpp, String.cpp
│   │   └── Registrars.hpp - builtin registration
│   │
│   └── common/ (3 files - shared lowering infrastructure)
│       ├── CommonLowering.cpp/hpp - shared lowering logic
│       └── BuiltinUtils.cpp/hpp - builtin utility functions
│
├── print/ (5 files - AST printing for diagnostics)
│   ├── Print_Stmt_Common.hpp (279 LOC) - statement printing utilities
│   ├── Print_Stmt_*.cpp - printing for various statement types
│   └── Used for AST dumps and debug output
│
└── sem/ (9 files - semantic analysis & checking)
    ├── Check_Expr_Binary.cpp (519 LOC) - binary operator checks
    ├── Check_Expr_Unary.cpp (116 LOC) - unary operator checks
    ├── Check_Common.hpp (333 LOC) - shared checking utilities
    ├── Check_SelectDetail.hpp (458 LOC) - SELECT CASE validation
    ├── Check_Expr_Call.cpp - function call validation
    ├── Check_If.cpp, Check_Loops.cpp, Check_Jumps.cpp, Check_Select.cpp
    └── Validates operator type compatibility, control flow, etc
```

---

## 2. Core Components (Root Level)

### 2.1 Pipeline Entry Points

| File | Lines | Purpose |
|------|-------|---------|
| **BasicCompiler.hpp/cpp** | 66/127 | Main entry point; orchestrates Lexer→Parser→Semantic→Lowerer |
| **Lexer.hpp/cpp** | 59/519 | Tokenization; yields stream of Token objects |
| **Parser.hpp/cpp** | 568/318 | Syntax analysis; produces Program AST; registry-based statement dispatch |
| **SemanticAnalyzer.hpp** | 449 | Symbol & label tracking, type inference, two-pass proc registration |
| **Lowerer.hpp** | 788 | IL lowering orchestration; translates AST → IL Module |

### 2.2 AST & Token Structures

| File | Lines | Purpose |
|------|-------|---------|
| **AST.hpp** | 12 | Stub header; includes full AST definition |
| **AST.cpp** | 928 | AST node implementations (visit methods, accessors) |
| **Token.hpp/cpp** | 44/59 | Token structure (kind, location, lexeme) |
| **Parser_Token.hpp** | 33 | Token handling helpers (peek, consume, advance) |
| **TokenKinds.def** | 3.6 KB | Macro-based token kind enumeration |

### 2.3 Type System & Names

| File | Lines | Purpose |
|------|-------|---------|
| **BasicTypes.hpp** | 58 | BASIC type enum (I64, F64, String, Bool, etc) |
| **TypeRules.hpp/cpp** | 58/311 | Type compatibility rules for operators |
| **TypeSuffix.hpp/cpp** | 26/68 | BASIC type suffixes ($, %, &, !, #) |
| **NameMangler.hpp/cpp** | 34/56 | Symbol name mangling (avoiding IL conflicts) |
| **NameMangler_OOP.hpp/cpp** | 53/144 | OOP-specific name mangling (classes, methods) |

### 2.4 Diagnostics & Error Handling

| File | Lines | Purpose |
|------|-------|---------|
| **DiagnosticEmitter.hpp/cpp** | 86/192 | Formats diagnostics with source location info |
| **SemanticDiagnostics.hpp/cpp** | 67/136 | Semantic-specific error messages |
| **BasicDiagnosticMessages.hpp** | 62 | Message templates & codes |
| **sem/Check_Common.hpp** | 333 | Shared checking utilities & diagnostic helpers |

### 2.5 Symbol & Scope Management

| File | Lines | Purpose |
|------|-------|---------|
| **ScopeTracker.hpp/cpp** | 51/126 | Procedure scope tracking (namespace aliasing) |
| **ProcRegistry.hpp/cpp** | 75/134 | Procedure signature registration & lookup |
| **AstWalkerUtils.hpp/cpp** | 92/44 | Helpers for AST traversal |

### 2.6 Constant Folding

| File | Lines | Purpose |
|------|-------|---------|
| **ConstFolder.hpp/cpp** | 18/889 | Dispatch & orchestration for constant folding |
| **constfold/Dispatch.cpp/hpp** | 341/120 | Visitor-based folding dispatch |
| **constfold/Value.hpp** | 242 | Variant-based constant value representation |

---

## 3. Parser Implementation (168 KB)

### 3.1 Core Parser

| File | Lines | Purpose |
|------|-------|---------|
| **Parser.hpp** | 568 | Main parser class; statement registry pattern |
| **Parser.cpp** | 318 | Program parsing, token management, sync-on-error |
| **Parser_Expr.cpp** | 608 | Expression parsing; precedence-climbing algorithm |
| **Parser_Stmt.cpp** | 343 | Statement dispatcher & factory |

### 3.2 Statement Parsers (by category)

**Control Flow Parsers (526 LOC)**
- `Parser_Stmt_Control.cpp` (62 LOC) - dispatcher
- `Parser_Stmt_If.cpp` (263 LOC) - IF/THEN/ELSE/END IF blocks
- `Parser_Stmt_Loop.cpp` (231 LOC) - FOR, WHILE, DO loops
- `Parser_Stmt_Select.cpp` (726 LOC) - SELECT CASE with complex arm parsing

**I/O Parsers (329 LOC)**
- `Parser_Stmt_IO.cpp` (329 LOC) - PRINT, WRITE, INPUT, LINE INPUT, OPEN, CLOSE, SEEK

**Jump/Runtime Parsers (513 LOC)**
- `Parser_Stmt_Jump.cpp` (152 LOC) - GOTO, GOSUB, RETURN
- `Parser_Stmt_Runtime.cpp` (361 LOC) - DIM, REDIM, RANDOMIZE, etc.

**OOP Parsers (558 LOC)**
- `Parser_Stmt_OOP.cpp` (558 LOC) - CLASS, TYPE, INTERFACE, NEW, method declarations

---

## 4. Semantic Analysis (2,000+ LOC)

### 4.1 Main Analyzer

| File | Lines | Purpose |
|------|-------|---------|
| **SemanticAnalyzer.hpp** | 449 | Symbol table, type tracking, two-pass proc registration |
| **SemanticAnalyzer.cpp** | 329 | Main analysis loop & program entry |
| **SemanticAnalyzer.Exprs.cpp** | 591 | Expression type inference & validation |
| **SemanticAnalyzer.Procs.cpp** | 491 | Procedure signature registration & validation |

### 4.2 Statement Analysis (1,108 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **SemanticAnalyzer.Stmts.cpp** | 295 | Main statement visitor dispatch |
| **SemanticAnalyzer.Stmts.Control.cpp** | 269 | IF, loops, SELECT CASE checking |
| **SemanticAnalyzer.Stmts.IO.cpp** | 327 | PRINT, INPUT, file I/O validation |
| **SemanticAnalyzer.Stmts.Runtime.cpp** | 494 | DIM, arrays, runtime statements |
| **SemanticAnalyzer.Stmts.Shared.cpp** | 187 | Shared validation logic |
| **SemanticAnalyzer.Builtins.cpp** | 351 | Builtin function signature definitions |

### 4.3 Detailed Checking (1,462 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **sem/Check_Expr_Binary.cpp** | 519 | Operator type compatibility rules |
| **sem/Check_SelectDetail.hpp** | 458 | Exhaustive SELECT CASE validation |
| **sem/Check_Common.hpp** | 333 | Shared checking utilities |
| **sem/Check_Loops.cpp** | 204 | FOR/NEXT nesting, LOOP structure |
| **sem/Check_Jumps.cpp** | 170 | GOTO/GOSUB label validation |

---

## 5. Lowering (IL Generation) (5,000+ LOC)

### 5.1 Lowerer Orchestration

| File | Lines | Purpose |
|------|-------|---------|
| **Lowerer.hpp** | 788 | Main lowerer class; IL IR builder interface |
| **Lowerer.cpp** | 190 | Constructor, setup, accessors |
| **Lowerer.Procedure.cpp** | 1,147 | Procedure lowering; block creation & jump targets |
| **Lowerer.Program.cpp** | 119 | Program entry & main block synthesis |
| **Lowerer.Statement.cpp** | 113 | Statement dispatch to specific lowerers |

### 5.2 Expression Lowering (1,629 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **lower/Lowerer_Expr.cpp** | 726 | General expression lowering dispatch |
| **LowerExprNumeric.cpp** | 588 | Arithmetic & comparison codegen |
| **LowerExprLogical.cpp** | 209 | Logical operators, boolean short-circuit |
| **LowerExprBuiltin.cpp** | 436 | Builtin function call lowering |
| **LowerExpr.cpp** | 405 | Variable & array reference lowering |

### 5.3 Statement Lowering (1,353 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **lower/Lowerer_Stmt.cpp** | 494 | Main statement lowering dispatch |
| **LowerStmt_Control.cpp** | 235 | IF, loop structure lowering |
| **LowerStmt_IO.cpp** | 653 | PRINT, INPUT, file I/O codegen |
| **LowerStmt_Runtime.cpp** | 466 | DIM, array allocation, runtime calls |
| **lower/Lower_Loops.cpp** | 526 | FOR, WHILE, DO loop expansion |
| **lower/Lower_If.cpp** | 221 | IF block structure & branch gen |
| **lower/Lower_Switch.cpp** | 49 | SELECT CASE lowering |
| **lower/Lower_TryCatch.cpp** | 143 | Error handling, ON ERROR GOTO |

### 5.4 Builtin Lowering (1,451 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **lower/builtins/Common.cpp** | 1,021 | Centralized builtin codegen (largest file) |
| **lower/builtins/Math.cpp** | 188 | Math function IL emission |
| **lower/builtins/String.cpp** | 151 | String function IL emission |
| **lower/builtins/Array.cpp** | 44 | Array operation codegen |
| **lower/builtins/IO.cpp** | 53 | File/console I/O lowering |
| **lower/BuiltinCommon.cpp/hpp** | 44/146 | Shared builtin utilities |

### 5.5 Infrastructure & Helpers (1,269 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **lower/Emitter.cpp** | 737 | IL instruction emission (builder wrapper) |
| **lower/Scan_RuntimeNeeds.cpp** | 797 | Identifies runtime functions needed by program |
| **lower/Scan_ExprTypes.cpp** | 474 | Infers expression types for code generation |
| **lower/common/CommonLowering.cpp** | 444 | Shared lowering patterns (control flow, etc) |
| **LoweringContext.hpp/cpp** | 67/79 | Lowering context & state management |
| **LoweringPipeline.hpp** | 130 | Pipeline coordination |
| **LowerRuntime.cpp/hpp** | 479/58 | Runtime function declarations & helpers |

---

## 6. Supporting Features

### 6.1 AST Printing (1,065 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **AstPrinter.cpp/hpp** | 173/124 | Base printer & visitor |
| **AstPrint_Stmt.cpp** | 518 | Statement printing (debug output) |
| **AstPrint_Expr.cpp** | 307 | Expression printing |
| **print/Print_Stmt_*.cpp** | 300 LOC | Category-specific statement printing |

### 6.2 OOP Features (1,503 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **Semantic_OOP.cpp/hpp** | 810/179 | OOP symbol index, class/interface tracking |
| **Lower_OOP_Emit.cpp** | 698 | OOP IL codegen (vtables, constructors) |
| **Lower_OOP_Expr.cpp** | 501 | NEW, method calls, member access |
| **NameMangler_OOP.cpp/hpp** | 144/53 | Class/method name mangling |
| **Lower_OOP_Scan.cpp** | 229 | OOP code analysis pass |
| **Lower_OOP_Stmt.cpp** | 108 | OOP statement lowering |

### 6.3 SELECT CASE Handling (765 LOC)

| File | Lines | Purpose |
|------|-------|---------|
| **SelectCaseLowering.cpp/hpp** | 609/109 | SELECT CASE → IF/jump expansion |
| **SelectModel.cpp/hpp** | 156/117 | SELECT CASE model (range handling) |
| **SelectCaseRange.hpp** | 27 | Range representation |

### 6.4 Utilities

| File | Lines | Purpose |
|------|-------|---------|
| **LineUtils.hpp** | 41 | Line number management |
| **Intrinsics.cpp/hpp** | 110/50 | LOF, EOF, LBOUND, UBOUND handling |
| **StatementSequencer.cpp/hpp** | 453/110 | Line/statement boundary tracking |
| **EmitCommon.cpp/hpp** | 253/101 | Common IL emission patterns |
| **LowerEmit.cpp/hpp** | 193/180 | Wrapper around IL emitter |
| **BuiltinRegistry.cpp/hpp** | 308/222 | Centralized builtin function registry |

---

## 7. File Organization Patterns

### Header/Implementation Separation
- **Headers (*.hpp)**: Type definitions, class interfaces, inline utilities
- **Implementation (*.cpp)**: Methods, visitors, algorithms

### Naming Conventions
1. **Category Prefix**: `SemanticAnalyzer.Stmts.IO.cpp` = semantic analysis for I/O statements
2. **Visitor Pattern**: `Check_*.cpp` files implement semantic checks
3. **Lowering Prefix**: `Lower*.cpp` = IL emission for specific constructs
4. **Builtin Suffix**: `MathBuiltins.cpp` = definitions of math functions

### Subsystem Organization

```
Lexing:       Lexer.hpp/cpp + Token system
Parsing:      Parser.hpp + Parser_*.cpp (split by statement category)
Semantics:    SemanticAnalyzer.hpp + SemanticAnalyzer.*.cpp + sem/*.cpp
Lowering:     Lowerer.hpp + Lowerer.*.cpp + lower/*.cpp
Support:      Type system, Name mangling, Diagnostics, Utilities
```

---

## 8. Key Classes & Responsibilities

### Parser
- Owns: Lexer, token buffer, statement registry
- Produces: Program AST with separate procedure/statement lists
- Handles: Recursive descent with precedence climbing for expressions

### SemanticAnalyzer
- Tracks: Symbols, labels, types, procedure signatures
- Two-pass design: first proc registration, then validation
- Reports: Type mismatches, undefined references, control flow errors

### Lowerer
- Orchestrates: IL generation for entire program
- Uses: IRBuilder to emit instructions
- Produces: il::core::Module with procedures and main entry

### Emitter
- Wraps: IRBuilder interface
- Emits: IL instructions (loads, stores, operations, calls)

---

## 9. Complexity Analysis

### Largest Components by LOC
1. **lower/builtins/Common.cpp** (1,021) - Massive centralized builtin lowering
2. **Lowerer.Procedure.cpp** (1,147) - Procedure boundary & block handling
3. **AST.cpp** (928) - AST node implementations
4. **ConstFolder.cpp** (889) - Constant folding logic
5. **lower/Scan_RuntimeNeeds.cpp** (797) - Complex runtime analysis

### Most Complex Subsystems
1. **SELECT CASE Handling** - Multi-file, complex arm parsing & lowering
2. **Builtin Functions** - 1,000+ LOC centralized dispatch
3. **Expression Lowering** - Multiple passes (type scan, lowering, constant fold)
4. **OOP Support** - Spread across 6 files, vtable/name mangling

---

## 10. Quality Observations

### Strengths
- **Clean layering**: Lexer → Parser → Semantic → Lowering
- **Well-organized**: Statements grouped by category
- **Pattern-heavy**: Visitor pattern, registry pattern, semantic checkers
- **Comprehensive**: Handles BASIC, OOP, error handling, constant folding
- **Modular builtins**: Separate files for Math/String/IO/Array

### Potential Issue Areas
- **Large centralized files**: Common.cpp (1,021 LOC), Lowerer.Procedure.cpp (1,147 LOC)
- **Multi-concern files**: AST.cpp mixes definitions & visitors (928 LOC)
- **Complex lowering**: Expression/statement lowering split across many files
- **Test coverage**: Recommend checking sem/ & lower/ test coverage

---

## 11. Integration Points

### External Dependencies
- **IL Core** (`viper/il/`): Module, IRBuilder, types, instructions
- **Support** (`support/`): Diagnostics, source locations, expected<T>
- **Runtime** (`il/runtime/`): RuntimeSignatures for extern calls

### Within Frontend
- **Lexer** → produces Token stream
- **Parser** → consumes Token stream, produces Program AST
- **SemanticAnalyzer** → validates AST, tracks symbols
- **Lowerer** → converts AST → IL Module
- **ConstFolder** → run before lowering for optimization
- **OopIndex** → class/interface tracking (Semantic_OOP.cpp)

---

## 12. Build Configuration

Location: `CMakeLists.txt` (4.5 KB)
- Targets: `viper_frontends_basic` library
- Links to: IL core, runtime, support libraries
- Dependencies: fmt, CLI11, Catch2 (via parent)

