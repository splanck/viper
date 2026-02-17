# Viper Bytecode VM - Comprehensive Technical Design

**Status:** IMPLEMENTED (see `src/bytecode/`)
**Date:** January 2026
**Version:** 2.1

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current IL Analysis](#2-current-il-analysis)
3. [Bytecode Architecture](#3-bytecode-architecture)
4. [Opcode Specification](#4-opcode-specification)
5. [Data Structures](#5-data-structures)
6. [IL-to-Bytecode Compiler](#6-il-to-bytecode-compiler)
7. [Interpreter Implementation](#7-interpreter-implementation)
8. [Threading Support](#8-threading-support)
9. [Exception Handling](#9-exception-handling)
10. [Debugging Support](#10-debugging-support)
11. [Memory Management](#11-memory-management)
12. [Runtime Integration](#12-runtime-integration)
13. [Phase 1 Implementation Plan](#13-phase-1-implementation-plan)
14. [Phase 2 Implementation Plan](#14-phase-2-implementation-plan)
15. [Phase 3 Implementation Plan](#15-phase-3-implementation-plan)
16. [Phase 4 Implementation Plan](#16-phase-4-implementation-plan)
17. [Testing Strategy](#17-testing-strategy)
18. [Performance Targets](#18-performance-targets)

---

## 1. Executive Summary

### 1.1 Problem Statement

The current Viper VM interprets a rich, compiler-oriented IL format at **~174,000 cycles per instruction**, making it **15,000x slower than Python's bytecode interpreter**. This architectural limitation cannot be overcome by incremental optimization.

### 1.2 Solution Overview

The implemented bytecode VM (see `src/bytecode/`):
1. Compiles IL to compact bytecode at module load time via `BytecodeCompiler`
2. Interprets bytecode using a stack-based evaluation model in `BytecodeVM`
3. Targets a **100-500x speedup** over the IL tree-walking VM (10-50x slower than Python)

### 1.3 Key Design Principles

1. **Determinism:** VM and bytecode VM outputs must match for all defined programs
2. **Feature Parity:** Support threading, exceptions, debugging, and tracing
3. **IL Compatibility:** All valid IL programs produce identical results
4. **Incremental Deployment:** Optional mode, then default, then exclusive
5. **Zero-Copy Integration:** Reuse existing runtime library unchanged

---

## 2. Current IL Analysis

### 2.1 Complete IL Opcode Inventory (80+ opcodes)

#### Integer Arithmetic (14 opcodes)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| Add | `add` | a + b (wrapping) | No |
| Sub | `sub` | a - b (wrapping) | No |
| Mul | `mul` | a * b (wrapping) | No |
| IAddOvf | `iadd.ovf` | a + b (overflow check) | Yes |
| ISubOvf | `isub.ovf` | a - b (overflow check) | Yes |
| IMulOvf | `imul.ovf` | a * b (overflow check) | Yes |
| SDiv | `sdiv` | a / b (signed) | No |
| UDiv | `udiv` | a / b (unsigned) | No |
| SRem | `srem` | a % b (signed) | No |
| URem | `urem` | a % b (unsigned) | No |
| SDivChk0 | `sdiv.chk0` | a / b (div-by-zero check) | Yes |
| UDivChk0 | `udiv.chk0` | a / b (div-by-zero check) | Yes |
| SRemChk0 | `srem.chk0` | a % b (div-by-zero check) | Yes |
| URemChk0 | `urem.chk0` | a % b (div-by-zero check) | Yes |

#### Bounds Checking (1 opcode)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| IdxChk | `idx.chk` | lo <= idx < hi | Yes |

#### Bitwise Operations (6 opcodes)
| Opcode | Mnemonic | Semantics |
|--------|----------|-----------|
| And | `and` | a & b |
| Or | `or` | a \| b |
| Xor | `xor` | a ^ b |
| Shl | `shl` | a << b |
| LShr | `lshr` | a >> b (logical) |
| AShr | `ashr` | a >> b (arithmetic) |

#### Float Arithmetic (4 opcodes)
| Opcode | Mnemonic | Semantics |
|--------|----------|-----------|
| FAdd | `fadd` | a + b |
| FSub | `fsub` | a - b |
| FMul | `fmul` | a * b |
| FDiv | `fdiv` | a / b |

#### Integer Comparisons (10 opcodes)
| Opcode | Mnemonic | Semantics |
|--------|----------|-----------|
| ICmpEq | `icmp_eq` | a == b |
| ICmpNe | `icmp_ne` | a != b |
| SCmpLT | `scmp_lt` | a < b (signed) |
| SCmpLE | `scmp_le` | a <= b (signed) |
| SCmpGT | `scmp_gt` | a > b (signed) |
| SCmpGE | `scmp_ge` | a >= b (signed) |
| UCmpLT | `ucmp_lt` | a < b (unsigned) |
| UCmpLE | `ucmp_le` | a <= b (unsigned) |
| UCmpGT | `ucmp_gt` | a > b (unsigned) |
| UCmpGE | `ucmp_ge` | a >= b (unsigned) |

#### Float Comparisons (8 opcodes)
| Opcode | Mnemonic | Semantics |
|--------|----------|-----------|
| FCmpEQ | `fcmp_eq` | a == b |
| FCmpNE | `fcmp_ne` | a != b |
| FCmpLT | `fcmp_lt` | a < b |
| FCmpLE | `fcmp_le` | a <= b |
| FCmpGT | `fcmp_gt` | a > b |
| FCmpGE | `fcmp_ge` | a >= b |
| FCmpOrd | `fcmp_ord` | ordered (both non-NaN) |
| FCmpUno | `fcmp_uno` | unordered (either NaN) |

#### Type Conversions (10 opcodes)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| Sitofp | `sitofp` | signed int → float | No |
| Fptosi | `fptosi` | float → signed int | No |
| CastFpToSiRteChk | `cast.fp_to_si.rte.chk` | float → int (checked) | Yes |
| CastFpToUiRteChk | `cast.fp_to_ui.rte.chk` | float → uint (checked) | Yes |
| CastSiNarrowChk | `cast.si_narrow.chk` | i64 → i32 (checked) | Yes |
| CastUiNarrowChk | `cast.ui_narrow.chk` | u64 → u32 (checked) | Yes |
| CastSiToFp | `cast.si_to_fp` | signed int → float | No |
| CastUiToFp | `cast.ui_to_fp` | unsigned int → float | No |
| Zext1 | `zext1` | i1 → i64 | No |
| Trunc1 | `trunc1` | i64 → i1 | No |

#### Memory Operations (6 opcodes)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| Alloca | `alloca` | Stack allocate n bytes | Yes |
| GEP | `gep` | ptr + offset | No |
| Load | `load` | *ptr | No |
| Store | `store` | *ptr = val | Yes |
| AddrOf | `addr_of` | &val | No |
| GAddr | `gaddr` | &global | No |

#### Constants (3 opcodes)
| Opcode | Mnemonic | Semantics |
|--------|----------|-----------|
| ConstStr | `const_str` | Load string literal |
| ConstNull | `const_null` | Load null pointer |
| ConstF64 | `const.f64` | Load float constant |

#### Control Flow (6 opcodes)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| Call | `call` | Call function | Yes |
| CallIndirect | `call.indirect` | Call via pointer | Yes |
| SwitchI32 | `switch.i32` | Multi-way branch | Yes |
| Br | `br` | Unconditional branch | Yes |
| CBr | `cbr` | Conditional branch | Yes |
| Ret | `ret` | Return from function | Yes |

#### Exception Handling (14 opcodes)
| Opcode | Mnemonic | Semantics | Trap? |
|--------|----------|-----------|-------|
| TrapKind | `trap.kind` | Get current trap kind | No |
| TrapFromErr | `trap.from_err` | Raise trap from code | Yes |
| TrapErr | `trap.err` | Create error value | No |
| ErrGetKind | `err.get_kind` | Extract kind from error | No |
| ErrGetCode | `err.get_code` | Extract code from error | No |
| ErrGetIp | `err.get_ip` | Extract IP from error | No |
| ErrGetLine | `err.get_line` | Extract line from error | No |
| EhPush | `eh.push` | Register handler | Yes |
| EhPop | `eh.pop` | Unregister handler | Yes |
| ResumeSame | `resume.same` | Resume at fault | Yes |
| ResumeNext | `resume.next` | Resume after fault | Yes |
| ResumeLabel | `resume.label` | Resume at label | Yes |
| EhEntry | `eh.entry` | Handler entry marker | Yes |
| Trap | `trap` | Raise domain trap | Yes |

### 2.2 Performance Analysis (Baseline at Design Time)

These figures were measured against the IL-tree-walking VM before bytecode VM implementation. The bytecode VM
(`src/bytecode/`) targets the right-column performance envelope.

| Metric | IL tree-walking VM (baseline) | Target Bytecode VM |
|--------|-------------------------------|-------------------|
| Cycles/instruction | ~174,000 | ~100-500 |
| Instruction throughput | 17K/sec | 5-30M/sec |
| fib(20) time | 7,050ms | 15-70ms |
| vs Python ratio | 15,000x slower | 10-50x slower |

---

## 3. Bytecode Architecture

### 3.1 Instruction Encoding

**Primary Format: 32-bit fixed-width**

```
┌───────────┬───────────┬───────────┬───────────┐
│ opcode(8) │  arg0(8)  │  arg1(8)  │  arg2(8)  │
└───────────┴───────────┴───────────┴───────────┘
```

**Extended Format: 64-bit for large operands**

```
┌───────────┬───────────┬───────────┬───────────┬───────────────────────────────┐
│ opcode(8) │  ext(8)   │  arg0(8)  │  arg1(8)  │           arg32(32)           │
└───────────┴───────────┴───────────┴───────────┴───────────────────────────────┘
```

### 3.2 Operand Encoding

| Encoding | Bits | Range | Usage |
|----------|------|-------|-------|
| u8 | 8 | 0-255 | Local indices, small constants |
| u16 | 16 | 0-65535 | Function indices, constant pool |
| i16 | 16 | ±32767 | Branch offsets |
| u24 | 24 | 0-16M | Extended constants |
| i24 | 24 | ±8M | Long branch offsets |

### 3.3 Stack Model

```
┌─────────────────────────────────────────────────────────────┐
│                    Bytecode Frame                           │
├─────────────────────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Locals Array                                            │ │
│ │ [0: param0] [1: param1] ... [n: local0] [n+1: local1]   │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Operand Stack (grows upward)                            │ │
│ │ [slot0] [slot1] [slot2] ... [TOS]                       │ │
│ │                              ^sp                        │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Alloca Region (grows downward from frame end)           │ │
│ │ ... [alloca2] [alloca1] [alloca0]                       │ │
│ │      ^alloca_sp                                         │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 3.4 Slot Type

Actual implementation from `src/bytecode/Bytecode.hpp`:

```cpp
union BCSlot {
    int64_t i64; ///< Integer representation (also booleans, unsigned values)
    double f64;  ///< IEEE-754 double-precision floating point
    void* ptr;   ///< Pointer representation (objects, strings, memory addresses)
};
static_assert(sizeof(BCSlot) == 8);
```

Note: Unlike the IL VM's `Slot`, `BCSlot` does not have a separate `rt_string` field.
String values are carried as `void*` (pointer to the runtime string object).

---

## 4. Opcode Specification

### 4.1 Stack Operations (0x00-0x0F)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0x00 | NOP | - | [] → [] | No operation |
| 0x01 | DUP | - | [a] → [a,a] | Duplicate TOS |
| 0x02 | DUP2 | - | [a,b] → [a,b,a,b] | Duplicate top 2 |
| 0x03 | POP | - | [a] → [] | Discard TOS |
| 0x04 | POP2 | - | [a,b] → [] | Discard top 2 |
| 0x05 | SWAP | - | [a,b] → [b,a] | Swap top 2 |
| 0x06 | ROT3 | - | [a,b,c] → [c,a,b] | Rotate top 3 |

### 4.2 Local Variable Operations (0x10-0x1F)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0x10 | LOAD_LOCAL | u8 idx | [] → [v] | Push locals[idx] |
| 0x11 | STORE_LOCAL | u8 idx | [v] → [] | Pop to locals[idx] |
| 0x12 | LOAD_LOCAL_W | u16 idx | [] → [v] | Wide local load |
| 0x13 | STORE_LOCAL_W | u16 idx | [v] → [] | Wide local store |
| 0x14 | INC_LOCAL | u8 idx | [] → [] | locals[idx]++ |
| 0x15 | DEC_LOCAL | u8 idx | [] → [] | locals[idx]-- |

### 4.3 Constant Loading (0x20-0x2F)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0x20 | LOAD_I8 | i8 val | [] → [v] | Push signed 8-bit |
| 0x21 | LOAD_I16 | i16 val | [] → [v] | Push signed 16-bit |
| 0x22 | LOAD_I32 | i32 val | [] → [v] | Push signed 32-bit (ext) |
| 0x23 | LOAD_I64 | u16 idx | [] → [v] | Push i64 from pool |
| 0x24 | LOAD_F64 | u16 idx | [] → [v] | Push f64 from pool |
| 0x25 | LOAD_STR | u16 idx | [] → [v] | Push string from pool |
| 0x26 | LOAD_NULL | - | [] → [null] | Push null pointer |
| 0x27 | LOAD_ZERO | - | [] → [0] | Push i64 zero |
| 0x28 | LOAD_ONE | - | [] → [1] | Push i64 one |
| 0x29 | LOAD_GLOBAL | u16 idx | [] → [v] | Push global value |
| 0x2A | STORE_GLOBAL | u16 idx | [v] → [] | Pop to global |

### 4.4 Integer Arithmetic (0x30-0x4F)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0x30 | ADD_I64 | [a,b] → [a+b] | Integer add |
| 0x31 | SUB_I64 | [a,b] → [a-b] | Integer subtract |
| 0x32 | MUL_I64 | [a,b] → [a*b] | Integer multiply |
| 0x33 | SDIV_I64 | [a,b] → [a/b] | Signed divide |
| 0x34 | UDIV_I64 | [a,b] → [a/b] | Unsigned divide |
| 0x35 | SREM_I64 | [a,b] → [a%b] | Signed remainder |
| 0x36 | UREM_I64 | [a,b] → [a%b] | Unsigned remainder |
| 0x37 | NEG_I64 | [a] → [-a] | Negate |
| 0x38 | ADD_I64_OVF | [a,b] → [a+b] | Add with overflow trap |
| 0x39 | SUB_I64_OVF | [a,b] → [a-b] | Sub with overflow trap |
| 0x3A | MUL_I64_OVF | [a,b] → [a*b] | Mul with overflow trap |
| 0x3B | SDIV_I64_CHK | [a,b] → [a/b] | Div with zero-check |
| 0x3C | UDIV_I64_CHK | [a,b] → [a/b] | Div with zero-check |
| 0x3D | SREM_I64_CHK | [a,b] → [a%b] | Rem with zero-check |
| 0x3E | UREM_I64_CHK | [a,b] → [a%b] | Rem with zero-check |
| 0x3F | IDX_CHK | [idx,lo,hi] → [idx] | Bounds check trap |

### 4.5 Float Arithmetic (0x50-0x5F)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0x50 | ADD_F64 | [a,b] → [a+b] | Float add |
| 0x51 | SUB_F64 | [a,b] → [a-b] | Float subtract |
| 0x52 | MUL_F64 | [a,b] → [a*b] | Float multiply |
| 0x53 | DIV_F64 | [a,b] → [a/b] | Float divide |
| 0x54 | NEG_F64 | [a] → [-a] | Float negate |

### 4.6 Bitwise Operations (0x60-0x6F)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0x60 | AND_I64 | [a,b] → [a&b] | Bitwise AND |
| 0x61 | OR_I64 | [a,b] → [a\|b] | Bitwise OR |
| 0x62 | XOR_I64 | [a,b] → [a^b] | Bitwise XOR |
| 0x63 | NOT_I64 | [a] → [~a] | Bitwise NOT |
| 0x64 | SHL_I64 | [a,b] → [a<<b] | Shift left |
| 0x65 | LSHR_I64 | [a,b] → [a>>>b] | Logical shift right |
| 0x66 | ASHR_I64 | [a,b] → [a>>b] | Arithmetic shift right |

### 4.7 Comparisons (0x70-0x8F)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0x70 | CMP_EQ_I64 | [a,b] → [a==b] | Integer equal |
| 0x71 | CMP_NE_I64 | [a,b] → [a!=b] | Integer not equal |
| 0x72 | CMP_SLT_I64 | [a,b] → [a<b] | Signed less than |
| 0x73 | CMP_SLE_I64 | [a,b] → [a<=b] | Signed less or equal |
| 0x74 | CMP_SGT_I64 | [a,b] → [a>b] | Signed greater than |
| 0x75 | CMP_SGE_I64 | [a,b] → [a>=b] | Signed greater or equal |
| 0x76 | CMP_ULT_I64 | [a,b] → [a<b] | Unsigned less than |
| 0x77 | CMP_ULE_I64 | [a,b] → [a<=b] | Unsigned less or equal |
| 0x78 | CMP_UGT_I64 | [a,b] → [a>b] | Unsigned greater than |
| 0x79 | CMP_UGE_I64 | [a,b] → [a>=b] | Unsigned greater or equal |
| 0x80 | CMP_EQ_F64 | [a,b] → [a==b] | Float equal |
| 0x81 | CMP_NE_F64 | [a,b] → [a!=b] | Float not equal |
| 0x82 | CMP_LT_F64 | [a,b] → [a<b] | Float less than |
| 0x83 | CMP_LE_F64 | [a,b] → [a<=b] | Float less or equal |
| 0x84 | CMP_GT_F64 | [a,b] → [a>b] | Float greater than |
| 0x85 | CMP_GE_F64 | [a,b] → [a>=b] | Float greater or equal |

### 4.8 Type Conversions (0x90-0x9F)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0x90 | I64_TO_F64 | [i] → [f] | Signed int to float |
| 0x91 | U64_TO_F64 | [i] → [f] | Unsigned int to float |
| 0x92 | F64_TO_I64 | [f] → [i] | Float to signed int |
| 0x93 | F64_TO_I64_CHK | [f] → [i] | Float to int (checked) |
| 0x94 | F64_TO_U64_CHK | [f] → [i] | Float to uint (checked) |
| 0x95 | I64_NARROW_CHK | [i] → [i] | Signed narrow (checked) |
| 0x96 | U64_NARROW_CHK | [i] → [i] | Unsigned narrow (checked) |
| 0x97 | BOOL_TO_I64 | [b] → [i] | Boolean to i64 |
| 0x98 | I64_TO_BOOL | [i] → [b] | i64 to boolean |

### 4.9 Memory Operations (0xA0-0xAF)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0xA0 | ALLOCA | - | [n] → [ptr] | Allocate n bytes |
| 0xA1 | GEP | - | [ptr,off] → [ptr+off] | Pointer arithmetic |
| 0xA2 | LOAD_I8 | - | [ptr] → [v] | Load 8-bit signed |
| 0xA3 | LOAD_I16 | - | [ptr] → [v] | Load 16-bit signed |
| 0xA4 | LOAD_I32 | - | [ptr] → [v] | Load 32-bit signed |
| 0xA5 | LOAD_I64_MEM | - | [ptr] → [v] | Load 64-bit |
| 0xA6 | LOAD_F64_MEM | - | [ptr] → [v] | Load float |
| 0xA7 | LOAD_PTR | - | [ptr] → [v] | Load pointer |
| 0xA8 | LOAD_STR_MEM | - | [ptr] → [v] | Load string handle |
| 0xA9 | STORE_I8 | - | [ptr,v] → [] | Store 8-bit |
| 0xAA | STORE_I16 | - | [ptr,v] → [] | Store 16-bit |
| 0xAB | STORE_I32 | - | [ptr,v] → [] | Store 32-bit |
| 0xAC | STORE_I64_MEM | - | [ptr,v] → [] | Store 64-bit |
| 0xAD | STORE_F64_MEM | - | [ptr,v] → [] | Store float |
| 0xAE | STORE_PTR | - | [ptr,v] → [] | Store pointer |
| 0xAF | STORE_STR_MEM | - | [ptr,v] → [] | Store string |

### 4.10 Control Flow (0xB0-0xBF)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0xB0 | JUMP | i16 off | [] → [] | Unconditional jump |
| 0xB1 | JUMP_IF_TRUE | i16 off | [c] → [] | Jump if nonzero |
| 0xB2 | JUMP_IF_FALSE | i16 off | [c] → [] | Jump if zero |
| 0xB3 | JUMP_LONG | i24 off | [] → [] | Extended jump |
| 0xB4 | SWITCH | u16 tbl | [v] → [] | Table switch |
| 0xB5 | CALL | u16 fn | [args...] → [ret] | Call function |
| 0xB6 | CALL_NATIVE | u16 fn | [args...] → [ret] | Call runtime |
| 0xB7 | CALL_INDIRECT | u8 n | [fn,args...] → [ret] | Indirect call |
| 0xB8 | RETURN | - | [v] → <ret> | Return value |
| 0xB9 | RETURN_VOID | - | [] → <ret> | Return void |
| 0xBA | TAIL_CALL | u16 fn | [args...] → <ret> | Tail call |

### 4.11 Exception Handling (0xC0-0xCF)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0xC0 | EH_PUSH | u16 off | [] → [] | Register handler at offset |
| 0xC1 | EH_POP | - | [] → [] | Unregister handler |
| 0xC2 | EH_ENTRY | - | [] → [] | Handler entry marker |
| 0xC3 | TRAP | u8 kind | [] → <trap> | Raise trap |
| 0xC4 | TRAP_FROM_ERR | - | [code] → <trap> | Trap from error |
| 0xC5 | MAKE_ERROR | - | [code,msg] → [err] | Create error value |
| 0xC6 | ERR_GET_KIND | - | [err] → [kind] | Extract trap kind |
| 0xC7 | ERR_GET_CODE | - | [err] → [code] | Extract error code |
| 0xC8 | ERR_GET_IP | - | [err] → [ip] | Extract fault IP |
| 0xC9 | ERR_GET_LINE | - | [err] → [line] | Extract source line |
| 0xCA | RESUME_SAME | - | [tok] → <resume> | Resume at fault |
| 0xCB | RESUME_NEXT | - | [tok] → <resume> | Resume after fault |
| 0xCC | RESUME_LABEL | i16 off | [tok] → <resume> | Resume at label |

### 4.12 Debug Operations (0xD0-0xDF)

| Code | Mnemonic | Args | Stack | Description |
|------|----------|------|-------|-------------|
| 0xD0 | LINE | u16 line | [] → [] | Source line marker |
| 0xD1 | BREAKPOINT | - | [] → [] | Debug breakpoint |
| 0xD2 | WATCH_VAR | u16 idx | [] → [] | Variable watch trigger |

### 4.13 String Operations (0xE0-0xEF)

| Code | Mnemonic | Stack | Description |
|------|----------|-------|-------------|
| 0xE0 | STR_RETAIN | [s] → [s] | Increment refcount |
| 0xE1 | STR_RELEASE | [s] → [] | Decrement refcount |

---

## 5. Data Structures

### 5.1 BytecodeModule

Actual implementation from `src/bytecode/BytecodeModule.hpp`:

```cpp
struct BytecodeModule {
    // Header
    uint32_t magic;   // kBytecodeModuleMagic = 0x01434256 ("VBC\x01")
    uint32_t version; // kBytecodeVersion = 1
    uint32_t flags;   // Feature flags (reserved)

    // Constant pools (deduplicated: same value -> same index)
    std::vector<int64_t> i64Pool;
    std::vector<double> f64Pool;
    std::vector<std::string> stringPool;

    // Functions
    std::vector<BytecodeFunction> functions;
    std::unordered_map<std::string, uint32_t> functionIndex;

    // Native function references
    std::vector<NativeFuncRef> nativeFuncs;
    std::unordered_map<std::string, uint32_t> nativeFuncIndex;

    // Globals
    std::vector<GlobalInfo> globals;
    std::unordered_map<std::string, uint32_t> globalIndex;

    // Debug info (optional): source file paths + per-function line tables
    std::vector<SourceFileInfo> sourceFiles;
};
```

### 5.2 BytecodeFunction

Actual implementation from `src/bytecode/BytecodeModule.hpp`:

```cpp
struct BytecodeFunction {
    std::string name;     // Fully qualified function name
    uint32_t numParams;   // Number of parameters (mapped to first N locals)
    uint32_t numLocals;   // Total local slots (params + temporaries)
    uint32_t maxStack;    // Maximum operand stack depth
    uint32_t allocaSize;  // Maximum alloca bytes
    bool hasReturn;       // True if function returns a value

    std::vector<uint32_t> code;              // Bytecode instruction stream (32-bit words)
    std::vector<ExceptionRange> exceptionRanges; // Protected regions and handler PCs
    std::vector<SwitchTable> switchTables;   // Tables referenced by SWITCH opcodes

    // Debug info (optional)
    std::vector<LocalVarInfo> localVars;  // Variable name/slot/liveness info
    uint32_t sourceFileIdx;               // Index into BytecodeModule::sourceFiles
    std::vector<uint32_t> lineTable;      // PC-to-source-line mapping (indexed by PC)
};

struct ExceptionRange {
    uint32_t startPc;   // Range start (inclusive)
    uint32_t endPc;     // Range end (exclusive)
    uint32_t handlerPc; // Handler entry point
};
```

### 5.3 BytecodeVM State

The actual implementation in `src/bytecode/BytecodeVM.hpp` uses the following structures:

```cpp
/// Call frame for a single function invocation.
struct BCFrame {
    const BytecodeFunction* func; ///< Function being executed.
    uint32_t pc;                  ///< Program counter (index into func->code).
    BCSlot* locals;               ///< Pointer to the first local variable slot.
    BCSlot* stackBase;            ///< Operand stack base for this frame.
    uint32_t ehStackDepth;        ///< Exception handler stack depth at frame entry.
    uint32_t callSitePc;          ///< PC at the call site (for stack traces).
    size_t allocaBase;            ///< Alloca stack position at frame entry (for cleanup).
};

/// Exception handler entry on the handler stack.
struct BCExceptionHandler {
    uint32_t handlerPc;   ///< PC of the handler entry point.
    uint32_t frameIndex;  ///< Call stack frame index when registered.
    BCSlot* stackPointer; ///< Operand stack pointer when registered.
};

/// Trap kinds for runtime error classification.
enum class TrapKind : uint8_t {
    None = 0, Overflow, InvalidCast, DivisionByZero,
    IndexOutOfBounds, NullPointer, StackOverflow, InvalidOpcode, RuntimeError
};

class BytecodeVM {
    // Key limits (from Bytecode.hpp):
    //   kMaxCallDepth = 4096   (max call stack frames)
    //   kMaxStackSize = 1024   (max BCSlot entries per frame operand stack)
    // ...
    VMState state_;          ///< Ready/Running/Halted/Trapped
    TrapKind trapKind_;      ///< Kind of most recent trap
    std::string trapMessage_;///< Human-readable trap description
    uint64_t instrCount_;    ///< Cumulative instruction count
    BCFrame* fp_;            ///< Current frame pointer
    bool runtimeBridgeEnabled_;  ///< Whether native calls use RuntimeBridge
    bool useThreadedDispatch_;   ///< Whether computed-goto dispatch is active
};
```

---

## 6. IL-to-Bytecode Compiler

### 6.1 Compilation Pipeline

```
IL Module
    │
    ▼
┌─────────────────────┐
│ 1. Function Scanner │ - Compute local counts, max stack
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 2. SSA Eliminator   │ - Map SSA values to local slots
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 3. Block Linearizer │ - Flatten CFG to linear bytecode
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 4. Constant Pooler  │ - Extract literals to pools
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 5. Code Generator   │ - Emit bytecode instructions
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 6. Branch Resolver  │ - Fix branch offsets
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│ 7. Peephole Opt     │ - Optimize instruction sequences
└─────────────────────┘
    │
    ▼
BytecodeModule
```

### 6.2 SSA to Locals Mapping

```cpp
class SSAToLocalsMapper {
    std::unordered_map<uint32_t, uint32_t> ssaToLocal;
    uint32_t nextLocal;

public:
    explicit SSAToLocalsMapper(const il::core::Function& fn) {
        nextLocal = 0;

        // Map parameters first (preserve order)
        for (const auto& param : fn.params) {
            ssaToLocal[param.id] = nextLocal++;
        }

        // Map block parameters
        for (const auto& block : fn.blocks) {
            for (const auto& param : block.params) {
                if (ssaToLocal.find(param.id) == ssaToLocal.end()) {
                    ssaToLocal[param.id] = nextLocal++;
                }
            }
        }

        // Map instruction results
        for (const auto& block : fn.blocks) {
            for (const auto& instr : block.instructions) {
                if (instr.result) {
                    if (ssaToLocal.find(*instr.result) == ssaToLocal.end()) {
                        ssaToLocal[*instr.result] = nextLocal++;
                    }
                }
            }
        }
    }

    uint32_t getLocal(uint32_t ssaId) const {
        auto it = ssaToLocal.find(ssaId);
        assert(it != ssaToLocal.end());
        return it->second;
    }

    uint32_t localCount() const { return nextLocal; }
};
```

### 6.3 Block Linearization

```cpp
class BlockLinearizer {
    std::vector<const il::core::BasicBlock*> ordering;
    std::unordered_map<std::string, uint32_t> blockOffsets;

public:
    void linearize(const il::core::Function& fn) {
        // Depth-first ordering with fall-through optimization
        std::unordered_set<const il::core::BasicBlock*> visited;
        std::vector<const il::core::BasicBlock*> worklist;

        if (!fn.blocks.empty()) {
            worklist.push_back(&fn.blocks.front());
        }

        while (!worklist.empty()) {
            auto* block = worklist.back();
            worklist.pop_back();

            if (visited.count(block)) continue;
            visited.insert(block);
            ordering.push_back(block);

            // Add successors in reverse order for DFS
            // Prefer fall-through to first successor
            // ...
        }
    }
};
```

### 6.4 Code Generation

```cpp
class BytecodeGenerator {
    std::vector<uint32_t> code;
    std::vector<std::pair<uint32_t, std::string>> pendingBranches;

    void emitInstr(const il::core::Instr& instr) {
        switch (instr.op) {
            case Opcode::Add:
                emitOperands(instr, 2);
                emit(BC_ADD_I64);
                emitStoreResult(instr);
                break;

            case Opcode::IAddOvf:
                emitOperands(instr, 2);
                emit(BC_ADD_I64_OVF);
                emitStoreResult(instr);
                break;

            case Opcode::Call:
                emitCallArgs(instr);
                emitCall(instr);
                emitStoreResult(instr);
                break;

            case Opcode::CBr:
                emitOperand(instr.operands[0]);
                emitConditionalBranch(instr);
                break;

            // ... all 70 opcodes
        }
    }

    void emitOperand(const il::core::Value& val) {
        switch (val.kind) {
            case Value::Kind::Temp:
                emit(BC_LOAD_LOCAL, mapper.getLocal(val.id));
                break;

            case Value::Kind::ConstInt:
                if (val.i64 >= -128 && val.i64 <= 127) {
                    emit(BC_LOAD_I8, static_cast<int8_t>(val.i64));
                } else {
                    uint32_t idx = addI64Constant(val.i64);
                    emit(BC_LOAD_I64, idx);
                }
                break;

            case Value::Kind::ConstFloat:
                emit(BC_LOAD_F64, addF64Constant(val.f64));
                break;

            // ...
        }
    }

    void emitStoreResult(const il::core::Instr& instr) {
        if (instr.result) {
            emit(BC_STORE_LOCAL, mapper.getLocal(*instr.result));
        } else {
            emit(BC_POP);  // Discard unused result
        }
    }
};
```

---

## 7. Interpreter Implementation

### 7.1 Main Dispatch Loop (Switch-based)

```cpp
void BytecodeVM::run() {
    while (true) {
        uint32_t instr = fp->func->code[fp->pc++];
        uint8_t opcode = instr & 0xFF;

        #if VIPER_BC_TRACE
        ++instrCount;
        if (tracer) tracer->onBytecode(fp->pc - 1, opcode);
        #endif

        switch (opcode) {
            case BC_LOAD_LOCAL: {
                uint8_t idx = (instr >> 8) & 0xFF;
                *sp++ = fp->locals[idx];
                break;
            }

            case BC_STORE_LOCAL: {
                uint8_t idx = (instr >> 8) & 0xFF;
                fp->locals[idx] = *--sp;
                break;
            }

            case BC_ADD_I64: {
                BCSlot b = *--sp;
                BCSlot a = *--sp;
                sp->i64 = a.i64 + b.i64;
                sp++;
                break;
            }

            case BC_ADD_I64_OVF: {
                BCSlot b = *--sp;
                BCSlot a = *--sp;
                int64_t result;
                if (__builtin_add_overflow(a.i64, b.i64, &result)) {
                    trap(TrapKind::Overflow, "integer overflow in add");
                }
                sp->i64 = result;
                sp++;
                break;
            }

            case BC_CALL: {
                uint16_t fnIdx = (instr >> 8) & 0xFFFF;
                call(&module->functions[fnIdx]);
                break;
            }

            case BC_CALL_NATIVE: {
                uint16_t fnIdx = (instr >> 8) & 0xFFFF;
                callNative(fnIdx);
                break;
            }

            case BC_RETURN: {
                BCSlot result = *--sp;
                if (!popFrame()) return;
                *sp++ = result;
                break;
            }

            case BC_JUMP_IF_FALSE: {
                int16_t offset = static_cast<int16_t>(instr >> 16);
                if ((*--sp).i64 == 0) {
                    fp->pc += offset;
                }
                break;
            }

            // ... all opcodes
        }
    }
}
```

### 7.2 Computed Goto Dispatch (Threaded)

```cpp
void BytecodeVM::runThreaded() {
    static void* dispatchTable[] = {
        &&L_NOP, &&L_DUP, &&L_DUP2, &&L_POP, &&L_POP2, &&L_SWAP, ...
    };

    #define DISPATCH() do {                           \
        instr = fp->func->code[fp->pc++];             \
        goto *dispatchTable[instr & 0xFF];            \
    } while(0)

    uint32_t instr;
    DISPATCH();

L_LOAD_LOCAL:
    *sp++ = fp->locals[(instr >> 8) & 0xFF];
    DISPATCH();

L_STORE_LOCAL:
    fp->locals[(instr >> 8) & 0xFF] = *--sp;
    DISPATCH();

L_ADD_I64:
    sp[-2].i64 += sp[-1].i64;
    sp--;
    DISPATCH();

// ... all labels
}
```

### 7.3 Function Call Implementation

```cpp
void BytecodeVM::call(const BytecodeFunction* fn) {
    // Check stack overflow
    if (callStack.size() >= kMaxCallDepth) {
        trap(TrapKind::RuntimeError, "stack overflow");
    }

    // Push new frame
    BCFrame& newFrame = callStack.emplace_back();
    newFrame.func = fn;
    newFrame.pc = 0;
    newFrame.ehStackDepth = ehStack.size();
    newFrame.callSitePc = fp->pc - 1;

    // Allocate locals
    BCSlot* localsStart = sp - fn->numParams;
    newFrame.locals = localsStart;
    newFrame.stackBase = localsStart + fn->numLocals;

    // Zero non-parameter locals
    std::fill(localsStart + fn->numParams,
              localsStart + fn->numLocals,
              BCSlot{});

    // Initialize alloca region
    newFrame.allocaPtr = allocaRegionEnd;

    // Update stack pointer past locals
    sp = newFrame.stackBase;

    // Switch to new frame
    fp = &newFrame;
}

bool BytecodeVM::popFrame() {
    // Unwind exception handlers
    while (ehStack.size() > fp->ehStackDepth) {
        ehStack.pop_back();
    }

    // Pop frame
    callStack.pop_back();
    if (callStack.empty()) {
        return false;  // Execution complete
    }

    // Restore previous frame
    fp = &callStack.back();
    sp = fp->stackBase + fp->func->maxStack;  // Restore approx position

    return true;
}
```

---

## 8. Threading Support

### 8.1 Thread Model

Each BytecodeVM instance is single-threaded. Multi-threaded programs create separate VMs per thread.

```cpp
struct ThreadState {
    BytecodeVM* vm;
    const BytecodeFunction* entry;
    std::span<BCSlot> args;
    std::promise<BCSlot> result;
};

void threadEntryPoint(ThreadState* state) {
    // Create new VM for this thread
    BytecodeVM vm;
    vm.module = state->vm->module;
    vm.rtContext = createThreadLocalContext();

    // Run entry function
    BCSlot result = vm.exec(state->entry, state->args);

    // Signal completion
    state->result.set_value(result);
}
```

### 8.2 Threading Primitives Support

The bytecode VM calls through to the existing runtime threading APIs:
- `rt_thread_start` → BC_CALL_NATIVE with thread entry trampoline
- `rt_gate_enter` → BC_CALL_NATIVE
- `rt_barrier_arrive` → BC_CALL_NATIVE
- `rt_rwlock_*` → BC_CALL_NATIVE

No bytecode-level threading opcodes needed—all threading via runtime calls.

---

## 9. Exception Handling

### 9.1 Handler Registration

```cpp
case BC_EH_PUSH: {
    uint16_t handlerOffset = (instr >> 8) & 0xFFFF;
    ehStack.push_back({
        .handlerPc = fp->pc + handlerOffset,
        .frame = fp
    });
    break;
}

case BC_EH_POP: {
    assert(!ehStack.empty());
    ehStack.pop_back();
    break;
}
```

### 9.2 Trap Propagation

```cpp
void BytecodeVM::trap(TrapKind kind, const char* message) {
    activeError = {
        .kind = kind,
        .code = 0,
        .ip = fp->pc - 1,
        .line = getSourceLine(fp->pc - 1)
    };

    // Search for handler
    while (!ehStack.empty()) {
        auto& handler = ehStack.back();

        // Unwind to handler's frame
        while (fp != handler.frame) {
            popFrame();
        }

        // Set up error parameters
        resumeState = {
            .faultPc = fp->pc - 1,
            .nextPc = fp->pc,
            .valid = true
        };

        // Jump to handler
        fp->pc = handler.handlerPc;
        ehStack.pop_back();

        // Push error and token onto stack
        sp->ptr = &activeError;
        sp++;
        sp->ptr = &resumeState;
        sp++;

        return;  // Resume execution in handler
    }

    // No handler found - abort
    throw UnhandledTrapException(activeError, message);
}
```

### 9.3 Resume Operations

```cpp
case BC_RESUME_SAME: {
    ResumeState* tok = static_cast<ResumeState*>((*--sp).ptr);
    assert(tok->valid);
    tok->valid = false;
    fp->pc = tok->faultPc;  // Re-execute faulting instruction
    break;
}

case BC_RESUME_NEXT: {
    ResumeState* tok = static_cast<ResumeState*>((*--sp).ptr);
    assert(tok->valid);
    tok->valid = false;
    fp->pc = tok->nextPc;   // Continue after fault
    break;
}

case BC_RESUME_LABEL: {
    int16_t offset = static_cast<int16_t>(instr >> 16);
    ResumeState* tok = static_cast<ResumeState*>((*--sp).ptr);
    assert(tok->valid);
    tok->valid = false;
    fp->pc += offset;       // Jump to specified label
    break;
}
```

---

## 10. Debugging Support

### 10.1 Source Line Tracking

```cpp
// Line number opcodes inserted by compiler
case BC_LINE: {
    uint16_t line = (instr >> 8) & 0xFFFF;
    currentLine = line;

    // Check source breakpoint
    if (debug && debug->shouldBreakOn(fp->func->sourceFileId, line)) {
        return DebugAction::Break;
    }
    break;
}
```

### 10.2 Breakpoint Support

```cpp
case BC_BREAKPOINT: {
    if (debug) {
        return DebugAction::Break;
    }
    break;
}
```

### 10.3 Variable Watches

```cpp
case BC_WATCH_VAR: {
    uint16_t varIdx = (instr >> 8) & 0xFFFF;
    if (debug && debug->hasWatch(varIdx)) {
        debug->onStore(fp->func->localVars[varIdx].name,
                       fp->locals[varIdx]);
    }
    break;
}
```

### 10.4 Single-Stepping

```cpp
BCSlot BytecodeVM::step() {
    // Execute exactly one bytecode instruction
    uint32_t instr = fp->func->code[fp->pc++];
    executeInstruction(instr);

    if (fp->pc >= fp->func->code.size()) {
        return popFrame() ? step() : BCSlot{};
    }
    return BCSlot{};  // Continue stepping
}
```

---

## 11. Memory Management

> **Design note:** The pseudocode below uses `.str` on `BCSlot` for illustration. In the
> actual implementation (`src/bytecode/BytecodeVM.cpp`), strings are represented as `void*`
> in `BCSlot::ptr`. String reference counting is managed via the runtime API using
> `static_cast<rt_string>(slot.ptr)`. The `localTypes` field is not present in
> `BytecodeFunction`; the compiler emits explicit `STR_RETAIN`/`STR_RELEASE` opcodes instead.

### 11.1 String Reference Counting

Strings are retained/released at bytecode boundaries. The compiler emits explicit
`STR_RETAIN` and `STR_RELEASE` instructions rather than tracking types per-local-slot:

```cpp
case BC_STORE_LOCAL: {
    uint8_t idx = (instr >> 8) & 0xFF;
    BCSlot newVal = *--sp;
    fp->locals[idx] = newVal;
    break;
    // Compiler emits STR_RELEASE/STR_RETAIN around stores to string-typed locals
}
```

### 11.2 String Operations

```cpp
case BC_STR_RETAIN: {
    // sp[-1].ptr holds the rt_string (as void*)
    rt_str_retain_maybe(static_cast<rt_string>(sp[-1].ptr));
    break;
}

case BC_STR_RELEASE: {
    rt_str_release_maybe(static_cast<rt_string>((*--sp).ptr));
    break;
}
```

### 11.3 Frame Cleanup

Frame cleanup releases any string locals tracked by the compiler via explicit opcodes.
The `BytecodeVM` does not scan `localTypes` (which doesn't exist); instead the compiled
bytecode contains `STR_RELEASE` instructions at scope exit points.

---

## 12. Runtime Integration

### 12.1 Native Function Calls

```cpp
case BC_CALL_NATIVE: {
    uint16_t fnIdx = (instr >> 8) & 0xFFFF;
    uint8_t argCount = (instr >> 24) & 0xFF;

    const NativeFuncRef& ref = module->nativeFuncs[fnIdx];

    // Pop arguments into temporary buffer
    BCSlot args[16];
    for (int i = argCount - 1; i >= 0; i--) {
        args[i] = *--sp;
    }

    // Call through RuntimeBridge
    BCSlot result = RuntimeBridge::call(
        rtContext,
        ref.name,
        std::span<const BCSlot>(args, argCount),
        {}, "", ""  // Source location for traps
    );

    // Push result if non-void
    if (ref.hasReturn) {
        *sp++ = result;
    }
    break;
}
```

### 12.2 Fast-Path Runtime Calls

Frequently-called runtime functions can have specialized bytecode. Note: `BCSlot` carries
strings as `void*` (no separate `str` field); the pattern below uses `ptr`:

```cpp
// BC_CALL_NATIVE with inline cache for hot functions
case BC_CALL_NATIVE_CACHED: {
    uint16_t fnIdx = (instr >> 8) & 0xFFFF;
    void* cachedHandler = nativeCache[fnIdx];

    if (cachedHandler == &rt_timer_ms) {
        sp->i64 = rt_timer_ms();
        sp++;
    } else if (cachedHandler == &rt_inkey_str) {
        sp->ptr = rt_inkey_str();  // String returned as void*
        sp++;
    } else {
        // Fall back to generic call
        callNativeGeneric(fnIdx);
    }
    break;
}
```

---

## 13. Phase 1 Implementation Plan

### 13.1 Scope

Phase 1 establishes the core bytecode infrastructure with basic functionality:

**In Scope:**
- Basic function calls (IL functions only)
- Bytecode instruction format and encoding
- BytecodeModule and BytecodeFunction data structures
- IL-to-bytecode compiler for arithmetic and control flow
- Simple test programs (fib, factorial, etc.)
- Switch-based interpreter dispatch loop

**Out of Scope for Phase 1:**
- Debugging support
- Exception handling
- Runtime/native function calls
- String/memory management
- Threading

### 13.2 Deliverables

```
src/bytecode/
├── Bytecode.hpp           # Opcode definitions, instruction encoding
├── BytecodeModule.hpp     # Module and function data structures
├── BytecodeCompiler.hpp   # IL-to-bytecode compiler
├── BytecodeCompiler.cpp
├── BytecodeVM.hpp         # Interpreter state and API
└── BytecodeVM.cpp         # Main interpreter loop

src/tests/unit/
└── test_bytecode_vm.cpp   # Bytecode VM unit tests
```

### 13.3 Milestone Criteria

1. Bytecode encoding/decoding roundtrips correctly
2. IL arithmetic programs compile to valid bytecode
3. fib(20) executes correctly on bytecode VM
4. Performance: fib(20) < 100ms (vs current 7000ms)

### 13.4 Estimated LOC

| Component | Lines |
|-----------|-------|
| Bytecode.hpp | ~200 |
| BytecodeModule | ~300 |
| BytecodeCompiler | ~800 |
| BytecodeVM | ~600 |
| Tests | ~400 |
| **Total** | ~2,300 |

---

## 14. Phase 2 Implementation Plan

### 14.1 Scope

Phase 2 adds runtime integration and basic memory management:

**In Scope:**
- Computed goto dispatch (threaded interpreter)
- Global variable access
- Memory load/store operations
- Native function calls via RuntimeBridge
- Performance optimization
- String reference counting

**Deliverables:**
```
src/bytecode/
├── BytecodeVM_Threaded.cpp    # Computed goto dispatch
├── RuntimeIntegration.cpp     # Native call handling
├── MemoryOps.cpp             # Load/store with refcounting
└── tests/
    ├── test_bytecode_runtime.cpp
    ├── test_bytecode_strings.cpp
    └── test_bytecode_memory.cpp
```

### 14.2 Milestone Criteria

1. Runtime functions callable from bytecode
2. String operations work with proper refcounting
3. Memory operations support all IL types
4. Performance: fib(20) < 50ms

---

## 15. Phase 3 Implementation Plan

### 15.1 Scope

Phase 3 adds exception handling and debugging:

**In Scope:**
- Breakpoint support
- Error value manipulation
- Exception handler registration (eh.push/eh.pop)
- Resume operations (same/next/label)
- Single-step execution
- Source line tracking
- Trap propagation and handler dispatch
- Variable watches

**Deliverables:**
```
src/bytecode/
├── ExceptionHandling.cpp     # Trap and handler logic
├── DebugSupport.cpp         # Breakpoints, stepping, watches
└── tests/
    ├── test_bytecode_exceptions.cpp
    ├── test_bytecode_debug.cpp
    └── test_bytecode_stepping.cpp
```

### 15.2 Milestone Criteria

1. Try/catch programs work correctly
2. Traps propagate to handlers
3. Resume operations function properly
4. Breakpoints trigger correctly
5. Single-stepping works

---

## 16. Phase 4 Implementation Plan

### 16.1 Scope

Phase 4 adds threading and CLI integration:

**In Scope:**
- Benchmark suite
- CLI flag `--bytecode` to select execution mode
- Documentation
- Migration plan
- Per-thread VM instances
- Synchronization primitives (via runtime)
- Thread spawning and joining

**Deliverables:**
```
src/bytecode/
├── ThreadSupport.cpp         # Multi-VM threading
└── tests/
    ├── test_bytecode_threading.cpp
    └── test_bytecode_concurrency.cpp

src/tools/viper/
├── cmd_run_bytecode.cpp      # CLI integration

docs/
├── bytecode-vm.md           # User documentation
└── bytecode-internals.md    # Developer documentation
```

### 16.2 Milestone Criteria

1. Threaded programs execute correctly
2. `viper -run --bytecode` works for all test programs
3. Performance within 10-50x of Python
4. All existing tests pass

---

## 17. Testing Strategy

### 17.1 Unit Tests

| Category | Tests |
|----------|-------|
| Encoding | Instruction encode/decode roundtrip |
| Compiler | IL → bytecode for each opcode |
| Interpreter | Each bytecode executes correctly |
| Memory | String refcount, load/store |
| Exceptions | Handler registration, trap, resume |
| Debug | Breakpoints, stepping, watches |

### 17.2 Integration Tests

| Test | Description |
|------|-------------|
| fib.zia | Recursive fibonacci |
| factorial.zia | Iterative factorial |
| strings.zia | String concatenation |
| arrays.zia | Array operations |
| exceptions.zia | Try/catch/finally |
| threads.zia | Thread creation |

### 17.3 Golden Tests

Compare bytecode VM output against current VM output for all existing test programs.

### 17.4 Performance Tests

| Benchmark | Target |
|-----------|--------|
| fib(20) | < 50ms |
| fib(35) | < 30s |
| string_concat(10000) | < 100ms |

---

## 18. Performance Targets

### 18.1 Per-Instruction Targets

| Operation | Target Cycles |
|-----------|---------------|
| Local load/store | 5-10 |
| Arithmetic | 10-20 |
| Comparison | 10-20 |
| Branch | 10-15 |
| Function call | 50-100 |
| Native call | 100-200 |

### 18.2 Aggregate Targets

| Metric | Current | Target | Improvement |
|--------|---------|--------|-------------|
| Cycles/instruction | 174,000 | 100-500 | 350-1700x |
| fib(20) time | 7,050ms | 15-70ms | 100-470x |
| vs Python ratio | 15,000x | 10-50x | 300-1500x |

---

## Appendix A: Opcode Quick Reference

See Section 4 for complete opcode specification.

## Appendix B: File Structure

The bytecode subsystem is fully implemented at `src/bytecode/`:

```
src/bytecode/
├── Bytecode.hpp              # Opcode enum, BCSlot type, kMaxCallDepth, kMaxStackSize
├── Bytecode.cpp              # opcodeName() and opcode table helpers
├── BytecodeCompiler.hpp      # IL → bytecode compiler API
├── BytecodeCompiler.cpp      # Compiler implementation (SSA→locals, linearizer, code gen)
├── BytecodeModule.hpp        # BytecodeModule and BytecodeFunction data structures
└── BytecodeVM.hpp / .cpp     # Interpreter loop, BCFrame, exception handling, debug support
```

Exception handling, runtime integration (via RuntimeBridge), threaded dispatch, and debug
support are all implemented directly in `BytecodeVM.cpp`.

## Appendix C: Migration Path

1. **Phase 1:** Bytecode VM as experimental feature (`--bytecode-experimental`)
2. **Phase 2:** Bytecode VM as optional mode (`--bytecode`)
3. **Phase 3:** Bytecode VM as default (`--legacy-vm` for old)
4. **Phase 4:** Remove legacy VM

---

*Document generated from comprehensive codebase analysis.*
*All features account for existing Viper VM capabilities.*
