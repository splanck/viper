# CODEMAP: Pascal Frontend

Pascal compiler frontend (`src/frontends/pascal/`) translating Pascal source to Viper IL.

Last updated: 2026-01-15

## Overview

- **Total source files**: 39 (.cpp/.hpp)
- **Subdirectories**: lower/, sem/

## Compiler Driver

| File           | Purpose                               |
|----------------|---------------------------------------|
| `Compiler.cpp` | Top-level compile implementation      |
| `Compiler.hpp` | Top-level compile entry point         |

## Lexer

| File        | Purpose                             |
|-------------|-------------------------------------|
| `Lexer.cpp` | Tokenizer implementation            |
| `Lexer.hpp` | Tokenizer for Pascal source         |

## Parser

| File              | Purpose                            |
|-------------------|------------------------------------|
| `Parser.cpp`      | Main parser implementation         |
| `Parser.hpp`      | Main parser interface and state    |
| `Parser_Decl.cpp` | Declaration parsing (var, const)   |
| `Parser_Expr.cpp` | Expression parsing                 |
| `Parser_OOP.cpp`  | Class and object parsing           |
| `Parser_Stmt.cpp` | Statement parsing                  |
| `Parser_Type.cpp` | Type annotation parsing            |
| `Parser_Unit.cpp` | Unit (program/module) parsing      |

## AST

| File      | Purpose                                 |
|-----------|-----------------------------------------|
| `AST.cpp` | AST node implementation                 |
| `AST.hpp` | AST node types and utilities            |

## Semantic Analysis

| File                         | Purpose                             |
|------------------------------|-------------------------------------|
| `SemanticAnalyzer.cpp`       | Main semantic analyzer impl         |
| `SemanticAnalyzer.hpp`       | Main semantic analyzer interface    |
| `SemanticAnalyzer_Body.cpp`  | Function body analysis              |
| `SemanticAnalyzer_Class.cpp` | Class/object type analysis          |
| `SemanticAnalyzer_Decl.cpp`  | Declaration analysis                |
| `SemanticAnalyzer_Expr.cpp`  | Expression type checking            |
| `SemanticAnalyzer_Stmt.cpp`  | Statement analysis                  |
| `SemanticAnalyzer_Type.cpp`  | Type resolution and checking        |
| `SemanticAnalyzer_Util.cpp`  | Utility functions                   |

## Type Definitions (`sem/`)

| File            | Purpose                               |
|-----------------|---------------------------------------|
| `sem/Types.hpp` | Pascal type system definitions        |
| `sem/OOPTypes.hpp`| Object-oriented type definitions    |

## IL Lowering

| File                      | Purpose                              |
|---------------------------|--------------------------------------|
| `Lowerer.cpp`             | Main lowering implementation         |
| `Lowerer.hpp`             | Main lowering interface              |
| `Lowerer_Decl.cpp`        | Declaration lowering                 |
| `Lowerer_Emit.cpp`        | IL instruction emission              |
| `Lowerer_Expr.cpp`        | Expression lowering coordinator      |
| `Lowerer_Expr_Access.cpp` | Field/array access lowering          |
| `Lowerer_Expr_Call.cpp`   | Function/method call lowering        |
| `Lowerer_Expr_Name.cpp`   | Name resolution in expressions       |
| `Lowerer_Expr_Ops.cpp`    | Operator expression lowering         |
| `Lowerer_OOP.cpp`         | OOP feature lowering                 |
| `Lowerer_Stmt.cpp`        | Statement lowering                   |

## Lowering Support (`lower/`)

| File                  | Purpose                              |
|-----------------------|--------------------------------------|
| `lower/ClassLayout.hpp`| Class memory layout computation     |

## Builtin Registry

| File                   | Purpose                               |
|------------------------|---------------------------------------|
| `BuiltinRegistry.cpp`  | Registry implementation               |
| `BuiltinRegistry.hpp`  | Registry for Pascal builtin functions |
