# CODEMAP: IL Verification

IL verifier (`src/il/verify/`) enforcing spec compliance.

Last updated: 2026-02-17

## Overview

- **Total source files**: 49 (.hpp/.cpp)
- **Subdirectories**: generated/

## Main Verifier

| File                | Purpose                                            |
|---------------------|----------------------------------------------------|
| `Verifier.cpp`      | Top-level module verification implementation       |
| `Verifier.hpp`      | Top-level module verification entry point          |
| `VerifyCtx.hpp`     | Verification context: maps, type inference, config |
| `VerifierTable.cpp` | Consolidated rule table implementation             |
| `VerifierTable.hpp` | Consolidated rule table mapping opcodes to checks  |

## Function Verification

| File                     | Purpose                                   |
|--------------------------|-------------------------------------------|
| `FunctionVerifier.cpp`   | Per-function verification implementation  |
| `FunctionVerifier.hpp`   | Per-function verification coordinator     |
| `ControlFlowChecker.cpp` | Block structure validation implementation |
| `ControlFlowChecker.hpp` | Block structure and terminator validation |
| `BranchVerifier.cpp`     | Branch validation implementation          |
| `BranchVerifier.hpp`     | Branch successor and argument validation  |
| `BlockMap.hpp`           | Block name to index mapping utilities     |

## Instruction Checking

| File                               | Purpose                                                  |
|------------------------------------|----------------------------------------------------------|
| `InstructionChecker.cpp`           | Main instruction validation implementation               |
| `InstructionChecker.hpp`           | Main instruction validation interface                    |
| `InstructionChecker_Arithmetic.cpp`| Arithmetic/comparison instruction checks                 |
| `InstructionChecker_Memory.cpp`    | Memory instruction validation (alloca, load, store, gep) |
| `InstructionChecker_Runtime.cpp`   | Runtime call site validation                             |
| `InstructionCheckerShared.hpp`     | Shared checker utilities                                 |
| `InstructionCheckUtils.cpp`        | Snippet rendering implementation                         |
| `InstructionCheckUtils.hpp`        | Snippet rendering and support functions                  |
| `InstructionStrategies.cpp`        | Per-opcode routing implementation                        |
| `InstructionStrategies.hpp`        | Per-opcode routing strategies                            |

## Type Checking

| File                      | Purpose                                |
|---------------------------|----------------------------------------|
| `TypeInference.cpp`       | Operand type inference implementation  |
| `TypeInference.hpp`       | Operand type inference and validation  |
| `OperandCountChecker.cpp` | Operand count enforcement impl         |
| `OperandCountChecker.hpp` | Per-opcode operand count enforcement   |
| `OperandTypeChecker.cpp`  | Operand type validation impl           |
| `OperandTypeChecker.hpp`  | Operand type category validation       |
| `ResultTypeChecker.cpp`   | Result type validation implementation  |
| `ResultTypeChecker.hpp`   | Result type contract validation        |

## Declaration Verification

| File                 | Purpose                                        |
|----------------------|------------------------------------------------|
| `ExternVerifier.cpp` | Extern validation implementation               |
| `ExternVerifier.hpp` | Extern uniqueness and ABI signature validation |
| `GlobalVerifier.cpp` | Global validation implementation               |
| `GlobalVerifier.hpp` | Global uniqueness validation                   |

## Exception Handling

| File                            | Purpose                                            |
|---------------------------------|----------------------------------------------------|
| `EhVerifier.cpp`                | EH verification implementation                     |
| `EhVerifier.hpp`                | EH verification front door                         |
| `EhModel.cpp`                   | Exception handling construct model impl            |
| `EhModel.hpp`                   | Exception handling construct model                 |
| `EhChecks.cpp`                  | EH rule validation implementation                  |
| `EhChecks.hpp`                  | EH rule validation (balanced try/catch, transfers) |
| `ExceptionHandlerAnalysis.cpp`  | Block EH metadata annotation impl                  |
| `ExceptionHandlerAnalysis.hpp`  | Block EH metadata annotation                       |

## Diagnostics

| File                       | Purpose                          |
|----------------------------|----------------------------------|
| `DiagSink.cpp`             | Collecting diagnostic sink impl  |
| `DiagSink.hpp`             | Collecting diagnostic sink       |
| `DiagFormat.cpp`           | Diagnostic formatting impl       |
| `DiagFormat.hpp`           | Diagnostic formatting helpers    |
| `Rule.hpp`                 | Rule descriptors and tags        |
| `SpecTables.hpp`           | Spec-derived verification tables |
| `generated/SpecTables.cpp` | Generated spec tables            |
