# Bytecode VM Parity & Isolated Tests

**Status:** Completed — scalar parity remains, and a dedicated bytecode suite now
compares broader full-program observable behavior against the IL VM.
**Area:** `src/bytecode/`, `src/tests/`
**Effort:** M
**Roadmap fit:** v0.2.x hardening

## Problem

The bytecode VM is a large, **actively-used** alternative execution engine (it is wired
into `cmd_run`, `cmd_bench`, `vm_executor`, and both REPL adapters), comprising
`BytecodeVM.cpp`, a threaded-dispatch variant, and a sizable `BytecodeCompiler`
(IL to bytecode). It already has substantial direct unit tests and one important parity
suite, but the coverage is still narrow:

- There is **no dedicated `src/tests/bytecode/` suite**; bytecode tests are scattered
  under unit/conformance tests.
- `src/tests/conformance/VMBytecodeScalarSemanticsTests.cpp` already compares IL VM,
  bytecode switch dispatch, and bytecode threaded dispatch for scalar semantics and trap
  kinds, but it does not cover full-program observable behavior.
- Bytecode has its own `TrapKind`, but values 0-11 are intentionally aligned with
  `il::vm::TrapKind`; the plan should guard that alignment, not assume a mismatch.

Two independent execution engines with only scalar parity are still at risk of semantic
drift in strings, calls, runtime bridge behavior, EH/resume flow, OOP dispatch, globals,
threaded dispatch, and program stdout.

## Current state (verified)

- Bytecode VM reachable from tools/REPL and `vm_executor`.
- `src/tests/unit/test_bytecode_vm.cpp` directly tests bytecode execution, validation,
  EH, metadata rejection, strings, arrays, async/thread integration, and dispatch modes.
- `src/tests/conformance/VMBytecodeScalarSemanticsTests.cpp` already covers scalar
  semantics parity for both bytecode dispatch engines.
- `src/bytecode/BytecodeVM.hpp` documents bytecode trap values 0-11 as aligned with
  `il::vm::TrapKind`, with bytecode-only trap kinds at 100+.

## Goal & scope

- **In:** A dedicated bytecode test directory and a **full-program differential parity
  harness**: same IL/source corpus -> run on (a) the IL VM, (b) bytecode switch dispatch,
  and (c) bytecode threaded dispatch -> assert identical stdout, return value, trap kind,
  and relevant runtime side effects.
- **Out:** Rewriting the bytecode engine; performance work (covered separately).

## Design

Mirror the existing VM-vs-native differential pattern, but VM-vs-bytecode:

```
for prog in shared_corpus:
    (out_il,  rc_il,  trap_il)  = run_il_vm(prog)
    (out_bc,  rc_bc,  trap_bc)  = run_bytecode_vm(prog)
    assert (out_il, rc_il, normalize(trap_il)) == (out_bc, rc_bc, normalize(trap_bc))
```

Reuse the same shared IL corpus proposed in `aarch64-test-parity.md` so all three
engines (IL VM, native, bytecode) are validated over one input set. Add a structural
round-trip check if the bytecode can be disassembled (IL → bytecode → disasm → compare
structure) to catch lossy compilation.

## Implementation steps

1. Create `src/tests/bytecode/` + CMake registration (labels `vm`, `bytecode`) and move
   bytecode-specific tests there over time without renaming public ctest labels.
2. Keep `VMBytecodeScalarSemanticsTests.cpp` as the scalar oracle; add a broader
   differential harness over a shared corpus (arithmetic, control flow, calls/recursion,
   strings, globals, runtime bridge calls, exceptions/traps, OOP/interface dispatch).
3. Add a **trap-kind alignment** test: compile-time/static assertions for values 0-11 and
   runtime programs for each trap kind. Bytecode-only traps remain explicitly separate.
4. Add stdout and side-effect capture so parity covers observable behavior, not only
   return slots.
5. (Optional) IL->bytecode disassembly structural test if a stable bytecode disassembler
   exists; avoid snapshotting unstable internal encodings too early.

## Tests

- The existing scalar conformance suite stays green and moves under the `bytecode` label.
- The new differential harness is the core missing test; seed with >=20 representative
  programs plus every shared trap kind.
- Negative: a deliberately divergent stub must make the harness fail (self-test the gate).
- Keep runs `RUN_SERIAL` with a sane timeout, matching the existing differential tests.

## Documentation

- Update `docs/testing.md` with the new `bytecode` label, the parity rationale, and the
  shared-corpus location.
- Update `docs/BYTECODE_VM_DESIGN.md` to state the parity guarantee and the existing
  trap-kind alignment contract.
- Note the engine's role (alternative execution path) in `docs/architecture.md` if not
  already explicit.

## Cross-platform

Pure execution comparison; runs on all hosts. No platform code.

Completion notes:

- Added `src/tests/bytecode/` and registered `test_bytecode_full_program_parity`
  with `vm;bytecode;conformance` labels.
- Added shared success and trap IL corpus files under `src/tests/shared_corpus/il/`.
- The parity harness compares return values and runtime stdout across the IL VM,
  bytecode switch dispatch, and bytecode threaded dispatch.
- Added compile-time trap-kind alignment assertions for values 0-11 plus runtime
  trap programs for every shared trap kind; bytecode-only traps remain 100+.
- Updated `docs/testing.md`, `docs/BYTECODE_VM_DESIGN.md`, and
  `docs/architecture.md`.

## Risks / open questions

- **Trap-kind drift:** values are currently intentionally aligned; lock that down with
  static/runtime tests so future enum edits cannot silently diverge.
- **Nondeterministic programs** (time, RNG) must be excluded from or seeded in the corpus
  so both engines are comparable.
