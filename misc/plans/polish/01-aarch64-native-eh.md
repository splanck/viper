# POLISH-01: AArch64 Native Exception Handling

## Context
try/catch doesn't work in native AArch64 executables. The EH opcodes
(`EhPush`, `EhPop`, `EhEntry`) are no-ops in `OpcodeDispatch.cpp:458-461`,
`Trap` calls `bl rt_trap` with no arguments at line 465-469, and
`ResumeSame`/`ResumeNext` fail with `fprintf(stderr)` at lines 494-500.
Only the VM has working exception handling. This is the #1 platform gap.

**Complexity: L** | **Priority: P0**

## Existing Infrastructure (Validated)

The runtime ALREADY has a complete setjmp/longjmp recovery mechanism:

**File: `src/runtime/core/rt_io.c`** (NOT rt_trap.c as previously assumed)
- `rt_trap_set_recovery(jmp_buf *buf)` — line 75. Sets thread-local `rt_trap_recovery_`
- `rt_trap_clear_recovery()` — line 80. Clears recovery AND error buffer
- `rt_trap_get_error()` — line 86. Returns 512-byte thread-local error string
- `rt_trap(const char *msg)` — line 141. If recovery set → `longjmp(*rt_trap_recovery_, 1)`

**Thread-local storage (lines 69-71):**
```c
static _Thread_local jmp_buf *rt_trap_recovery_ = NULL;
static _Thread_local char rt_trap_error_[512] = "";
```

**Live usage example in `rt_threads.c:870-887`** — `safe_thread_entry()` uses
this exact pattern: `setjmp(recovery)` → call code → if trap → `longjmp` back.

## Design

### EhPush Implementation

When the lowerer sees `EhPush ^handler_label`:

1. **Allocate jmp_buf on stack** via `FrameBuilder::addLocal()` at lowering time
   - `sizeof(jmp_buf)` on Darwin AArch64 is platform-defined (typically ~160 bytes)
   - Must be computed at compile time: use `sizeof(jmp_buf)` from `<setjmp.h>`
   - **Cannot use dynamic alloca** — all frame slots must be known at function entry
   - Each try block needs its own jmp_buf slot

2. **Emit MIR sequence:**
```asm
; Compute address of jmp_buf slot
add x0, x29, #<jmp_buf_fp_offset>

; Install recovery point
bl rt_trap_set_recovery

; Call setjmp (returns 0 normally, non-zero on longjmp)
add x0, x29, #<jmp_buf_fp_offset>
bl _setjmp

; If non-zero (trap caught), branch to handler
cbnz w0, ^handler_label
```

3. **MIR lowering in OpcodeDispatch.cpp:458-461:**
```cpp
case Opcode::EhPush: {
    // Allocate jmp_buf frame slot (if not already allocated for this block)
    int jmpBufOffset = ctx.fb.addLocal(/*tempId=*/0xFFFF,
                                        /*sizeBytes=*/JMP_BUF_SIZE,
                                        /*alignBytes=*/16);

    uint16_t addrVReg = ctx.nextVRegId++;

    // add x0, x29, #offset (compute jmp_buf address)
    bbOut().instrs.push_back(MInstr{MOpcode::AddRI,
        {MOperand::vregOp(RegClass::GPR, addrVReg),
         MOperand::regOp(PhysReg::X29),
         MOperand::immOp(jmpBufOffset)}});

    // bl rt_trap_set_recovery (x0 = &jmp_buf)
    bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
        {MOperand::regOp(PhysReg::X0),
         MOperand::vregOp(RegClass::GPR, addrVReg)}});
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap_set_recovery")}});

    // bl _setjmp (x0 = &jmp_buf again — clobbered by previous call)
    bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
        {MOperand::regOp(PhysReg::X0),
         MOperand::vregOp(RegClass::GPR, addrVReg)}});
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("_setjmp")}});

    // cbnz w0, ^handler (if setjmp returned non-zero → trap caught)
    if (!ins.labels.empty()) {
        uint16_t retVReg = ctx.nextVRegId++;
        bbOut().instrs.push_back(MInstr{MOpcode::MovRR,
            {MOperand::vregOp(RegClass::GPR, retVReg),
             MOperand::regOp(PhysReg::X0)}});
        // Emit conditional branch to handler label
        // (handled via TerminatorLowering or inline CBR)
    }
    return true;
}
```

### EhPop Implementation

```cpp
case Opcode::EhPop:
    // bl rt_trap_clear_recovery
    bbOut().instrs.push_back(MInstr{MOpcode::Bl,
        {MOperand::labelOp("rt_trap_clear_recovery")}});
    return true;
```

### EhEntry — Remains no-op (marker instruction)

### Trap — Already calls `bl rt_trap` (correct behavior)

When `rt_trap_recovery_` is set (by EhPush), `rt_trap()` calls
`longjmp(*rt_trap_recovery_, 1)` which jumps back to the `_setjmp` site
in EhPush, which then branches to the handler label.

### ResumeSame/ResumeNext — Keep as unsupported

The Zia frontend only emits `ResumeLabel` (already works as unconditional
branch via `TerminatorLowering.cpp:447-454`). BASIC's `RESUME`/`RESUME NEXT`
would need these, but native BASIC compilation is deferred.

### Nested try/catch

Each try block gets its own jmp_buf slot. When an inner try's EhPush calls
`rt_trap_set_recovery`, it replaces the outer recovery point. When the inner
try's EhPop calls `rt_trap_clear_recovery`, the outer recovery is gone.

**Fix for nesting:** Save/restore the previous recovery buffer:
```c
// In EhPush: save previous recovery
jmp_buf *prev = rt_trap_get_recovery();  // Need to add this API
rt_trap_set_recovery(&my_jmp_buf);
// Store prev in a frame slot for EhPop to restore

// In EhPop: restore previous
rt_trap_set_recovery(prev_recovery);  // Restore outer handler
```

This requires adding `rt_trap_get_recovery()` to the runtime API.

## Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/OpcodeDispatch.cpp` | Replace EhPush/EhPop no-ops with MIR emission |
| `src/codegen/aarch64/FrameBuilder.hpp` | Ensure jmp_buf-sized slots can be allocated |
| `src/runtime/core/rt_io.c` | Add `rt_trap_get_recovery()` for nested EH |
| `src/codegen/aarch64/CodegenPipeline.cpp` | Add `_setjmp` and `rt_trap_*` to extern list |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md` — Add native EH to release notes
- `docs/codegen/aarch64.md` — Document EH implementation strategy
- `misc/plans/zia_bugs_20260329.md` — Cross-ref with catch(e) VM fix

## Verification

```zia
module Test;
bind Viper.Terminal;

func start() {
    try {
        throw "hello";
    } catch(e) {
        Say("caught: " + e);
    }

    try {
        var x = 10 / 0;
    } catch(e) {
        Say("div: " + e);
    }

    // Nested
    try {
        try { throw "inner"; }
        catch(e) { Say("inner: " + e); throw "outer"; }
    } catch(e) {
        Say("outer: " + e);
    }

    Say("done");
}
```

1. Compile native: `viper build test.zia -o test && ./test`
2. Compare with VM: `viper run test.zia`
3. Verify identical output
4. Run full test suite: `ctest --test-dir build --output-on-failure`

## Risks
- `sizeof(jmp_buf)` varies by platform — must use `<setjmp.h>` definition
- `_setjmp` on Darwin doesn't save/restore signal mask (faster but different from `setjmp`)
- Callee-saved registers (X19-X28, D8-D15) are preserved by setjmp/longjmp per ABI
- Each try block adds ~160 bytes to stack frame
- `rt_trap_set_recovery` is thread-local → thread-safe
