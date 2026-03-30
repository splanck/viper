# POLISH-05: Add Diagnostics to Silent Lowering Failures

## Context
**Validated: 41 `return false`** sites in `InstrLowering.cpp` (1,821 LOC)
where opcode lowering fails silently. No diagnostic infrastructure exists in
the codegen lowering layer (unlike the frontend which has `DiagnosticEngine`).

The function signature provides: `const il::core::Instr &ins` (opcode +
operands), `LoweringContext &ctx`, and `MBasicBlock &out`. Source locations
are NOT directly available but can be extracted from `ins.loc`.

**Complexity: S** | **Priority: P2**

## Design

Add a static `warnLoweringFailed()` helper to `InstrLowering.cpp`:

```cpp
static void warnLoweringFailed(const il::core::Instr &ins,
                               const char *reason) {
    fprintf(stderr, "warning: AArch64: failed to lower %s",
            il::core::toString(ins.op));
    if (ins.loc.hasLine())
        fprintf(stderr, " at line %u", ins.loc.line);
    fprintf(stderr, ": %s\n", reason);
}
```

Systematically add before each `return false`:

| Lines | Context | Message |
|-------|---------|---------|
| 323 | Temp producer not found | "cannot find instruction producing temp %u" |
| 331, 334 | Nested materialization | "failed to materialize binary operand" |
| 414, 422 | Binary op emit | "unsupported binary operand combination" |
| 505, 546, 572, 583 | Load operations | "failed to materialize load address" |
| 615 | End of function | "unsupported Value::Kind %d" |
| 645 | Invalid call target | "call target is not a function or global" |
| 685 | Call arg materialization | "failed to materialize call argument %zu" |
| 792, 808, 819 | Division | "failed to materialize division operand" |
| 905-950 | Bounds check | "failed to materialize bounds check operand" |
| 1004-1030 | Signed remainder | "failed to materialize remainder operand" |

### Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/InstrLowering.cpp` | Add warnLoweringFailed at 41 sites |

Also fix the 3 `fprintf(stderr)` sites that already exist but use ad-hoc
formatting:
- `OpcodeDispatch.cpp:496` — ResumeSame/ResumeNext
- `InstrLowering.cpp:1662` — lowerCallWithArgs
- `LowerILToMIR.cpp:696` — unhandled opcode

## Documentation Updates
- `docs/codegen/aarch64.md` — Note diagnostic output format

## Verification
Trigger a lowering failure and verify warning appears with line number context.
