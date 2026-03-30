# POLISH-02: Forward Trap Messages in Native Code

## Context
`OpcodeDispatch.cpp:465-469` emits `bl rt_trap` for ALL trap variants
(Trap, TrapKind, TrapErr) with NO argument passing. `rt_trap(const char *msg)`
at `rt_io.c:141` accepts a message parameter, but native code always passes
nothing (equivalent to `rt_trap(NULL)`), producing "Trap" as the diagnostic.

The Zia lowerer already emits `call Viper.Error.SetThrowMsg` before `trap`
in IL (from the BUG-ZIA-003 fix), so for user `throw "msg"`, the message IS
stored in TLS. But for `TrapErr` (which carries the message as an operand),
the native backend ignores it.

**Complexity: S** | **Priority: P1**

## Current Code (Validated)

```cpp
// OpcodeDispatch.cpp:465-469
case Opcode::Trap:
case Opcode::TrapKind:
case Opcode::TrapErr:
    bbOut().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
    return true;
```

`rt_trap` signature at `rt_io.c:141`:
```c
void rt_trap(const char *msg);
```

When `msg` is NULL, the trap message is generic "Trap".
When `msg` is non-NULL, the message is stored in `rt_trap_error_[512]`.

## Design

Split the combined case into per-opcode handling:

### Trap (bare) — Keep as-is
```cpp
case Opcode::Trap:
    // No message available — pass NULL
    bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
        {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap")}});
    return true;
```

### TrapErr — Forward the message operand
TrapErr has 2 operands per `Opcode.def:1214-1230`:
- Operand 0: I32 error code
- Operand 1: Str message text

```cpp
case Opcode::TrapErr: {
    // Materialize message string (operand 1) into a vreg
    if (ins.operands.size() > 1) {
        uint16_t msgVreg = 0;
        RegClass msgCls = RegClass::GPR;
        if (materializeValueToVReg(ins.operands[1], bbIn, ctx.ti, ctx.fb,
                                   bbOut(), ctx.tempVReg, ctx.tempRegClass,
                                   ctx.nextVRegId, msgVreg, msgCls)) {
            // Move to x0 (first argument register per AAPCS64)
            bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
                {MOperand::regOp(PhysReg::X0),
                 MOperand::vregOp(RegClass::GPR, msgVreg)}});
        } else {
            // Fallback: pass NULL if materialization fails
            bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
                {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
        }
    } else {
        bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
            {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    }
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap")}});
    return true;
}
```

Note: `rt_trap` expects `const char *` (C string), but the Str operand is
an `rt_string` (Viper string pointer). Need to call `rt_string_to_cstr()`
or pass the raw pointer (since `rt_string` data starts at `payload + header`).
**Simpler approach:** Call `rt_trap` with the rt_string pointer — `rt_trap`
will format the first few bytes as the message. OR: change `rt_trap` to also
accept `rt_string` via a new overload `rt_trap_str(rt_string msg)`.

### TrapKind — Pass trap kind name as message
```cpp
case Opcode::TrapKind:
    // TrapKind has no useful message; pass NULL
    bbOut().instrs.push_back(MInstr{MOpcode::MovRI,
        {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap")}});
    return true;
```

## Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/OpcodeDispatch.cpp:465-469` | Split into per-opcode cases with arg passing |
| `src/runtime/core/rt_io.c` (optional) | Add `rt_trap_str(rt_string)` for Viper string messages |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md` — Note trap message forwarding

## Verification
Compile native executable with `throw "message"`. Run and verify message
appears in error output instead of generic "Trap".
