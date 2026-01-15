# CODEMAP: Frontend Common

Shared utilities (`src/frontends/common/`) used across all language frontends.

Last updated: 2026-01-15

## Overview

- **Total source files**: 18 (.cpp/.hpp)

## Lexer Utilities

| File                | Purpose                                      |
|---------------------|----------------------------------------------|
| `LexerBase.hpp`     | Common lexer base class and utilities        |
| `CharUtils.hpp`     | Character classification helpers             |
| `KeywordTable.hpp`  | Keyword lookup table infrastructure          |
| `NumberParsing.hpp` | Numeric literal parsing                      |

## Parser Utilities

| File               | Purpose                                      |
|--------------------|----------------------------------------------|
| `BlockManager.hpp` | Block scope management                       |
| `LoopContext.hpp`  | Loop nesting context tracking                |
| `ScopeTracker.hpp` | Scope entry/exit tracking                    |
| `ExprResult.hpp`   | Expression parsing result type               |

## Type Utilities

| File             | Purpose                                       |
|------------------|-----------------------------------------------|
| `TypeUtils.hpp`  | Common type manipulation utilities            |

## Lowering Utilities

| File                     | Purpose                               |
|--------------------------|---------------------------------------|
| `InstructionEmitter.hpp` | Common IL instruction emission        |
| `ConstantFolding.hpp`    | Compile-time constant evaluation      |
| `NameMangler.hpp`        | Name mangling utilities               |

## String Utilities

| File             | Purpose                                       |
|------------------|-----------------------------------------------|
| `StringTable.hpp`| String interning for literals                 |
| `StringHash.hpp` | String hashing utilities                      |

## Diagnostics

| File                    | Purpose                                |
|-------------------------|----------------------------------------|
| `DiagnosticHelpers.hpp` | Common diagnostic formatting helpers   |

## Runtime Registry

| File                  | Purpose                                |
|-----------------------|----------------------------------------|
| `RuntimeRegistry.cpp` | Runtime function registry impl         |
| `RuntimeRegistry.hpp` | Runtime function signature registry    |

## General Utilities

| File          | Purpose                                          |
|---------------|--------------------------------------------------|
| `Common.hpp`  | Shared type definitions and forward declarations |
