# CODEMAP: Zia Frontend

The Zia frontend (`src/frontends/zia/`) compiles Zia source to IL.

Zia is Viper's native language with entities, value types, generics, lambdas, and imports.

Last updated: 2026-01-15

## Overview

- **Total source files**: 50 (.hpp/.cpp)

## Core Infrastructure

| File               | Purpose                                        |
|--------------------|------------------------------------------------|
| `Compiler.cpp`     | Main compiler pipeline implementation          |
| `Compiler.hpp`     | Main compiler pipeline orchestration           |
| `Options.hpp`      | Compiler options (optimization levels)         |
| `RuntimeNames.hpp` | Runtime function name constants (~1400 funcs)  |

## Lexer and Parser

| File               | Purpose                                        |
|--------------------|------------------------------------------------|
| `Lexer.cpp`        | Tokenizer implementation                       |
| `Lexer.hpp`        | Tokenizer for Zia source                       |
| `Token.hpp`        | Token types and representation                 |
| `Parser.hpp`       | Recursive-descent parser core                  |
| `Parser_Decl.cpp`  | Declaration parsing (func, entity, value)      |
| `Parser_Expr.cpp`  | Expression parsing                             |
| `Parser_Stmt.cpp`  | Statement parsing                              |
| `Parser_Type.cpp`  | Type declaration parsing                       |
| `Parser_Tokens.cpp`| Token handling utilities                       |

## AST

| File            | Purpose                              |
|-----------------|--------------------------------------|
| `AST.hpp`       | Main AST include                     |
| `AST_Fwd.hpp`   | Forward declarations                 |
| `AST_Decl.hpp`  | Declaration node definitions         |
| `AST_Expr.hpp`  | Expression node definitions          |
| `AST_Stmt.hpp`  | Statement node definitions           |
| `AST_Types.hpp` | Type representation nodes            |

## Types

| File         | Purpose                                                          |
|--------------|------------------------------------------------------------------|
| `Types.cpp`  | Type system implementation                                       |
| `Types.hpp`  | Type system (primitives, entities, collections, optionals, funcs)|

## Semantic Analysis

| File              | Purpose                                    |
|-------------------|--------------------------------------------|
| `Sema.cpp`        | Main semantic analyzer implementation      |
| `Sema.hpp`        | Main semantic analyzer and symbol table    |
| `Sema_Decl.cpp`   | Declaration analysis                       |
| `Sema_Expr.cpp`   | Expression type checking                   |
| `Sema_Stmt.cpp`   | Statement analysis                         |
| `Sema_Expr_Advanced.cpp` | Advanced expression analysis (index, field, optional chain, etc.) |
| `Sema_Expr_Call.cpp`     | Call expression analysis and collection method resolution |
| `Sema_Expr_Ops.cpp`      | Operator expression analysis (binary, unary, ternary) |
| `Sema_Generics.cpp`      | Generic type and function support           |
| `Sema_Runtime.cpp`       | Runtime function registration               |
| `Sema_TypeResolution.cpp`| Type resolution and closure capture collection |

## Import Resolution

| File                 | Purpose                           |
|----------------------|-----------------------------------|
| `ImportResolver.cpp` | Module import resolution impl     |
| `ImportResolver.hpp` | Module import resolution          |
| `RuntimeAdapter.cpp` | Type conversion bridge impl (IL types to Zia types) |
| `RuntimeAdapter.hpp` | Type conversion utilities for RuntimeRegistry |

## IL Lowering

| File                          | Purpose                                     |
|-------------------------------|---------------------------------------------|
| `Lowerer.cpp`                 | Main lowering coordinator impl              |
| `Lowerer.hpp`                 | Main lowering coordinator and BlockManager  |
| `Lowerer_Decl.cpp`            | Declaration lowering (functions, types)     |
| `Lowerer_Emit.cpp`            | IL emission helpers (box/unbox, GEP, etc.)  |
| `Lowerer_Dispatch.cpp`        | Method dispatch lowering                    |
| `Lowerer_Expr.cpp`            | Expression lowering main dispatch           |
| `Lowerer_Expr_Binary.cpp`     | Binary operator lowering                    |
| `Lowerer_Expr_Call.cpp`       | Function/method call lowering               |
| `Lowerer_Expr_Complex.cpp`   | Complex expression lowering (field, new, coalesce, lambda, etc.) |
| `Lowerer_Expr_Method.cpp`    | Method call and type construction lowering  |
| `Lowerer_Expr_Collections.cpp`| List/Map/Tuple literal and index lowering   |
| `Lowerer_Expr_Literals.cpp`   | Literal expression lowering                 |
| `Lowerer_Expr_Match.cpp`      | Match expression lowering                   |
| `Lowerer_Stmt.cpp`            | Statement lowering                          |

## Key Features

- Full type inference with Unknown type propagation
- Entity and Value type support with vtables
- Generic types (List<T>, Map<K,V>)
- Lambda expressions with closures
- Pattern matching with match expressions
- Optional types with null safety
- Import system for multi-file projects
