# Bytecode VM Parity & Isolated Tests

**Status:** Verified (reachable & large, but under-isolated in tests)
**Area:** `src/bytecode/`, `src/tests/`
**Effort:** M
**Roadmap fit:** v0.2.x hardening

## Problem

The bytecode VM is a large, **actively-used** alternative execution engine (it is wired
into `cmd_run`, `cmd_bench`, `vm_executor`, and both REPL adapters), comprising
`BytecodeVM.cpp` (~6K LOC), a threaded-dispatch variant, and a sizable
`BytecodeCompiler` (IL → bytecode). But:

- There is **no dedicated `src/tests/bytecode/` suite**.
- There is **no IL↔bytecode parity test** asserting the bytecode VM produces the same
  observable results as the IL VM for the same program.

Two independent execution engines without a parity guard is a semantic-drift risk: a
bytecode-compiler or dispatch change could silently diverge from IL-VM semantics.

## Current state (verified)

- Bytecode VM reachable from tools/REPL; runs alongside the IL VM in some tests but with
  no isolation suite.
- (Agent-reported, **confirm during implementation**) the bytecode VM may define its own
  `TrapKind` values distinct from the IL VM's — if so, the parity harness must normalize
  trap identity, and the mismatch is itself worth fixing.

## Goal & scope

- **In:** A dedicated bytecode test directory and a **differential parity harness**:
  same program/IL corpus → run on (a) the IL VM and (b) the bytecode VM → assert
  identical stdout, return value, and trap kind.
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

1. Create `src/tests/bytecode/` + CMake registration (labels `vm`, `bytecode`).
2. Implement the differential harness over the shared corpus (arithmetic, control flow,
   calls/recursion, strings, exceptions/traps, OOP dispatch).
3. Add a **trap-kind alignment** test: assert the bytecode VM's trap identity maps 1:1 to
   the IL VM's for each trap-producing program; if values differ, unify them (single
   source of truth for trap kinds) as part of this work.
4. (Optional) IL→bytecode→IL/disasm round-trip structural test.

## Tests

- The differential harness **is** the core test; seed with ≥20 representative programs
  plus every trap kind.
- Negative: a deliberately divergent stub must make the harness fail (self-test the gate).
- Keep runs `RUN_SERIAL` with a sane timeout, matching the existing differential tests.

## Documentation

- Update `docs/testing.md` with the new `bytecode` label, the parity rationale, and the
  shared-corpus location.
- Update `docs/BYTECODE_VM_DESIGN.md` to state the parity guarantee and the trap-kind
  single-source-of-truth decision.
- Note the engine's role (alternative execution path) in `docs/architecture.md` if not
  already explicit.

## Cross-platform

Pure execution comparison; runs on all hosts. No platform code.

## Risks / open questions

- **Trap-kind divergence** (if confirmed) may require a small refactor to a shared enum —
  scope it into this work rather than papering over it in the harness.
- **Nondeterministic programs** (time, RNG) must be excluded from or seeded in the corpus
  so both engines are comparable.
