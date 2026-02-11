# x86_64 Windows Backend TODO

**Goal:** Compile all BASIC demos (frogger.bas, centipede.bas, vtris.bas) to working Windows executables.

**Status:** In Progress
**Last Updated:** 2025-12-25

---

## Phase 1: Missing IL Opcodes (Critical)

The BASIC demos use IL opcodes that are not yet implemented in the x86_64 backend.

### 1.1 Overflow-Checked Arithmetic (High Priority)

These are heavily used throughout the demos for safe integer operations.

| Opcode | Description | Implementation Strategy |
|--------|-------------|------------------------|
| `IAddOvf` | Signed add with overflow check | Use `addq` + `jo` to trap block |
| `ISubOvf` | Signed sub with overflow check | Use `subq` + `jo` to trap block |
| `IMulOvf` | Signed mul with overflow check | Use `imulq` + `jo` to trap block |

**Reference Pattern (ARM64):**
```
// Fast path: arithmetic + conditional branch to trap
add x0, x1, x2
b.vs .Ltrap_overflow

// Trap block (shared):
.Ltrap_overflow:
  mov x0, #TRAP_OVERFLOW
  bl rt_trap
```

**x86_64 Implementation:**
```asm
; IAddOvf example
addq %rsi, %rdi
jo .Ltrap_overflow     ; Jump if overflow flag set
; continue with result in %rdi

.Ltrap_overflow:
  movq $TRAP_OVERFLOW, %rdi
  callq rt_trap
```

### 1.2 Memory Operations (High Priority)

| Opcode | Description | Implementation Strategy |
|--------|-------------|------------------------|
| `Alloca` | Stack allocation | Subtract from RSP, return pointer |
| `GEP` | Get element pointer | Base + index * scale + offset |

**Alloca Implementation:**
```asm
; alloca(size)
subq %rdi, %rsp        ; Subtract size from stack pointer
andq $-16, %rsp        ; Align to 16 bytes
movq %rsp, %rax        ; Return stack pointer as result
```

**GEP Implementation:**
```asm
; gep base, index, scale, offset
; result = base + (index * scale) + offset
leaq offset(%rdi, %rsi, scale), %rax
```

### 1.3 Checked Casts (Medium Priority)

| Opcode | Description | Implementation Strategy |
|--------|-------------|------------------------|
| `CastSiNarrowChk` | Signed narrowing with range check | Compare + conditional trap |

**Implementation:**
```asm
; CastSiNarrowChk i64 -> i32
movq %rdi, %rax
movslq %eax, %rcx      ; Sign-extend 32-bit back to 64-bit
cmpq %rdi, %rcx        ; Compare with original
jne .Ltrap_narrow      ; Trap if values differ
; %eax contains valid i32 result
```

---

## Phase 2: Windows x64 ABI Compliance (Critical)

The current x86_64 backend assumes SysV AMD64 ABI. Windows uses a different calling convention.

### 2.1 Calling Convention Differences

| Aspect | SysV AMD64 (Linux/macOS) | Windows x64 |
|--------|--------------------------|-------------|
| Integer args | RDI, RSI, RDX, RCX, R8, R9 | RCX, RDX, R8, R9 |
| Float args | XMM0-XMM7 | XMM0-XMM3 |
| Return value | RAX (int), XMM0 (float) | RAX (int), XMM0 (float) |
| Shadow space | None | 32 bytes required |
| Red zone | 128 bytes below RSP | None |
| Stack alignment | 16-byte at call | 16-byte at call |
| Caller-saved | RAX, RCX, RDX, RSI, RDI, R8-R11 | RAX, RCX, RDX, R8-R11 |
| Callee-saved | RBX, RBP, R12-R15 | RBX, RBP, RDI, RSI, R12-R15 |

### 2.2 Implementation Tasks

#### Task 2.2.1: Platform Detection
Add platform detection to select correct ABI at codegen time:
```cpp
// In TargetInfo or similar
enum class CallingConvention { SysV, Win64 };
CallingConvention getCallingConvention() const {
#ifdef _WIN32
    return CallingConvention::Win64;
#else
    return CallingConvention::SysV;
#endif
}
```

#### Task 2.2.2: Argument Register Mapping
Create ABI-aware register sequences:
```cpp
// SysV argument registers
constexpr PhysReg kSysVIntArgs[] = {RDI, RSI, RDX, RCX, R8, R9};

// Windows argument registers
constexpr PhysReg kWin64IntArgs[] = {RCX, RDX, R8, R9};
```

#### Task 2.2.3: Shadow Space Allocation
Windows requires 32 bytes of "shadow space" above the return address:
```asm
; Windows prologue
push rbp
mov rbp, rsp
sub rsp, 32          ; Shadow space (minimum)
sub rsp, locals_size ; Local variables

; Before any call
sub rsp, 32          ; Ensure shadow space for callee
call target
add rsp, 32          ; Restore
```

#### Task 2.2.4: Red Zone Elimination
Remove any code that uses the 128-byte red zone (not available on Windows):
- Check frame building for red zone usage
- Ensure all locals are explicitly allocated

#### Task 2.2.5: Callee-Saved Register Updates
Windows considers RDI and RSI callee-saved (unlike SysV):
```cpp
// Windows callee-saved set
constexpr PhysReg kWin64CalleeSaved[] = {RBX, RBP, RDI, RSI, R12, R13, R14, R15};
```

### 2.3 Files to Modify

| File | Changes Required |
|------|-----------------|
| `src/codegen/x86_64/TargetX64.hpp` | Add platform detection, ABI enum |
| `src/codegen/x86_64/CallLowering.cpp` | Dual ABI register sequences |
| `src/codegen/x86_64/FrameLowering.cpp` | Shadow space, no red zone on Windows |
| `src/codegen/x86_64/RegAllocLinear.cpp` | Platform-specific callee-saved sets |
| `src/codegen/x86_64/AsmEmitter.cpp` | ABI-aware prologue/epilogue |

---

## Phase 3: Symbol and Linking (Medium Priority)

### 3.1 Symbol Naming

Windows COFF object files have different symbol naming conventions:

| Aspect | ELF (Linux) | Mach-O (macOS) | COFF (Windows) |
|--------|-------------|----------------|----------------|
| C function prefix | none | `_` | none (x64) |
| Global prefix | none | `_` | none |

**Note:** 32-bit Windows uses `_` prefix, but 64-bit Windows does not.

### 3.2 External Symbol References

Ensure runtime function calls use correct names:
```cpp
// Already correct for x64 Windows (no underscore)
callq rt_print_str
callq rt_str_from_lit
```

### 3.3 Object File Format

| Platform | Object Format | Assembler Flag |
|----------|---------------|----------------|
| Linux | ELF | (default) |
| macOS | Mach-O | (default) |
| Windows | COFF | `/Fo` for MSVC, `-c` for clang-cl |

---

## Phase 4: Runtime Library (High Priority)

The demos require 94 runtime functions. Verify all are available in Windows builds.

### 4.1 Core Runtime Functions Used

**String Operations:**
- `rt_str_from_lit` - Create string from literal
- `rt_str_concat` - String concatenation
- `rt_str_cmp_*` - String comparisons
- `rt_print_str` - Print string

**Array Operations:**
- `rt_array_*` - Array allocation and access
- `rt_gep` - Generic element pointer

**Console I/O:**
- `rt_print_*` - Print various types
- `rt_input_*` - Input handling
- `rt_cls` - Clear screen (Windows: different implementation)
- `rt_sleep` - Sleep (Windows: different API)

**Math:**
- `rt_rnd`, `rt_randomize` - Random numbers
- `rt_sin`, `rt_cos`, etc. - Trig functions

### 4.2 Windows-Specific Runtime Changes

Some runtime functions need Windows-specific implementations:

| Function | Linux/macOS | Windows |
|----------|-------------|---------|
| `rt_cls` | ANSI escape codes | `system("cls")` or Win32 API |
| `rt_sleep` | `usleep()` | `Sleep()` (milliseconds) |
| `rt_inkey` | termios | `_kbhit()` / `_getch()` |
| `rt_locate` | ANSI codes | `SetConsoleCursorPosition` |

---

## Phase 5: Assembler and Linker Toolchain

### 5.1 Current Toolchain Detection

The backend currently invokes `cc` which doesn't exist on Windows:
```
'cc' is not recognized as an internal or external command
```

### 5.2 Windows Toolchain Options

| Toolchain | Assembler | Linker | Notes |
|-----------|-----------|--------|-------|
| MSVC | ml64.exe | link.exe | Requires Intel syntax |
| Clang | clang.exe | lld-link.exe | Supports AT&T syntax |
| MinGW | as.exe | ld.exe | Supports AT&T syntax |

**Recommended:** Use Clang on Windows for compatibility:
```cpp
#ifdef _WIN32
    std::string compiler = "clang";
#else
    std::string compiler = "cc";
#endif
```

### 5.3 Build System Integration

Modify toolchain invocation in `cmd_codegen_x64.cpp`:
```cpp
std::string getSystemCompiler() {
#ifdef _WIN32
    // Try clang first, fall back to cl
    if (commandExists("clang")) return "clang";
    if (commandExists("cl")) return "cl";
    return "";
#else
    return "cc";
#endif
}
```

---

## Phase 6: Testing Strategy

### 6.1 Unit Tests

Each new opcode needs unit tests:
- `test_codegen_x86_64_iadd_ovf.cpp`
- `test_codegen_x86_64_isub_ovf.cpp`
- `test_codegen_x86_64_imul_ovf.cpp`
- `test_codegen_x86_64_alloca.cpp`
- `test_codegen_x86_64_gep.cpp`
- `test_codegen_x86_64_cast_narrow.cpp`

### 6.2 Golden Tests

IL-to-assembly golden tests for regression:
- `tests/golden/codegen/x86_64/overflow_arith.il`
- `tests/golden/codegen/x86_64/memory_ops.il`

### 6.3 E2E Demo Tests

Final validation - compile and run each demo:
```powershell
# Windows test script
.\build\bin\vipc.exe .\demos\basic\frogger.bas -o frogger.exe
.\frogger.exe

.\build\bin\vipc.exe .\demos\basic\centipede.bas -o centipede.exe
.\centipede.exe

.\build\bin\vipc.exe .\demos\basic\vtris.bas -o vtris.exe
.\vtris.exe
```

---

## Implementation Order

### Week 1: Core Opcodes
1. [ ] Implement `Alloca` opcode
2. [ ] Implement `GEP` opcode
3. [ ] Add unit tests for memory ops

### Week 2: Overflow Arithmetic
4. [ ] Implement `IAddOvf` with trap block
5. [ ] Implement `ISubOvf` with trap block
6. [ ] Implement `IMulOvf` with trap block
7. [ ] Add trap block infrastructure
8. [ ] Add unit tests

### Week 3: Casts and ABI
9. [ ] Implement `CastSiNarrowChk`
10. [ ] Add Windows x64 ABI detection
11. [ ] Implement Win64 argument passing
12. [ ] Add shadow space to frame builder

### Week 4: Toolchain and Runtime
13. [ ] Fix Windows assembler/linker invocation
14. [ ] Verify all runtime functions build on Windows
15. [ ] Add Windows-specific runtime implementations

### Week 5: Integration and Testing
16. [ ] Run all unit tests on Windows
17. [ ] Compile frogger.bas - fix any issues
18. [ ] Compile centipede.bas - fix any issues
19. [ ] Compile vtris.bas - fix any issues
20. [ ] Document remaining issues

---

## Current Progress

### Completed
- [x] XORrr32 register size fix (32-bit register names)
- [x] ConstStr opcode implementation
- [x] String literal handling in rodata

### In Progress
- [ ] Alloca opcode support
- [ ] GEP opcode support

### Blocked
- [ ] Full demo compilation (missing opcodes)
- [ ] Windows executable generation (toolchain issue)

---

## References

- ARM64 backend (reference implementation): `src/codegen/aarch64/`
- IL specification: `docs/il-guide.md`
- Windows x64 ABI: Microsoft docs
- SysV AMD64 ABI: System V Application Binary Interface
