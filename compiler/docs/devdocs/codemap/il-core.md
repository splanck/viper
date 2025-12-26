# CODEMAP: IL Core

Core IL data structures (`src/il/core/`) representing modules, functions, blocks, and instructions.

## Module Structure

| File                 | Purpose                                                       |
|----------------------|---------------------------------------------------------------|
| `Module.hpp/cpp`     | Module container: externs, globals, functions, version        |
| `Function.hpp/cpp`   | Function definition: signature, params, blocks, SSA names     |
| `BasicBlock.hpp/cpp` | Block: label, parameters, instructions, terminator flag       |
| `Instr.hpp/cpp`      | Instruction: opcode, result, operands, successors, source loc |

## Types and Values

| File            | Purpose                                                      |
|-----------------|--------------------------------------------------------------|
| `Type.hpp/cpp`  | Type wrapper and Kind enumeration (i64, f64, ptr, str, etc.) |
| `Value.hpp/cpp` | SSA value tagged union: temps, constants, globals, null      |
| `Param.hpp`     | Function/block parameter: type and optional name             |

## Declarations

| File             | Purpose                                           |
|------------------|---------------------------------------------------|
| `Extern.hpp/cpp` | Extern declaration: name, return type, parameters |
| `Global.hpp/cpp` | Global variable/constant: name, type, initializer |

## Opcodes

| File                 | Purpose                                                |
|----------------------|--------------------------------------------------------|
| `Opcode.hpp`         | Central Opcode enumeration for all IL operations       |
| `OpcodeInfo.hpp/cpp` | Opcode metadata: operand counts, result arity, effects |
| `OpcodeNames.cpp`    | Opcode to/from mnemonic string conversion              |

## Forward Declarations

| File      | Purpose                                          |
|-----------|--------------------------------------------------|
| `fwd.hpp` | Forward declarations to minimize header coupling |
