# CODEMAP: BASIC Frontend

The BASIC frontend (`src/frontends/basic/`) compiles Viper BASIC source to IL.

## Core Infrastructure

| File                          | Purpose                                                |
|-------------------------------|--------------------------------------------------------|
| `BasicCompiler.hpp/cpp`       | Main compiler pipeline: parse → fold → analyze → lower |
| `Options.hpp/cpp`             | Compiler options and configuration                     |
| `BasicTypes.hpp`              | BASIC type definitions and mappings                    |
| `BasicDiagnosticMessages.hpp` | Diagnostic message templates                           |
| `DiagnosticCodes.hpp`         | Error and warning code definitions                     |
| `DiagnosticEmitter.hpp/cpp`   | Diagnostic output and formatting                       |
| `Diag.hpp/cpp`                | Diagnostic infrastructure                              |

## Lexer and Parser

| File                             | Purpose                                   |
|----------------------------------|-------------------------------------------|
| `Lexer.hpp/cpp`                  | Tokenizer for BASIC source                |
| `Token.hpp/cpp`                  | Token types and representation            |
| `Parser.hpp/cpp`                 | Recursive-descent parser core             |
| `Parser_Expr.cpp`                | Expression parsing                        |
| `Parser_Stmt.cpp`                | Statement parsing dispatch                |
| `Parser_Stmt_Core.cpp`           | Core statement parsing (LET, PRINT, etc.) |
| `Parser_Stmt_Control.cpp`        | Control flow parsing (IF, FOR, WHILE)     |
| `Parser_Stmt_ControlHelpers.hpp` | Control flow parsing helpers              |
| `Parser_Stmt_If.cpp`             | IF/THEN/ELSE parsing                      |
| `Parser_Stmt_Loop.cpp`           | Loop statement parsing                    |
| `Parser_Stmt_Select.cpp`         | SELECT CASE parsing                       |
| `Parser_Stmt_Jump.cpp`           | Jump statement parsing (GOTO, GOSUB)      |
| `Parser_Stmt_IO.cpp`             | I/O statement parsing                     |
| `Parser_Stmt_OOP.cpp`            | OOP construct parsing                     |
| `Parser_Stmt_Runtime.cpp`        | Runtime statement parsing                 |
| `Parser_Token.hpp/cpp`           | Token handling utilities                  |

## AST (`ast/`)

| File                     | Purpose                                         |
|--------------------------|-------------------------------------------------|
| `AST.hpp/cpp`            | Main AST definitions and visitor accept methods |
| `ASTUtils.hpp`           | AST utility functions                           |
| `ast/NodeFwd.hpp`        | Forward declarations                            |
| `ast/ExprNodes.hpp`      | Expression node definitions                     |
| `ast/StmtBase.hpp`       | Statement base classes                          |
| `ast/StmtNodes.hpp`      | Statement node definitions                      |
| `ast/StmtNodesAll.hpp`   | Aggregated statement includes                   |
| `ast/StmtControl.hpp`    | Control flow statement nodes                    |
| `ast/StmtDecl.hpp`       | Declaration statement nodes                     |
| `ast/StmtExpr.hpp`       | Expression statement nodes                      |
| `ast/DeclNodes.hpp`      | Declaration nodes                               |
| `AstWalker.hpp`          | AST traversal utilities                         |
| `AstWalkerUtils.hpp/cpp` | AST walker helper functions                     |
| `AstPrinter.hpp/cpp`     | AST pretty-printing                             |
| `AstPrint_Expr.cpp`      | Expression printing                             |
| `AstPrint_Stmt.cpp`      | Statement printing                              |

## Semantic Analysis (`sem/`)

| File                                     | Purpose                        |
|------------------------------------------|--------------------------------|
| `SemanticAnalyzer.hpp/cpp`               | Main semantic analyzer         |
| `SemanticAnalyzer_Internal.hpp`          | Internal analyzer helpers      |
| `SemanticAnalyzer.Exprs.cpp`             | Expression analysis            |
| `SemanticAnalyzer.Stmts.cpp`             | Statement analysis dispatch    |
| `SemanticAnalyzer.Stmts.Control.hpp/cpp` | Control flow analysis          |
| `SemanticAnalyzer.Stmts.IO.hpp/cpp`      | I/O statement analysis         |
| `SemanticAnalyzer.Stmts.Runtime.hpp/cpp` | Runtime statement analysis     |
| `SemanticAnalyzer.Stmts.Shared.hpp/cpp`  | Shared statement analysis      |
| `SemanticAnalyzer.Procs.cpp`             | Procedure analysis             |
| `SemanticAnalyzer.Builtins.cpp`          | Builtin function analysis      |
| `SemanticAnalyzer.Namespace.cpp`         | Namespace resolution           |
| `SemanticDiagnostics.hpp/cpp`            | Semantic error reporting       |
| `SemanticDiagUtil.hpp`                   | Semantic diagnostic utilities  |
| `sem/TypeRegistry.hpp/cpp`               | Type registration and lookup   |
| `sem/TypeResolver.hpp/cpp`               | Type resolution                |
| `sem/NamespaceRegistry.hpp/cpp`          | Namespace management           |
| `sem/UsingContext.hpp/cpp`               | USING directive handling       |
| `sem/OverloadResolution.hpp/cpp`         | Method overload resolution     |
| `sem/RegistryBuilder.hpp/cpp`            | Registry construction          |
| `sem/RuntimeMethodIndex.hpp/cpp`         | Runtime method lookup          |
| `sem/RuntimePropertyIndex.hpp/cpp`       | Runtime property lookup        |
| `sem/Check_*.cpp`                        | Semantic check implementations |

## OOP Support

| File                         | Purpose                                  |
|------------------------------|------------------------------------------|
| `Semantic_OOP.hpp/cpp`       | OOP semantic analysis and index building |
| `OopIndex.hpp/cpp`           | Class/interface metadata storage         |
| `OopLoweringContext.hpp/cpp` | OOP lowering state management            |
| `NameMangler_OOP.hpp/cpp`    | OOP name mangling                        |
| `Lower_OOP_Internal.hpp`     | Internal OOP lowering declarations       |
| `Lower_OOP_Alloc.cpp`        | Object allocation lowering               |
| `Lower_OOP_Emit.cpp`         | OOP IL emission                          |
| `Lower_OOP_Expr.cpp`         | OOP expression lowering                  |
| `Lower_OOP_Helpers.cpp`      | OOP lowering utilities                   |
| `Lower_OOP_MemberAccess.cpp` | Member access lowering                   |
| `Lower_OOP_MethodCall.cpp`   | Method call lowering                     |
| `Lower_OOP_Scan.cpp`         | OOP pre-scan phase                       |
| `Lower_OOP_Stmt.cpp`         | OOP statement lowering                   |

## IL Lowering

| File                        | Purpose                      |
|-----------------------------|------------------------------|
| `Lowerer.hpp/cpp`           | Main lowering coordinator    |
| `Lowerer.Procedure.cpp`     | Procedure lowering           |
| `Lowerer.Program.cpp`       | Program-level lowering       |
| `Lowerer.Statement.cpp`     | Statement lowering dispatch  |
| `LowererContext.hpp`        | Lowering context and state   |
| `LoweringContext.hpp/cpp`   | Extended lowering context    |
| `LoweringPipeline.hpp`      | Lowering pipeline definition |
| `LowerEmit.hpp/cpp`         | IL emission helpers          |
| `LowerExpr.cpp`             | Expression lowering          |
| `LowerExprBuiltin.hpp/cpp`  | Builtin expression lowering  |
| `LowerExprLogical.hpp/cpp`  | Logical expression lowering  |
| `LowerExprNumeric.hpp/cpp`  | Numeric expression lowering  |
| `LowerScan.hpp/cpp`         | Pre-lowering scan phase      |
| `LowerRuntime.hpp/cpp`      | Runtime call lowering        |
| `LowerStmt_Core.hpp`        | Core statement lowering      |
| `LowerStmt_Control.hpp/cpp` | Control flow lowering        |
| `LowerStmt_IO.hpp/cpp`      | I/O statement lowering       |
| `LowerStmt_Runtime.hpp/cpp` | Runtime statement lowering   |
| `EmitCommon.hpp/cpp`        | Common emission utilities    |

## Lowering Submodules (`lower/`)

| File                          | Purpose                      |
|-------------------------------|------------------------------|
| `lower/Emitter.hpp/cpp`       | IL emitter class             |
| `lower/AstVisitor.hpp`        | Lowering AST visitor         |
| `lower/Emit_Expr.cpp`         | Expression emission          |
| `lower/Emit_Control.cpp`      | Control flow emission        |
| `lower/Emit_Builtin.cpp`      | Builtin emission             |
| `lower/Emit_OOP.cpp`          | OOP emission                 |
| `lower/Lower_If.cpp`          | IF statement lowering        |
| `lower/Lower_Loops.cpp`       | Loop lowering                |
| `lower/Lower_Switch.cpp`      | Switch/SELECT lowering       |
| `lower/Lower_TryCatch.cpp`    | Exception handling lowering  |
| `lower/Lowerer_Expr.cpp`      | Expression lowerer           |
| `lower/Lowerer_Stmt.cpp`      | Statement lowerer            |
| `lower/Lowerer_Errors.cpp`    | Error lowering               |
| `lower/Scan_ExprTypes.cpp`    | Expression type scanning     |
| `lower/Scan_RuntimeNeeds.cpp` | Runtime requirement scanning |
| `lower/BuiltinCommon.hpp/cpp` | Common builtin lowering      |
| `lower/common/*.cpp`          | Common lowering utilities    |
| `lower/builtins/*.cpp`        | Builtin-specific lowering    |

## Statement Lowering

| File                              | Purpose                    |
|-----------------------------------|----------------------------|
| `ControlStatementLowerer.hpp/cpp` | Control statement lowering |
| `IoStatementLowerer.hpp/cpp`      | I/O statement lowering     |
| `RuntimeStatementLowerer.hpp/cpp` | Runtime statement lowering |
| `SelectCaseLowering.hpp/cpp`      | SELECT CASE lowering       |
| `SelectCaseRange.hpp`             | SELECT CASE range handling |
| `SelectModel.hpp/cpp`             | SELECT CASE model          |

## Builtins

| File                              | Purpose                        |
|-----------------------------------|--------------------------------|
| `BuiltinRegistry.hpp/cpp`         | Builtin function registry      |
| `Intrinsics.hpp/cpp`              | Intrinsic function definitions |
| `builtins/MathBuiltins.hpp/cpp`   | Math builtin implementations   |
| `builtins/StringBuiltins.hpp/cpp` | String builtin implementations |

## Constant Folding (`constfold/`)

| File                          | Purpose                           |
|-------------------------------|-----------------------------------|
| `ConstFolder.hpp/cpp`         | Constant folding coordinator      |
| `constfold/Dispatch.hpp/cpp`  | Fold dispatch logic               |
| `constfold/Value.hpp`         | Compile-time value representation |
| `constfold/ConstantUtils.hpp` | Constant utilities                |
| `constfold/FoldArith.cpp`     | Arithmetic folding                |
| `constfold/FoldCasts.cpp`     | Cast folding                      |
| `constfold/FoldCompare.cpp`   | Comparison folding                |
| `constfold/FoldLogical.cpp`   | Logical folding                   |
| `constfold/FoldStrings.cpp`   | String folding                    |

## Types

| File                        | Purpose                           |
|-----------------------------|-----------------------------------|
| `TypeRules.hpp/cpp`         | Type checking rules               |
| `TypeSuffix.hpp/cpp`        | Type suffix handling (%, $, etc.) |
| `types/TypeMapping.hpp/cpp` | BASIC to IL type mapping          |
| `ILTypeUtils.hpp/cpp`       | IL type utilities                 |

## Utilities

| File                          | Purpose                      |
|-------------------------------|------------------------------|
| `NameMangler.hpp/cpp`         | Name mangling                |
| `IdentifierUtil.hpp`          | Identifier utilities         |
| `StringUtils.hpp`             | String utilities             |
| `LineUtils.hpp`               | Line number utilities        |
| `LocationScope.hpp/cpp`       | Source location scoping      |
| `ScopeTracker.hpp/cpp`        | Scope tracking               |
| `StatementSequencer.hpp/cpp`  | Statement sequencing         |
| `ProcRegistry.hpp/cpp`        | Procedure registry           |
| `passes/CollectProcs.hpp/cpp` | Procedure collection pass    |
| `print/*.cpp`                 | Statement printing utilities |
