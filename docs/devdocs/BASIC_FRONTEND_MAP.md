# BASIC Frontend Codebase - Comprehensive Map

## Executive Summary

The BASIC frontend in `src/frontends/basic/` is a substantial compiler front-end with **~72,190
lines of code** across **269 files** organized into logical subsystems. The codebase follows a clean pipeline
architecture: **Lexing → Parsing → Semantic Analysis → Lowering → IL Emission**.

### Key Statistics

- **Total Lines**: ~72,190 LOC
- **Total Files**: 269 (114 headers, 155 implementations) plus `TokenKinds.def`
- **Main Subdirectories**: 10 (ast, builtins, constfold, detail, diag, lower, passes, print, sem, types) with lower having 4 subdirs (builtins, common, detail, oop)
- **Largest Files**: lower/builtins/Common.cpp (1,035 LOC), lower/Scan_RuntimeNeeds.cpp (999 LOC), lower/Emitter.cpp (899 LOC)
- **Compilation Pipeline**: Lexer → Parser → SemanticAnalyzer → Lowerer → IL Module

---

## 1. Directory Structure

```
src/frontends/basic/
├── Core Components (root level - 180+ files)
│   ├── Lexer/Parser/Semantic/Lowering
│   ├── AST & Type System
│   ├── Name Mangling & Symbol Tables
│   └── Diagnostics & Utilities
│
├── ast/ (9 files - AST node definitions)
│   ├── ExprNodes.hpp - expression node types
│   ├── StmtBase.hpp - statement base & visitors
│   ├── StmtControl.hpp - control flow statements
│   ├── StmtExpr.hpp - expression & assignment statements
│   ├── StmtDecl.hpp - declarations (procedures, types, etc)
│   └── Support headers (NodeFwd, DeclNodes, StmtNodes, StmtNodesAll)
│
├── builtins/ (4 files - BASIC builtin definitions)
│   ├── MathBuiltins.cpp/hpp - Math functions (ABS, SQR, SIN, etc)
│   └── StringBuiltins.cpp/hpp - String functions (LEN, INSTR, MID, etc)
│
├── constfold/ (9 files - compile-time constant folding)
│   ├── Dispatch.cpp/hpp - entry point for folding
│   ├── FoldArith.cpp - arithmetic operations
│   ├── FoldBuiltins.cpp - builtin function folding
│   ├── FoldCasts.cpp - type conversions
│   ├── FoldCompare.cpp - comparison operations
│   ├── FoldLogical.cpp - logical operations
│   ├── FoldStrings.cpp - string operations
│   └── Value.hpp & ConstantUtils.hpp - value representation
│
├── detail/ (1 file - internal OOP semantic declarations)
│   └── Semantic_OOP_Internal.hpp
│
├── diag/ (1 file - BASIC-specific diagnostics)
│   └── BasicDiag.cpp
│
├── lower/ (19 files - IL lowering machinery)
│   ├── Emitter.cpp/hpp (899 LOC) - IL instruction emission
│   ├── Lowerer_Expr.cpp (982 LOC) - expression lowering
│   ├── Lowerer_Stmt.cpp (756 LOC) - statement lowering
│   ├── Scan_RuntimeNeeds.cpp (999 LOC) - runtime function analysis
│   ├── Scan_ExprTypes.cpp (488 LOC) - expression type scanning
│   ├── Lower_If.cpp, Lower_Loops.cpp, Lower_Switch.cpp, Lower_TryCatch.cpp
│   ├── Lower_Expr_NumericClassifier.cpp - numeric type classification
│   ├── Lowerer_Errors.cpp - error handling lowering
│   ├── BuiltinCommon.cpp/hpp - shared builtin lowering utilities
│   ├── AstVisitor.hpp - visitor pattern for lowering
│   ├── Emit_Builtin.cpp, Emit_Control.cpp, Emit_Expr.cpp, Emit_OOP.cpp
│   │
│   ├── builtins/ (6 files - builtin function lowering)
│   │   ├── Common.cpp (1,035 LOC) - large centralized builtin lowering
│   │   ├── Array.cpp, IO.cpp, Math.cpp, String.cpp
│   │   └── Registrars.hpp - builtin registration
│   │
│   ├── common/ (4 files - shared lowering infrastructure)
│   │   ├── CommonLowering.cpp/hpp - shared lowering logic
│   │   └── BuiltinUtils.cpp/hpp - builtin utility functions
│   │
│   ├── detail/ (5 files - lowering helpers)
│   │   ├── LowererDetail.hpp - internal declarations
│   │   ├── ControlLoweringHelper.cpp, ExprLoweringHelper.cpp
│   │   ├── OopLoweringHelper.cpp, RuntimeLoweringHelper.cpp
│   │
│   └── oop/ (13 files - OOP IL lowering)
│       ├── Lower_OOP_Emit.cpp, Lower_OOP_Expr.cpp, Lower_OOP_Alloc.cpp
│       ├── Lower_OOP_MemberAccess.cpp, Lower_OOP_MethodCall.cpp
│       ├── Lower_OOP_Scan.cpp, Lower_OOP_Stmt.cpp
│       ├── Lower_OOP_Helpers.cpp, Lower_OOP_RuntimeHelpers.cpp/hpp
│       ├── Lower_OOP_Internal.hpp
│       └── MethodDispatchHelpers.cpp/hpp
│
├── passes/ (2 files - compiler passes)
│   └── CollectProcs.cpp/hpp - procedure collection pass
│
├── print/ (5 files - AST printing for diagnostics)
│   ├── Print_Stmt_Common.hpp - statement printing utilities
│   ├── Print_Stmt_Control.cpp, Print_Stmt_Decl.cpp
│   ├── Print_Stmt_IO.cpp, Print_Stmt_Jump.cpp
│   └── Used for AST dumps and debug output
│
├── sem/ (25+ files - semantic analysis & checking)
│   ├── Check_Expr_Binary.cpp - binary operator checks
│   ├── Check_Expr_Unary.cpp - unary operator checks
│   ├── Check_Common.hpp - shared checking utilities
│   ├── Check_SelectDetail.hpp - SELECT CASE validation
│   ├── Check_Expr_Array.cpp, Check_Expr_Call.cpp, Check_Expr_Var.cpp
│   ├── Check_If.cpp, Check_Loops.cpp, Check_Jumps.cpp, Check_Select.cpp
│   ├── NamespaceRegistry.cpp/hpp, OverloadResolution.cpp/hpp
│   ├── RegistryBuilder.cpp/hpp, RuntimeMethodIndex.cpp/hpp
│   ├── RuntimePropertyIndex.cpp/hpp, TypeRegistry.cpp/hpp
│   ├── TypeResolver.cpp/hpp, UsingContext.cpp/hpp
│   └── Validates operator type compatibility, control flow, etc
│
└── types/ (2 files - BASIC to IL type mapping)
    └── TypeMapping.cpp/hpp
```

---

## 2. Core Components (Root Level)

### 2.1 Pipeline Entry Points

| File                      | Lines     | Purpose                                                                  |
|---------------------------|-----------|--------------------------------------------------------------------------|
| **BasicCompiler.hpp/cpp** | 142/146   | Main entry point; orchestrates Lexer→Parser→Semantic→Lowerer             |
| **Lexer.hpp/cpp**         | 115/587   | Tokenization; yields stream of Token objects                             |
| **Parser.hpp/cpp**        | 752/618   | Syntax analysis; produces Program AST; registry-based statement dispatch |
| **SemanticAnalyzer.hpp**  | 616       | Symbol & label tracking, type inference, two-pass proc registration      |
| **Lowerer.hpp**           | 1,432     | IL lowering orchestration; translates AST → IL Module                   |

### 2.2 AST & Token Structures

| File                 | Lines  | Purpose                                             |
|----------------------|--------|-----------------------------------------------------|
| **AST.hpp**          | 51     | Stub header; includes full AST definition           |
| **AST.cpp**          | 199    | AST node implementations (visit methods, accessors) |
| **Token.hpp/cpp**    | 77/59  | Token structure (kind, location, lexeme)            |
| **Parser_Token.hpp** | 88     | Token handling helpers (peek, consume, advance)     |
| **TokenKinds.def**   | 3.6 KB | Macro-based token kind enumeration                  |

### 2.3 Type System & Names

| File                    | Lines  | Purpose                                        |
|-------------------------|--------|------------------------------------------------|
| **BasicTypes.hpp**      | 101    | BASIC type enum (I64, F64, String, Bool, etc)  |
| **NameMangler.hpp/cpp** | 26/56  | Symbol name mangling (avoiding IL conflicts)   |
| **NameMangler_OOP.hpp** | 38     | OOP-specific name mangling (classes, methods)  |
| **TypeRules.hpp/cpp**   | 108/311| Type compatibility rules for operators         |
| **TypeSuffix.hpp/cpp**  | 36/68  | BASIC type suffixes ($, %, &, !, #)            |

### 2.4 Diagnostics & Error Handling

| File                            | Lines   | Purpose                                        |
|---------------------------------|---------|------------------------------------------------|
| **BasicDiagnosticMessages.hpp** | 72      | Message templates & codes                      |
| **DiagnosticEmitter.hpp/cpp**   | 145/205 | Formats diagnostics with source location info  |
| **sem/Check_Common.hpp**        | 721     | Shared checking utilities & diagnostic helpers |
| **SemanticDiagnostics.hpp/cpp** | 103/136 | Semantic-specific error messages               |

### 2.5 Symbol & Scope Management

| File                       | Lines   | Purpose                                       |
|----------------------------|---------|-----------------------------------------------|
| **AstWalkerUtils.hpp/cpp** | 102/44  | Helpers for AST traversal                     |
| **ProcRegistry.hpp/cpp**   | 161/386 | Procedure signature registration & lookup     |
| **ScopeTracker.hpp**       | 29      | Procedure scope tracking (namespace aliasing) |

### 2.6 Constant Folding

| File                           | Lines   | Purpose                                       |
|--------------------------------|---------|-----------------------------------------------|
| **ConstFolder.hpp/cpp**        | 74/790  | Dispatch & orchestration for constant folding |
| **constfold/Dispatch.cpp/hpp** | 341/138 | Visitor-based folding dispatch                |
| **constfold/Value.hpp**        | 248     | Variant-based constant value representation   |

---

## 3. Parser Implementation (168 KB)

### 3.1 Core Parser

| File                | Lines | Purpose                                           |
|---------------------|-------|---------------------------------------------------|
| **Parser.hpp**      | 752   | Main parser class; statement registry pattern     |
| **Parser.cpp**      | 618   | Program parsing, token management, sync-on-error  |
| **Parser_Expr.cpp** | 844   | Expression parsing; precedence-climbing algorithm |
| **Parser_Stmt.cpp** | 518   | Statement dispatcher & factory                    |

### 3.2 Statement Parsers (by category)

**Control Flow Parsers (1,620 LOC)**

- `Parser_Stmt_Control.cpp` (221 LOC) - dispatcher
- `Parser_Stmt_If.cpp` (256 LOC) - IF/THEN/ELSE/END IF blocks
- `Parser_Stmt_Loop.cpp` (279 LOC) - FOR, WHILE, DO loops
- `Parser_Stmt_Select.cpp` (864 LOC) - SELECT CASE with complex arm parsing

**I/O Parsers (329 LOC)**

- `Parser_Stmt_IO.cpp` (329 LOC) - PRINT, WRITE, INPUT, LINE INPUT, OPEN, CLOSE, SEEK

**Jump/Runtime Parsers (715 LOC)**

- `Parser_Stmt_Jump.cpp` (128 LOC) - GOTO, GOSUB, RETURN
- `Parser_Stmt_Runtime.cpp` (587 LOC) - DIM, REDIM, RANDOMIZE, etc.

**OOP Parsers (1,122 LOC)**

- `Parser_Stmt_OOP.cpp` (1,122 LOC) - CLASS, TYPE, INTERFACE, NEW, method declarations

---

## 4. Semantic Analysis (2,000+ LOC)

### 4.1 Main Analyzer

| File                           | Lines | Purpose                                                 |
|--------------------------------|-------|---------------------------------------------------------|
| **SemanticAnalyzer.hpp**       | 616   | Symbol table, type tracking, two-pass proc registration |
| **SemanticAnalyzer.cpp**       | 464   | Main analysis loop & program entry                      |
| **SemanticAnalyzer_Exprs.cpp** | 939   | Expression type inference & validation                  |
| **SemanticAnalyzer_Procs.cpp** | 985   | Procedure signature registration & validation           |

### 4.2 Statement Analysis (2,724 LOC)

| File                                   | Lines | Purpose                                |
|----------------------------------------|-------|----------------------------------------|
| **SemanticAnalyzer_Builtins.cpp**      | 510   | Builtin function signature definitions |
| **SemanticAnalyzer_Stmts.cpp**         | 348   | Main statement visitor dispatch        |
| **SemanticAnalyzer_Stmts_Control.cpp** | 413   | IF, loops, SELECT CASE checking        |
| **SemanticAnalyzer_Stmts_IO.cpp**      | 285   | PRINT, INPUT, file I/O validation      |
| **SemanticAnalyzer_Stmts_Runtime.cpp** | 973   | DIM, arrays, runtime statements        |
| **SemanticAnalyzer_Stmts_Shared.cpp**  | 195   | Shared validation logic                |

### 4.3 Detailed Checking (1,511 LOC)

| File                           | Lines | Purpose                           |
|--------------------------------|-------|-----------------------------------|
| **sem/Check_Common.hpp**       | 721   | Shared checking utilities         |
| **sem/Check_Expr_Binary.cpp**  | 472   | Operator type compatibility rules |
| **sem/Check_Jumps.cpp**        | 170   | GOTO/GOSUB label validation       |
| **sem/Check_Loops.cpp**        | 286   | FOR/NEXT nesting, LOOP structure  |
| **sem/Check_SelectDetail.hpp** | 583   | Exhaustive SELECT CASE validation |

---

## 5. Lowering (IL Generation) (5,000+ LOC)

### 5.1 Lowerer Orchestration

| File                               | Lines | Purpose                                            |
|------------------------------------|-------|----------------------------------------------------|
| **Lowerer.hpp**                    | 1,432 | Main lowerer class; IL IR builder interface        |
| **Lowerer.cpp**                    | 320   | Constructor, setup, accessors                      |
| **Lowerer_Procedure.cpp**          | 34    | Procedure lowering coordinator (thin dispatcher)   |
| **Lowerer_Procedure_Context.cpp**  | 224   | Procedure context management                       |
| **Lowerer_Procedure_Emit.cpp**     | 563   | Procedure IL emission                              |
| **Lowerer_Procedure_Signatures.cpp**| 250  | Procedure signature handling                       |
| **Lowerer_Procedure_Skeleton.cpp** | 348   | Procedure skeleton generation                      |
| **Lowerer_Procedure_Variables.cpp**| 694   | Procedure variable handling                        |
| **Lowerer_Program.cpp**            | 127   | Program entry & main block synthesis               |
| **Lowerer_Statement.cpp**          | 195   | Statement dispatch to specific lowerers            |

### 5.2 Expression Lowering (2,628 LOC)

| File                       | Lines | Purpose                                  |
|----------------------------|-------|------------------------------------------|
| **lower/Lowerer_Expr.cpp** | 982   | General expression lowering dispatch     |
| **LowerExpr.cpp**          | 388   | Variable & array reference lowering      |
| **LowerExprBuiltin.cpp**   | 495   | Builtin function call lowering           |
| **LowerExprLogical.cpp**   | 210   | Logical operators, boolean short-circuit |
| **LowerExprNumeric.cpp**   | 553   | Arithmetic & comparison codegen          |

### 5.3 Statement Lowering (2,110 LOC)

| File                         | Lines | Purpose                              |
|------------------------------|-------|--------------------------------------|
| **lower/Lower_If.cpp**       | 232   | IF block structure & branch gen      |
| **lower/Lower_Loops.cpp**    | 766   | FOR, WHILE, DO loop expansion        |
| **lower/Lower_Switch.cpp**   | 49    | SELECT CASE lowering                 |
| **lower/Lower_TryCatch.cpp** | 511   | Error handling, ON ERROR GOTO        |
| **lower/Lowerer_Stmt.cpp**   | 756   | Main statement lowering dispatch     |
| **LowerStmt_Control.cpp**    | 59    | IF, loop structure lowering          |
| **LowerStmt_IO.cpp**         | 147   | PRINT, INPUT, file I/O codegen       |
| **LowerStmt_Runtime.cpp**    | 190   | DIM, array allocation, runtime calls |

### 5.4 Builtin Lowering (1,511 LOC)

| File                            | Lines   | Purpose                                    |
|---------------------------------|---------|--------------------------------------------|
| **lower/builtins/Array.cpp**    | 44      | Array operation codegen                    |
| **lower/builtins/Common.cpp**   | 1,035   | Centralized builtin codegen (largest file) |
| **lower/builtins/IO.cpp**       | 53      | File/console I/O lowering                  |
| **lower/builtins/Math.cpp**     | 193     | Math function IL emission                  |
| **lower/builtins/String.cpp**   | 142     | String function IL emission                |
| **lower/BuiltinCommon.cpp/hpp** | 44/278  | Shared builtin utilities                   |

### 5.5 Infrastructure & Helpers (2,571 LOC)

| File                                | Lines   | Purpose                                        |
|-------------------------------------|---------|------------------------------------------------|
| **lower/Emitter.cpp**               | 899     | IL instruction emission (builder wrapper)      |
| **lower/Scan_ExprTypes.cpp**        | 488     | Infers expression types for code generation    |
| **lower/Scan_RuntimeNeeds.cpp**     | 999     | Identifies runtime functions needed by program |
| **lower/common/CommonLowering.cpp** | 444     | Shared lowering patterns (control flow, etc)   |
| **LoweringContext.hpp/cpp**         | 77/79   | Lowering context & state management            |
| **LoweringPipeline.hpp**            | 191     | Pipeline coordination                          |
| **LowerRuntime.cpp/hpp**            | 950/119 | Runtime function declarations & helpers        |

---

## 6. Supporting Features

### 6.1 AST Printing (1,301 LOC)

| File                       | Lines   | Purpose                              |
|----------------------------|---------|--------------------------------------|
| **AstPrint_Expr.cpp**      | 348     | Expression printing                  |
| **AstPrint_Stmt.cpp**      | 609     | Statement printing (debug output)    |
| **AstPrinter.cpp/hpp**     | 173/171 | Base printer & visitor               |
| **print/Print_Stmt_*.cpp** | ~300 LOC| Category-specific statement printing |

### 6.2 OOP Features

| File                             | Lines    | Purpose                                       |
|----------------------------------|----------|-----------------------------------------------|
| **lower/oop/Lower_OOP_Emit.cpp** | 1,102    | OOP IL codegen (vtables, constructors)        |
| **lower/oop/Lower_OOP_Expr.cpp** | 23       | OOP expression dispatch                       |
| **lower/oop/Lower_OOP_Scan.cpp** | 289      | OOP code analysis pass                        |
| **lower/oop/Lower_OOP_Stmt.cpp** | 127      | OOP statement lowering                        |
| **lower/oop/MethodDispatchHelpers.cpp/hpp** | 431/255 | Method dispatch utilities     |
| **NameMangler_OOP.hpp**          | 38       | Class/method name mangling                    |
| **Semantic_OOP.cpp/hpp**         | 35/34    | OOP symbol index coordinator                  |

### 6.3 SELECT CASE Handling (934 LOC)

| File                           | Lines   | Purpose                            |
|--------------------------------|---------|------------------------------------|
| **SelectCaseLowering.cpp/hpp** | 650/164 | SELECT CASE → IF/jump expansion    |
| **SelectCaseRange.hpp**        | 47      | Range representation               |
| **SelectModel.cpp/hpp**        | 156/117 | SELECT CASE model (range handling) |

### 6.4 Utilities

| File                           | Lines   | Purpose                               |
|--------------------------------|---------|---------------------------------------|
| **BuiltinRegistry.cpp/hpp**    | 388/289 | Centralized builtin function registry |
| **EmitCommon.cpp/hpp**         | 254/101 | Common IL emission patterns           |
| **Intrinsics.cpp/hpp**         | 110/93  | LOF, EOF, LBOUND, UBOUND handling     |
| **LineUtils.hpp**              | 51      | Line number management                |
| **LowerEmit.cpp/hpp**          | 243/492 | Wrapper around IL emitter             |
| **StatementSequencer.cpp/hpp** | 458/145 | Line/statement boundary tracking      |

---

## 7. File Organization Patterns

### Header/Implementation Separation

- **Headers (*.hpp)**: Type definitions, class interfaces, inline utilities
- **Implementation (*.cpp)**: Methods, visitors, algorithms

### Naming Conventions

1. **Category Prefix**: `SemanticAnalyzer_Stmts_IO.cpp` = semantic analysis for I/O statements
2. **Visitor Pattern**: `Check_*.cpp` files implement semantic checks
3. **Lowering Prefix**: `Lower*.cpp` = IL emission for specific constructs
4. **Builtin Suffix**: `MathBuiltins.cpp` = definitions of math functions

### Subsystem Organization

```
Lexing:       Lexer.hpp/cpp + Token system
Parsing:      Parser.hpp + Parser_*.cpp (split by statement category)
Semantics:    SemanticAnalyzer.hpp + SemanticAnalyzer_*.cpp + sem/*.cpp
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

1. **Lowerer.hpp** (1,432) - Main lowerer class with full IL IR builder interface
2. **Parser_Stmt_OOP.cpp** (1,122) - OOP construct parsing (CLASS, INTERFACE, etc.)
3. **lower/oop/Lower_OOP_Emit.cpp** (1,102) - OOP IL codegen (vtables, constructors)
4. **lower/builtins/Common.cpp** (1,035) - Centralized builtin lowering
5. **lower/Scan_RuntimeNeeds.cpp** (999) - Complex runtime analysis

### Most Complex Subsystems

1. **OOP Support** - Spread across 13+ files in lower/oop/, vtable/name mangling
2. **SELECT CASE Handling** - Multi-file, complex arm parsing & lowering
3. **Builtin Functions** - 1,000+ LOC centralized dispatch
4. **Expression Lowering** - Multiple passes (type scan, lowering, constant fold)
5. **Procedure Lowering** - Split across 6 specialized files

---

## 10. Quality Observations

### Strengths

- **Clean layering**: Lexer → Parser → Semantic → Lowering
- **Well-organized**: Statements grouped by category
- **Pattern-heavy**: Visitor pattern, registry pattern, semantic checkers
- **Comprehensive**: Handles BASIC, OOP, error handling, constant folding
- **Modular builtins**: Separate files for Math/String/IO/Array

### Potential Issue Areas

- **Large centralized files**: Lower_OOP_Emit.cpp (1,102 LOC), lower/builtins/Common.cpp (1,035 LOC)
- **Highly split implementation**: Procedure lowering spread across 6 files; OOP lowering across 13+ files
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

Location: `CMakeLists.txt`

- Target: `fe_basic` (static library)
- Links to: `viper_il_full`, `il_build`, `il_runtime`, `fe_common`
- Dependencies: fmt, CLI11, Catch2 (via parent)

