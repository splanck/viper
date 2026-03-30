# POLISH-03: Error Field Extraction via Runtime TLS Bridge

## Context
`ErrGetKind`, `ErrGetCode`, `ErrGetIp`, `ErrGetLine` all return hardcoded 0
in AArch64 native code at `OpcodeDispatch.cpp:474-484`. The Zia lowerer DOES
emit `ErrGetKind` for typed catch handlers (`Lowerer_Stmt_EH.cpp:214`), so
this affects real programs that use typed catch like `catch(e: DivideByZero)`.

**Validated:** Only the MESSAGE is currently stored in thread-local storage
(`rt_io.c:70`). Trap kind and code are NOT stored — they exist only in the
`VmError` struct which is VM-only.

**Complexity: M** | **Priority: P1**

## Design

### Step 1: Add trap field storage to runtime

**File: `src/runtime/core/rt_error.c`** (where `rt_throw_msg_set/get` already live)

Add thread-local storage for trap kind and code:
```c
static _Thread_local int32_t tls_trap_kind = 0;
static _Thread_local int32_t tls_trap_code = 0;
static _Thread_local int32_t tls_trap_line = -1;

void rt_trap_fields_set(int32_t kind, int32_t code, int32_t line) {
    tls_trap_kind = kind;
    tls_trap_code = code;
    tls_trap_line = line;
}

int64_t rt_trap_get_kind(void) { return (int64_t)tls_trap_kind; }
int64_t rt_trap_get_code(void) { return (int64_t)tls_trap_code; }
int64_t rt_trap_get_line(void) { return (int64_t)tls_trap_line; }
```

### Step 2: Store trap fields when raising

**File: `src/runtime/core/rt_io.c`** — In `rt_trap()` at line 141:

Before the longjmp, store the trap kind. Since `rt_trap` only receives a
message string (not a kind enum), we need a separate function:

```c
void rt_trap_with_kind(int32_t kind, int32_t code, const char *msg) {
    rt_trap_fields_set(kind, code, -1);
    rt_trap(msg);  // This does the longjmp
}
```

Or: modify the Zia lowerer to emit `call rt_trap_fields_set` before the
`trap` instruction, similar to how `rt_throw_msg_set` is called before `trap`.

### Step 3: Register runtime functions

**File: `src/il/runtime/runtime.def`**
```
RT_FUNC(TrapFieldsSet, rt_trap_fields_set, "Viper.Error.SetTrapFields", "void(i32,i32,i32)")
RT_FUNC(TrapGetKind,   rt_trap_get_kind,   "Viper.Error.GetTrapKind",   "i64()")
RT_FUNC(TrapGetCode,   rt_trap_get_code,   "Viper.Error.GetTrapCode",   "i64()")
RT_FUNC(TrapGetLine,   rt_trap_get_line,   "Viper.Error.GetTrapLine",   "i64()")
```

### Step 4: Update AArch64 backend

**File: `src/codegen/aarch64/OpcodeDispatch.cpp:474-484`**

Replace hardcoded 0 with calls to the runtime accessors:
```cpp
case Opcode::ErrGetKind: {
    const uint16_t dst = ctx.nextVRegId++;
    if (ins.result) ctx.tempVReg[*ins.result] = dst;
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap_get_kind")}});
    bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
        {MOperand::vregOp(RegClass::GPR, dst),
         MOperand::regOp(PhysReg::X0)}});
    return true;
}
// Same pattern for ErrGetCode, ErrGetLine
```

### Step 5: Update Zia lowerer for native code path

**File: `src/frontends/zia/Lowerer_Stmt_EH.cpp`**

Before emitting `trap` in `lowerThrowStmt`, also emit a call to
`Viper.Error.SetTrapFields` with the appropriate kind and code.
For system traps (division by zero), the runtime's trap handler
should set the fields automatically.

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/core/rt_error.c` | Add TLS fields + set/get functions |
| `src/runtime/core/rt_error.h` | Declare new functions |
| `src/il/runtime/runtime.def` | Register 4 new runtime functions |
| `src/codegen/aarch64/OpcodeDispatch.cpp:474-484` | Replace MovRI(0) with bl rt_trap_get_* |
| `src/frontends/zia/Lowerer_Stmt_EH.cpp` | Emit SetTrapFields before trap |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md` — Note error field extraction
- `docs/codegen/aarch64.md` — Document runtime bridge approach

## Verification
Native-compiled program with typed catch:
```zia
try { var x = 10 / 0; }
catch(e: DivideByZero) { Say("Division by zero!"); }
```
Verify the typed catch correctly dispatches (currently always falls through
because ErrGetKind returns 0).
