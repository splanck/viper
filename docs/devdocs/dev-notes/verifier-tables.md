# Verifier Opcode Table Updates

This note records the minimal steps required to introduce a new opcode so that the verifier's table-driven checks remain
accurate.

## 1. Extend the opcode metadata

Update `Opcode.def` with the new opcode entry. Populate the following metadata fields:

- **Operand `TypeCategory` values**: enumerate the expected category for each operand so the verifier can statically
  match instruction operands.
- **Result `TypeCategory`**: if the opcode produces a value, assign its category so the verifier can type-check uses of
  the instruction result. Use `TypeCategory::Void` (or the existing sentinel) for opcodes without a result.
- **Side-effect flag**: set the side-effect metadata to communicate whether the instruction interacts with memory,
  control flow, or other observable state. This keeps the verifier's structural checks aligned with the opcode's
  semantics.

No additional files need to change when the opcode's validation logic is fully captured by these table entries.

## 2. Only specialize the verifier when semantics demand it

The table-driven verifier already enforces operand/result categories and generic structural rules. Add code in
`InstructionChecker` only if the opcode has semantic requirements that cannot be expressed via the static metadata—for
example, if operands must reference the same block or if the opcode interacts with features not represented in
`TypeCategory`.

Keep any such specialization narrowly scoped to the opcode to avoid coupling unrelated checks.
