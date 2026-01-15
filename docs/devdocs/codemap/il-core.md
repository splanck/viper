# CODEMAP: IL Core

Core IL data structures (`src/il/core/`) representing modules, functions, blocks, and instructions.

Last updated: 2026-01-15

## Overview

- **Total source files**: 22 (.hpp/.cpp)

## Module Structure

| File            | Purpose                                                       |
|-----------------|---------------------------------------------------------------|
| `Module.cpp`    | Module container implementation                               |
| `Module.hpp`    | Module container: externs, globals, functions, version        |
| `Function.cpp`  | Function definition implementation                            |
| `Function.hpp`  | Function definition: signature, params, blocks, SSA names     |
| `BasicBlock.cpp`| Block implementation                                          |
| `BasicBlock.hpp`| Block: label, parameters, instructions, terminator flag       |
| `Instr.cpp`     | Instruction implementation                                    |
| `Instr.hpp`     | Instruction: opcode, result, operands, successors, source loc |

## Types and Values

| File        | Purpose                                                      |
|-------------|--------------------------------------------------------------|
| `Type.cpp`  | Type wrapper implementation                                  |
| `Type.hpp`  | Type wrapper and Kind enumeration (i64, f64, ptr, str, etc.) |
| `Value.cpp` | SSA value implementation                                     |
| `Value.hpp` | SSA value tagged union: temps, constants, globals, null      |
| `Param.hpp` | Function/block parameter: type and optional name             |

## Declarations

| File         | Purpose                                           |
|--------------|---------------------------------------------------|
| `Extern.cpp` | Extern declaration implementation                 |
| `Extern.hpp` | Extern declaration: name, return type, parameters |
| `Global.cpp` | Global variable/constant implementation           |
| `Global.hpp` | Global variable/constant: name, type, initializer |

## Opcodes

| File             | Purpose                                                |
|------------------|--------------------------------------------------------|
| `Opcode.hpp`     | Central Opcode enumeration for all IL operations       |
| `OpcodeInfo.cpp` | Opcode metadata implementation                         |
| `OpcodeInfo.hpp` | Opcode metadata: operand counts, result arity, effects |
| `OpcodeNames.cpp`| Opcode to/from mnemonic string conversion              |

## Forward Declarations

| File      | Purpose                                          |
|-----------|--------------------------------------------------|
| `fwd.hpp` | Forward declarations to minimize header coupling |
