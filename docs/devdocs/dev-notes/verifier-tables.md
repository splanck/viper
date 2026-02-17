# Verifier Opcode Table Updates

This note records the minimal steps required to introduce a new opcode so that the verifier's table-driven checks remain
accurate.

## 1. Extend the opcode metadata

Two files require updates when adding a new opcode:

**a) `src/il/core/Opcode.def`** — add the new opcode entry with its `OpcodeInfo` fields:

- **Operand `TypeCategory` values**: enumerate the expected category for each operand (up to `kMaxOperandCategories = 3`)
  so the verifier can statically match instruction operands.
- **Result `TypeCategory`**: if the opcode produces a value, assign its category. Use `TypeCategory::None` for the
  result type of opcodes without a result; pair it with `ResultArity::None` to indicate no result is produced.
- **Side-effect flag** (`hasSideEffects`): set to `true` if the instruction interacts with memory, control flow, or
  other observable state. This keeps the verifier's structural checks aligned with the opcode's semantics.
- **`numSuccessors` and `isTerminator`**: set for control-flow instructions that end a basic block.
- **`VMDispatch` category**: select the appropriate `VMDispatch` enumerator so the interpreter knows which handler to
  invoke (defined in `src/il/core/OpcodeInfo.hpp`).

**b) `src/il/verify/generated/SpecTables.cpp`** — add a corresponding `InstructionSpec` entry in declaration order:

- Mirror the `TypeCategory` operand/result fields from `Opcode.def`.
- Set `ResultArity` to `One`, `None`, or `Optional` as appropriate.
- Choose the `VerifyStrategy` that matches the opcode's semantic requirements (see `SpecTables.hpp`). Use
  `VerifyStrategy::Default` when the table-driven checks are sufficient. Use `VerifyStrategy::Reject` with an
  explanatory message for opcodes the verifier should never accept in user IL.

No additional files need to change when the opcode's validation logic is fully captured by these table entries.

## 2. Only specialize the verifier when semantics demand it

The table-driven verifier already enforces operand/result categories and generic structural rules. Add code in
`InstructionChecker` (in `src/il/verify/InstructionChecker.cpp` and related files) only if the opcode has semantic
requirements that cannot be expressed via the static metadata — for example, if operands must reference the same block,
or if the opcode interacts with features not represented in `TypeCategory`.

Keep any such specialization narrowly scoped to the opcode to avoid coupling unrelated checks. Assign a non-`Default`
`VerifyStrategy` in `SpecTables.cpp` to route the instruction to the specialized checker.
