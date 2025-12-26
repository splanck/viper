# CODEMAP: IL I/O

Text parsing and serialization (`src/il/io/`) for IL modules.

## Parser

| File                           | Purpose                                              |
|--------------------------------|------------------------------------------------------|
| `Parser.hpp/cpp`               | Top-level parser facade and entry point              |
| `ParserState.cpp`              | Mutable parser context constructor                   |
| `ParserUtil.cpp`               | Lexical helpers: trim, tokenize, parse literals      |
| `FunctionParser.cpp`           | Function body parsing: headers, blocks, instructions |
| `FunctionParser_Body.cpp`      | Function body block and instruction parsing          |
| `FunctionParser_Prototype.cpp` | Function prototype/signature parsing                 |
| `InstrParser.cpp`              | Individual instruction line parsing                  |
| `ModuleParser.cpp`             | Module-level directives: il, extern, global, func    |
| `TypeParser.cpp`               | Type mnemonic to Type object translation             |
| `OperandParser.cpp`            | Legacy monolithic operand parser                     |
| `OperandParse_Value.cpp`       | Value operand parsing dispatcher                     |
| `OperandParse_ValueDetail.cpp` | Token-to-Value decoding                              |
| `OperandParse_Label.cpp`       | Branch label operand parsing                         |
| `OperandParse_Type.cpp`        | Type literal operand parsing                         |
| `OperandParse_Const.cpp`       | Constant literal operand parsing                     |

## Serializer

| File                   | Purpose                                         |
|------------------------|-------------------------------------------------|
| `Serializer.hpp/cpp`   | Module to text emission with canonical ordering |
| `FormatUtils.cpp`      | Locale-independent integer/float formatting     |
| `StringEscape.hpp/cpp` | String escape/unescape for IL literals          |

## Internal Headers (`internal/io/`)

| File                 | Purpose                                                     |
|----------------------|-------------------------------------------------------------|
| `FunctionParser.hpp` | Function parser declarations                                |
| `InstrParser.hpp`    | Instruction parser declarations                             |
| `ModuleParser.hpp`   | Module parser declarations                                  |
| `OperandParser.hpp`  | Operand parser declarations                                 |
| `ParserState.hpp`    | Parser state: module, function, block refs, SSA bookkeeping |
| `ParserUtil.hpp`     | Lexical helper declarations                                 |
| `TypeParser.hpp`     | Type parser declarations                                    |
