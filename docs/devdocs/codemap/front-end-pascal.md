# CODEMAP: Pascal Frontend

The Pascal frontend (`src/frontends/pascal/`) compiles Pascal source to IL.

## Core Infrastructure

| File                | Purpose                                    |
|---------------------|--------------------------------------------|
| `Compiler.hpp/cpp`  | Main compiler pipeline                     |

## Lexer and Parser

| File               | Purpose                                     |
|--------------------|---------------------------------------------|
| `Lexer.hpp/cpp`    | Tokenizer for Pascal source                 |
| `Parser.hpp/cpp`   | Recursive-descent parser core               |
| `Parser_Decl.cpp`  | Declaration parsing                         |
| `Parser_Expr.cpp`  | Expression parsing                          |
| `Parser_Stmt.cpp`  | Statement parsing                           |
| `Parser_Type.cpp`  | Type declaration parsing                    |
| `Parser_Unit.cpp`  | Unit/program structure parsing              |
| `Parser_OOP.cpp`   | OOP construct parsing (classes, methods)    |

## AST

| File           | Purpose                          |
|----------------|----------------------------------|
| `AST.hpp/cpp`  | AST node definitions and helpers |

## Semantic Analysis

| File                        | Purpose                           |
|-----------------------------|-----------------------------------|
| `SemanticAnalyzer.hpp/cpp`  | Main semantic analyzer            |
| `SemanticAnalyzer_Body.cpp` | Procedure/function body analysis  |
| `SemanticAnalyzer_Class.cpp`| Class and OOP analysis            |
| `SemanticAnalyzer_Decl.cpp` | Declaration analysis              |
| `SemanticAnalyzer_Expr.cpp` | Expression type checking          |
| `SemanticAnalyzer_Stmt.cpp` | Statement analysis                |
| `SemanticAnalyzer_Type.cpp` | Type resolution and checking      |
| `SemanticAnalyzer_Util.cpp` | Semantic analysis utilities       |

## IL Lowering

| File                     | Purpose                           |
|--------------------------|-----------------------------------|
| `Lowerer.hpp/cpp`        | Main lowering coordinator         |
| `Lowerer_Decl.cpp`       | Declaration lowering              |
| `Lowerer_Emit.cpp`       | IL emission helpers               |
| `Lowerer_Expr.cpp`       | Expression lowering               |
| `Lowerer_Expr_Access.cpp`| Member/array access lowering      |
| `Lowerer_Expr_Call.cpp`  | Procedure/function call lowering  |
| `Lowerer_Expr_Name.cpp`  | Name resolution lowering          |
| `Lowerer_Expr_Ops.cpp`   | Operator lowering                 |
| `Lowerer_OOP.cpp`        | OOP construct lowering            |
| `Lowerer_Stmt.cpp`       | Statement lowering                |

## Builtins

| File                      | Purpose                      |
|---------------------------|------------------------------|
| `BuiltinRegistry.hpp/cpp` | Built-in function registry   |
