# CODEMAP: Zia Frontend

The Zia frontend (`src/frontends/zia/`) compiles Zia source to IL.

Zia is Viper's native language with entities, generics, and imports.

## Core Infrastructure

| File                | Purpose                                    |
|---------------------|--------------------------------------------|
| `Compiler.hpp/cpp`  | Main compiler pipeline                     |
| `Options.hpp`       | Compiler options and configuration         |
| `RuntimeNames.hpp`  | Runtime function name constants            |

## Lexer and Parser

| File                | Purpose                                    |
|---------------------|--------------------------------------------|
| `Lexer.hpp/cpp`     | Tokenizer for Zia source             |
| `Token.hpp`         | Token types and representation             |
| `Parser.hpp`        | Recursive-descent parser core              |
| `Parser_Decl.cpp`   | Declaration parsing                        |
| `Parser_Expr.cpp`   | Expression parsing                         |
| `Parser_Stmt.cpp`   | Statement parsing                          |
| `Parser_Type.cpp`   | Type declaration parsing                   |
| `Parser_Tokens.cpp` | Token handling utilities                   |

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

| File            | Purpose                              |
|-----------------|--------------------------------------|
| `Types.hpp/cpp` | Type system implementation           |

## Semantic Analysis

| File             | Purpose                              |
|------------------|--------------------------------------|
| `Sema.hpp/cpp`   | Main semantic analyzer               |
| `Sema_Decl.cpp`  | Declaration analysis                 |
| `Sema_Expr.cpp`  | Expression type checking             |
| `Sema_Stmt.cpp`  | Statement analysis                   |

## Import Resolution

| File                      | Purpose                        |
|---------------------------|--------------------------------|
| `ImportResolver.hpp/cpp`  | Module import resolution       |

## IL Lowering

| File               | Purpose                              |
|--------------------|--------------------------------------|
| `Lowerer.hpp/cpp`  | Main lowering coordinator            |
| `Lowerer_Decl.cpp` | Declaration lowering                 |
| `Lowerer_Emit.cpp` | IL emission helpers                  |
| `Lowerer_Expr.cpp` | Expression lowering                  |
| `Lowerer_Stmt.cpp` | Statement lowering                   |
