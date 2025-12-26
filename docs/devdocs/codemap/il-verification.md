# CODEMAP: IL Verification

IL verifier (`src/il/verify/`) enforcing spec compliance.

## Main Verifier

| File                    | Purpose                                            |
|-------------------------|----------------------------------------------------|
| `Verifier.hpp/cpp`      | Top-level module verification entry point          |
| `VerifyCtx.hpp`         | Verification context: maps, type inference, config |
| `VerifierTable.hpp/cpp` | Consolidated rule table mapping opcodes to checks  |

## Function Verification

| File                         | Purpose                                   |
|------------------------------|-------------------------------------------|
| `FunctionVerifier.hpp/cpp`   | Per-function verification coordinator     |
| `ControlFlowChecker.hpp/cpp` | Block structure and terminator validation |
| `BranchVerifier.hpp/cpp`     | Branch successor and argument validation  |
| `BlockMap.hpp`               | Block name to index mapping utilities     |

## Instruction Checking

| File                                | Purpose                                                  |
|-------------------------------------|----------------------------------------------------------|
| `InstructionChecker.hpp/cpp`        | Main instruction validation interface                    |
| `InstructionChecker_Arithmetic.cpp` | Arithmetic/comparison instruction checks                 |
| `InstructionChecker_Memory.cpp`     | Memory instruction validation (alloca, load, store, gep) |
| `InstructionChecker_Runtime.cpp`    | Runtime call site validation                             |
| `InstructionCheckerShared.hpp`      | Shared checker utilities                                 |
| `InstructionCheckUtils.hpp/cpp`     | Snippet rendering and support functions                  |
| `InstructionStrategies.hpp/cpp`     | Per-opcode routing strategies                            |

## Type Checking

| File                          | Purpose                               |
|-------------------------------|---------------------------------------|
| `TypeInference.hpp/cpp`       | Operand type inference and validation |
| `OperandCountChecker.hpp/cpp` | Per-opcode operand count enforcement  |
| `OperandTypeChecker.hpp/cpp`  | Operand type category validation      |
| `ResultTypeChecker.hpp/cpp`   | Result type contract validation       |

## Declaration Verification

| File                     | Purpose                                        |
|--------------------------|------------------------------------------------|
| `ExternVerifier.hpp/cpp` | Extern uniqueness and ABI signature validation |
| `GlobalVerifier.hpp/cpp` | Global uniqueness validation                   |

## Exception Handling

| File                               | Purpose                                            |
|------------------------------------|----------------------------------------------------|
| `EhVerifier.hpp/cpp`               | EH verification front door                         |
| `EhModel.hpp/cpp`                  | Exception handling construct model                 |
| `EhChecks.hpp/cpp`                 | EH rule validation (balanced try/catch, transfers) |
| `ExceptionHandlerAnalysis.hpp/cpp` | Block EH metadata annotation                       |

## Diagnostics

| File                       | Purpose                          |
|----------------------------|----------------------------------|
| `DiagSink.hpp/cpp`         | Collecting diagnostic sink       |
| `DiagFormat.hpp/cpp`       | Diagnostic formatting helpers    |
| `Rule.hpp`                 | Rule descriptors and tags        |
| `SpecTables.hpp`           | Spec-derived verification tables |
| `generated/SpecTables.cpp` | Generated spec tables            |
