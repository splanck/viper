# CODEMAP: IL I/O

Text parsing and serialization (`src/il/io/`) for IL modules.

Last updated: 2026-01-15

## Overview

- **Total source files**: 29 (.hpp/.cpp)
- **Subdirectories**: ../internal/io/ (internal headers)

## Parser Core

| File                | Purpose                                              |
|---------------------|------------------------------------------------------|
| `Parser.cpp`        | Top-level parser implementation                      |
| `Parser.hpp`        | Top-level parser facade and entry point              |
| `ParserState.cpp`   | Mutable parser context constructor                   |
| `ParserUtil.cpp`    | Lexical helpers: trim, tokenize, parse literals      |
| `ModuleParser.cpp`  | Module-level directives: il, extern, global, func    |
| `TypeParser.cpp`    | Type mnemonic to Type object translation             |

## Function Parsing

| File                          | Purpose                                      |
|-------------------------------|----------------------------------------------|
| `FunctionParser.cpp`          | Function body parsing: headers, blocks       |
| `FunctionParser_Body.cpp`     | Function body block and instruction parsing  |
| `FunctionParser_Prototype.cpp`| Function prototype/signature parsing         |
| `InstrParser.cpp`             | Individual instruction line parsing          |

## Operand Parsing

| File                         | Purpose                          |
|------------------------------|----------------------------------|
| `OperandParser.cpp`          | Legacy monolithic operand parser |
| `OperandParse_Value.cpp`     | Value operand parsing dispatcher |
| `OperandParse_ValueDetail.cpp`| Token-to-Value decoding         |
| `OperandParse_Label.cpp`     | Branch label operand parsing     |
| `OperandParse_Type.cpp`      | Type literal operand parsing     |
| `OperandParse_Const.cpp`     | Constant literal operand parsing |

## Serializer

| File              | Purpose                                         |
|-------------------|-------------------------------------------------|
| `Serializer.cpp`  | Module to text emission implementation          |
| `Serializer.hpp`  | Module to text emission with canonical ordering |
| `FormatUtils.cpp` | Locale-independent integer/float formatting     |
| `StringEscape.cpp`| String escape/unescape implementation           |
| `StringEscape.hpp`| String escape/unescape for IL literals          |

## Internal Headers (`src/il/internal/io/`)

| File                                      | Purpose                                                     |
|-------------------------------------------|-------------------------------------------------------------|
| `internal/io/FunctionParser.hpp`          | Function parser declarations                                |
| `internal/io/FunctionParser_Internal.hpp` | Internal function parser helpers                            |
| `internal/io/InstrParser.hpp`             | Instruction parser declarations                             |
| `internal/io/ModuleParser.hpp`            | Module parser declarations                                  |
| `internal/io/OperandParser.hpp`           | Operand parser declarations                                 |
| `internal/io/ParserState.hpp`             | Parser state: module, function, block refs, SSA bookkeeping |
| `internal/io/ParserUtil.hpp`              | Lexical helper declarations                                 |
| `internal/io/TypeParser.hpp`              | Type parser declarations                                    |
