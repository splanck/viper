# CODEMAP: BASIC Frontend

The BASIC frontend (`src/frontends/basic/`) compiles Viper BASIC source to IL.

Last updated: 2026-01-15

## Overview

- **Total source files**: 269 (.hpp/.cpp)
- **Subdirectories**: ast/, builtins/, constfold/, detail/, diag/, lower/, passes/, print/, sem/, types/

## Core Infrastructure

| File                          | Purpose                                                |
|-------------------------------|--------------------------------------------------------|
| `BasicCompiler.cpp`           | Main compiler pipeline: parse → fold → analyze → lower |
| `BasicCompiler.hpp`           | Compiler class declarations                            |
| `Options.cpp`                 | Compiler options implementation                        |
| `Options.hpp`                 | Compiler options and configuration                     |
| `BasicTypes.hpp`              | BASIC type definitions and mappings                    |
| `BasicSymbolQuery.cpp`        | Symbol lookup and query implementation                 |
| `BasicSymbolQuery.hpp`        | Symbol lookup and query utilities                      |
| `BasicDiagnosticMessages.hpp` | Diagnostic message templates                           |
| `DiagnosticCodes.hpp`         | Error and warning code definitions                     |
| `DiagnosticEmitter.cpp`       | Diagnostic output implementation                       |
| `DiagnosticEmitter.hpp`       | Diagnostic output and formatting                       |
| `Diag.cpp`                    | Diagnostic infrastructure implementation               |
| `Diag.hpp`                    | Diagnostic infrastructure                              |

## Lexer and Parser

| File                           | Purpose                                   |
|--------------------------------|-------------------------------------------|
| `Lexer.cpp`                    | Tokenizer implementation                  |
| `Lexer.hpp`                    | Tokenizer for BASIC source                |
| `Token.cpp`                    | Token implementation                      |
| `Token.hpp`                    | Token types and representation            |
| `Parser.cpp`                   | Recursive-descent parser core             |
| `Parser.hpp`                   | Parser class declarations                 |
| `Parser_Expr.cpp`              | Expression parsing                        |
| `Parser_Stmt.cpp`              | Statement parsing dispatch                |
| `Parser_Stmt_Core.cpp`         | Core statement parsing (LET, PRINT, etc.) |
| `Parser_Stmt_Control.cpp`      | Control flow parsing (IF, FOR, WHILE)     |
| `Parser_Stmt_ControlHelpers.hpp`| Control flow parsing helpers             |
| `Parser_Stmt_If.cpp`           | IF/THEN/ELSE parsing                      |
| `Parser_Stmt_Loop.cpp`         | Loop statement parsing                    |
| `Parser_Stmt_Select.cpp`       | SELECT CASE parsing                       |
| `Parser_Stmt_Jump.cpp`         | Jump statement parsing (GOTO, GOSUB)      |
| `Parser_Stmt_IO.cpp`           | I/O statement parsing                     |
| `Parser_Stmt_OOP.cpp`          | OOP construct parsing                     |
| `Parser_Stmt_Runtime.cpp`      | Runtime statement parsing                 |
| `Parser_Token.cpp`             | Token handling implementation             |
| `Parser_Token.hpp`             | Token handling utilities                  |

## Symbol Tables

| File                          | Purpose                                   |
|-------------------------------|-------------------------------------------|
| `SymbolTable.cpp`             | Variable and procedure symbol table impl  |
| `SymbolTable.hpp`             | Variable and procedure symbol table       |
| `StringTable.cpp`             | String literal interning implementation   |
| `StringTable.hpp`             | String literal interning table            |
| `ProcedureSymbolTracker.cpp`  | Procedure symbol tracking impl            |
| `ProcedureSymbolTracker.hpp`  | Procedure symbol tracking                 |
| `ProcRegistry.cpp`            | Procedure registration implementation     |
| `ProcRegistry.hpp`            | Procedure registration and lookup         |

## AST (`ast/`)

| File                   | Purpose                                         |
|------------------------|-------------------------------------------------|
| `AST.cpp`              | Main AST implementation                         |
| `AST.hpp`              | Main AST definitions and visitor accept methods |
| `ASTUtils.hpp`         | AST utility functions                           |
| `ast/NodeFwd.hpp`      | Forward declarations                            |
| `ast/ExprNodes.hpp`    | Expression node definitions                     |
| `ast/StmtBase.hpp`     | Statement base classes                          |
| `ast/StmtNodes.hpp`    | Statement node definitions                      |
| `ast/StmtNodesAll.hpp` | Aggregated statement includes                   |
| `ast/StmtControl.hpp`  | Control flow statement nodes                    |
| `ast/StmtDecl.hpp`     | Declaration statement nodes                     |
| `ast/StmtExpr.hpp`     | Expression statement nodes                      |
| `ast/DeclNodes.hpp`    | Declaration nodes                               |
| `AstWalker.hpp`        | AST traversal utilities                         |
| `AstWalkerUtils.cpp`   | AST walker helper implementation                |
| `AstWalkerUtils.hpp`   | AST walker helper functions                     |
| `AstPrinter.cpp`       | AST pretty-printing implementation              |
| `AstPrinter.hpp`       | AST pretty-printing                             |
| `AstPrint_Expr.cpp`    | Expression printing                             |
| `AstPrint_Stmt.cpp`    | Statement printing                              |

## Semantic Analysis (`sem/`)

| File                                   | Purpose                              |
|----------------------------------------|--------------------------------------|
| `SemanticAnalyzer.cpp`                 | Main semantic analyzer impl          |
| `SemanticAnalyzer.hpp`                 | Main semantic analyzer               |
| `SemanticAnalyzer_Internal.hpp`        | Internal analyzer helpers            |
| `SemanticAnalyzer_Exprs.cpp`           | Expression analysis                  |
| `SemanticAnalyzer_Stmts.cpp`           | Statement analysis dispatch          |
| `SemanticAnalyzer_Stmts_Control.cpp`   | Control flow analysis impl           |
| `SemanticAnalyzer_Stmts_Control.hpp`   | Control flow analysis                |
| `SemanticAnalyzer_Stmts_IO.cpp`        | I/O statement analysis impl          |
| `SemanticAnalyzer_Stmts_IO.hpp`        | I/O statement analysis               |
| `SemanticAnalyzer_Stmts_Runtime.cpp`   | Runtime statement analysis impl      |
| `SemanticAnalyzer_Stmts_Runtime.hpp`   | Runtime statement analysis           |
| `SemanticAnalyzer_Stmts_Shared.cpp`    | Shared statement analysis impl       |
| `SemanticAnalyzer_Stmts_Shared.hpp`    | Shared statement analysis            |
| `SemanticAnalyzer_Procs.cpp`           | Procedure analysis                   |
| `SemanticAnalyzer_Builtins.cpp`        | Builtin function analysis            |
| `SemanticAnalyzer_Namespace.cpp`       | Namespace resolution                 |
| `SemanticDiagnostics.cpp`              | Semantic error reporting impl        |
| `SemanticDiagnostics.hpp`              | Semantic error reporting             |
| `SemanticDiagUtil.hpp`                 | Semantic diagnostic utilities        |
| `sem/TypeRegistry.cpp`                 | Type registration implementation     |
| `sem/TypeRegistry.hpp`                 | Type registration and lookup         |
| `sem/TypeResolver.cpp`                 | Type resolution implementation       |
| `sem/TypeResolver.hpp`                 | Type resolution                      |
| `sem/NamespaceRegistry.cpp`            | Namespace management impl            |
| `sem/NamespaceRegistry.hpp`            | Namespace management                 |
| `sem/UsingContext.cpp`                 | USING directive implementation       |
| `sem/UsingContext.hpp`                 | USING directive handling             |
| `sem/OverloadResolution.cpp`           | Method overload resolution impl      |
| `sem/OverloadResolution.hpp`           | Method overload resolution           |
| `sem/RegistryBuilder.cpp`              | Registry construction impl           |
| `sem/RegistryBuilder.hpp`              | Registry construction                |
| `sem/RuntimeMethodIndex.cpp`           | Runtime method lookup impl           |
| `sem/RuntimeMethodIndex.hpp`           | Runtime method lookup                |
| `sem/RuntimePropertyIndex.cpp`         | Runtime property lookup impl         |
| `sem/RuntimePropertyIndex.hpp`         | Runtime property lookup              |
| `sem/Check_Common.hpp`                 | Common semantic check utilities      |
| `sem/Check_Expr_Array.cpp`             | Array expression semantic checks     |
| `sem/Check_Expr_Binary.cpp`            | Binary expression semantic checks    |
| `sem/Check_Expr_Call.cpp`              | Call expression semantic checks      |
| `sem/Check_Expr_Unary.cpp`             | Unary expression semantic checks     |
| `sem/Check_Expr_Var.cpp`               | Variable expression semantic checks  |
| `sem/Check_If.cpp`                     | IF statement checks                  |
| `sem/Check_Jumps.cpp`                  | Jump statement checks                |
| `sem/Check_Loops.cpp`                  | Loop statement checks                |
| `sem/Check_Select.cpp`                 | SELECT CASE checks                   |
| `sem/Check_SelectDetail.hpp`           | SELECT CASE check details            |

## OOP Support

| File                              | Purpose                                  |
|-----------------------------------|------------------------------------------|
| `Semantic_OOP.cpp`                | OOP semantic analysis impl               |
| `Semantic_OOP.hpp`                | OOP semantic analysis and index building |
| `Semantic_OOP_Builder.cpp`        | OOP builder utilities                    |
| `Semantic_OOP_Helpers.cpp`        | OOP helper functions                     |
| `detail/Semantic_OOP_Internal.hpp`| Internal OOP semantic declarations       |
| `OopIndex.cpp`                    | Class/interface metadata impl            |
| `OopIndex.hpp`                    | Class/interface metadata storage         |
| `OopLoweringContext.cpp`          | OOP lowering state impl                  |
| `OopLoweringContext.hpp`          | OOP lowering state management            |
| `NameMangler_OOP.hpp`             | OOP name mangling                        |

## OOP Lowering (`lower/oop/`)

| File                                | Purpose                            |
|-------------------------------------|------------------------------------|
| `lower/oop/Lower_OOP_Internal.hpp`  | Internal OOP lowering declarations |
| `lower/oop/Lower_OOP_Alloc.cpp`     | Object allocation lowering         |
| `lower/oop/Lower_OOP_Emit.cpp`      | OOP IL emission                    |
| `lower/oop/Lower_OOP_Expr.cpp`      | OOP expression lowering            |
| `lower/oop/Lower_OOP_Helpers.cpp`   | OOP lowering utilities             |
| `lower/oop/Lower_OOP_MemberAccess.cpp`| Member access lowering           |
| `lower/oop/Lower_OOP_MethodCall.cpp`| Method call lowering               |
| `lower/oop/Lower_OOP_RuntimeHelpers.cpp`| OOP runtime helper lowering    |
| `lower/oop/Lower_OOP_RuntimeHelpers.hpp`| OOP runtime helper declarations|
| `lower/oop/Lower_OOP_Scan.cpp`      | OOP pre-scan phase                 |
| `lower/oop/Lower_OOP_Stmt.cpp`      | OOP statement lowering             |
| `lower/oop/MethodDispatchHelpers.cpp`| Method dispatch implementation    |
| `lower/oop/MethodDispatchHelpers.hpp`| Method dispatch utilities         |

## IL Lowering Core

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `Lowerer.cpp`                     | Main lowering coordinator impl       |
| `Lowerer.hpp`                     | Main lowering coordinator            |
| `LowererContext.hpp`              | Lowering context and state           |
| `LowererFwd.hpp`                  | Forward declarations                 |
| `LowererTypes.hpp`                | Lowerer type definitions             |
| `LowererRuntimeHelpers.hpp`       | Runtime helper declarations          |
| `LoweringContext.cpp`             | Extended lowering context impl       |
| `LoweringContext.hpp`             | Extended lowering context            |
| `LoweringPipeline.hpp`            | Lowering pipeline definition         |

## Procedure Lowering

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `Lowerer_Procedure.cpp`           | Procedure lowering                   |
| `Lowerer_Procedure_Context.cpp`   | Procedure context management         |
| `Lowerer_Procedure_Emit.cpp`      | Procedure emission                   |
| `Lowerer_Procedure_Signatures.cpp`| Procedure signature handling         |
| `Lowerer_Procedure_Skeleton.cpp`  | Procedure skeleton generation        |
| `Lowerer_Procedure_Variables.cpp` | Procedure variable handling          |
| `Lowerer_Program.cpp`             | Program-level lowering               |
| `Lowerer_Statement.cpp`           | Statement lowering dispatch          |

## Expression Lowering

| File                        | Purpose                              |
|-----------------------------|--------------------------------------|
| `LowerExpr.cpp`             | Expression lowering                  |
| `LowerExprBuiltin.cpp`      | Builtin expression lowering impl     |
| `LowerExprBuiltin.hpp`      | Builtin expression lowering          |
| `LowerExprLogical.cpp`      | Logical expression lowering impl     |
| `LowerExprLogical.hpp`      | Logical expression lowering          |
| `LowerExprNumeric.cpp`      | Numeric expression lowering impl     |
| `LowerExprNumeric.hpp`      | Numeric expression lowering          |

## Statement Lowering

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `ControlStatementLowerer.cpp`     | Control statement lowering impl      |
| `ControlStatementLowerer.hpp`     | Control statement lowering           |
| `IoStatementLowerer.cpp`          | I/O statement lowering impl          |
| `IoStatementLowerer.hpp`          | I/O statement lowering               |
| `RuntimeStatementLowerer.cpp`     | Runtime statement lowering impl      |
| `RuntimeStatementLowerer.hpp`     | Runtime statement lowering           |
| `RuntimeStatementLowerer_Assign.cpp`| Assignment lowering                |
| `RuntimeStatementLowerer_Decl.cpp`| Declaration lowering                 |
| `RuntimeStatementLowerer_Terminal.cpp`| Terminal I/O lowering            |
| `LowerStmt_Control.cpp`           | Control flow lowering impl           |
| `LowerStmt_Control.hpp`           | Control flow lowering                |
| `LowerStmt_Core.hpp`              | Core statement lowering              |
| `LowerStmt_IO.cpp`                | I/O statement lowering impl          |
| `LowerStmt_IO.hpp`                | I/O statement lowering               |
| `LowerStmt_Runtime.cpp`           | Runtime statement lowering impl      |
| `LowerStmt_Runtime.hpp`           | Runtime statement lowering           |
| `SelectCaseLowering.cpp`          | SELECT CASE lowering impl            |
| `SelectCaseLowering.hpp`          | SELECT CASE lowering                 |
| `SelectCaseRange.hpp`             | SELECT CASE range handling           |
| `SelectModel.cpp`                 | SELECT CASE model impl               |
| `SelectModel.hpp`                 | SELECT CASE model                    |
| `RuntimeCallHelpers.cpp`          | Runtime call utilities impl          |
| `RuntimeCallHelpers.hpp`          | Runtime call utilities               |
| `RuntimeHelperRequests.hpp`       | Runtime helper request types         |

## Lowering Submodules (`lower/`)

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `lower/Emitter.cpp`               | IL emitter implementation            |
| `lower/Emitter.hpp`               | IL emitter class                     |
| `lower/AstVisitor.hpp`            | Lowering AST visitor                 |
| `lower/Emit_Expr.cpp`             | Expression emission                  |
| `lower/Emit_Control.cpp`          | Control flow emission                |
| `lower/Emit_Builtin.cpp`          | Builtin emission                     |
| `lower/Emit_OOP.cpp`              | OOP emission                         |
| `lower/Lower_If.cpp`              | IF statement lowering                |
| `lower/Lower_Loops.cpp`           | Loop lowering                        |
| `lower/Lower_Switch.cpp`          | Switch/SELECT lowering               |
| `lower/Lower_TryCatch.cpp`        | Exception handling lowering          |
| `lower/Lower_Expr_NumericClassifier.cpp`| Numeric type classification     |
| `lower/Lowerer_Expr.cpp`          | Expression lowerer                   |
| `lower/Lowerer_Stmt.cpp`          | Statement lowerer                    |
| `lower/Lowerer_Errors.cpp`        | Error lowering                       |
| `lower/Scan_ExprTypes.cpp`        | Expression type scanning             |
| `lower/Scan_RuntimeNeeds.cpp`     | Runtime requirement scanning         |
| `lower/BuiltinCommon.cpp`         | Common builtin lowering impl         |
| `lower/BuiltinCommon.hpp`         | Common builtin lowering              |

## Lowering Common (`lower/common/`)

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `lower/common/BuiltinUtils.cpp`   | Builtin utilities impl               |
| `lower/common/BuiltinUtils.hpp`   | Builtin utilities                    |
| `lower/common/CommonLowering.cpp` | Common lowering utilities impl       |
| `lower/common/CommonLowering.hpp` | Common lowering utilities            |

## Lowering Builtins (`lower/builtins/`)

| File                              | Purpose                              |
|-----------------------------------|--------------------------------------|
| `lower/builtins/Array.cpp`        | Array builtin lowering               |
| `lower/builtins/Common.cpp`       | Common builtin lowering              |
| `lower/builtins/IO.cpp`           | I/O builtin lowering                 |
| `lower/builtins/Math.cpp`         | Math builtin lowering                |
| `lower/builtins/Registrars.hpp`   | Builtin registrar declarations       |
| `lower/builtins/String.cpp`       | String builtin lowering              |

## Lowering Detail (`lower/detail/`)

| File                                | Purpose                            |
|-------------------------------------|------------------------------------|
| `lower/detail/LowererDetail.hpp`    | Internal lowerer declarations      |
| `lower/detail/ControlLoweringHelper.cpp`| Control flow lowering helpers  |
| `lower/detail/ExprLoweringHelper.cpp`| Expression lowering helpers       |
| `lower/detail/OopLoweringHelper.cpp`| OOP lowering helpers               |
| `lower/detail/RuntimeLoweringHelper.cpp`| Runtime call lowering helpers  |

## Emission Helpers

| File                 | Purpose                              |
|----------------------|--------------------------------------|
| `LowerEmit.cpp`      | IL emission helpers impl             |
| `LowerEmit.hpp`      | IL emission helpers                  |
| `LowerRuntime.cpp`   | Runtime call lowering impl           |
| `LowerRuntime.hpp`   | Runtime call lowering                |
| `LowerScan.cpp`      | Pre-lowering scan impl               |
| `LowerScan.hpp`      | Pre-lowering scan phase              |
| `EmitCommon.cpp`     | Common emission utilities impl       |
| `EmitCommon.hpp`     | Common emission utilities            |

## Builtins

| File                            | Purpose                              |
|---------------------------------|--------------------------------------|
| `BuiltinRegistry.cpp`           | Builtin function registry impl       |
| `BuiltinRegistry.hpp`           | Builtin function registry            |
| `Intrinsics.cpp`                | Intrinsic function impl              |
| `Intrinsics.hpp`                | Intrinsic function definitions       |
| `builtins/MathBuiltins.cpp`     | Math builtin implementations         |
| `builtins/MathBuiltins.hpp`     | Math builtin declarations            |
| `builtins/StringBuiltins.cpp`   | String builtin implementations       |
| `builtins/StringBuiltins.hpp`   | String builtin declarations          |

## Constant Folding (`constfold/`)

| File                          | Purpose                           |
|-------------------------------|-----------------------------------|
| `ConstFolder.cpp`             | Constant folding coordinator impl |
| `ConstFolder.hpp`             | Constant folding coordinator      |
| `constfold/Dispatch.cpp`      | Fold dispatch implementation      |
| `constfold/Dispatch.hpp`      | Fold dispatch logic               |
| `constfold/Value.hpp`         | Compile-time value representation |
| `constfold/ConstantUtils.hpp` | Constant utilities                |
| `constfold/FoldArith.cpp`     | Arithmetic folding                |
| `constfold/FoldBuiltins.cpp`  | Builtin function folding          |
| `constfold/FoldCasts.cpp`     | Cast folding                      |
| `constfold/FoldCompare.cpp`   | Comparison folding                |
| `constfold/FoldLogical.cpp`   | Logical folding                   |
| `constfold/FoldStrings.cpp`   | String folding                    |

## Types

| File                        | Purpose                           |
|-----------------------------|-----------------------------------|
| `TypeRules.cpp`             | Type checking rules impl          |
| `TypeRules.hpp`             | Type checking rules               |
| `TypeSuffix.cpp`            | Type suffix handling impl         |
| `TypeSuffix.hpp`            | Type suffix handling (%, $, etc.) |
| `TypeCoercionEngine.cpp`    | Type coercion logic impl          |
| `TypeCoercionEngine.hpp`    | Type coercion logic               |
| `NumericRules.hpp`          | Numeric type rules                |
| `types/TypeMapping.cpp`     | BASIC to IL type mapping impl     |
| `types/TypeMapping.hpp`     | BASIC to IL type mapping          |
| `ILTypeUtils.cpp`           | IL type utilities impl            |
| `ILTypeUtils.hpp`           | IL type utilities                 |

## Diagnostics (`diag/`)

| File                 | Purpose                              |
|----------------------|--------------------------------------|
| `diag/BasicDiag.cpp` | BASIC-specific diagnostics           |

## Passes (`passes/`)

| File                        | Purpose                      |
|-----------------------------|------------------------------|
| `passes/CollectProcs.cpp`   | Procedure collection impl    |
| `passes/CollectProcs.hpp`   | Procedure collection pass    |

## Print Utilities (`print/`)

| File                          | Purpose                        |
|-------------------------------|--------------------------------|
| `print/Print_Stmt_Common.hpp` | Common statement print utils   |
| `print/Print_Stmt_Control.cpp`| Control statement printing     |
| `print/Print_Stmt_Decl.cpp`   | Declaration statement printing |
| `print/Print_Stmt_IO.cpp`     | I/O statement printing         |
| `print/Print_Stmt_Jump.cpp`   | Jump statement printing        |

## Utilities

| File                          | Purpose                      |
|-------------------------------|------------------------------|
| `NameMangler.cpp`             | Name mangling impl           |
| `NameMangler.hpp`             | Name mangling                |
| `IdentifierUtil.hpp`          | Identifier utilities         |
| `StringUtils.hpp`             | String utilities             |
| `LineUtils.hpp`               | Line number utilities        |
| `LocationScope.cpp`           | Source location scoping impl |
| `LocationScope.hpp`           | Source location scoping      |
| `ScopeTracker.hpp`            | Scope tracking               |
| `StatementSequencer.cpp`      | Statement sequencing impl    |
| `StatementSequencer.hpp`      | Statement sequencing         |
