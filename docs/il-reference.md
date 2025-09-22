<!--
SPDX-License-Identifier: MIT
File: docs/il-reference.md
Purpose: Reference for Viper IL v0.1.2.
-->

# IL Reference (v0.1.2)

> Start here: [IL Quickstart](il-quickstart.md) for a hands-on introduction.

## Overview
Viper IL is the project’s "thin waist" intermediate language designed to sit between diverse front ends and back ends. Its goals are:

* **Determinism** – VM and native back ends must produce identical observable behaviour.
* **Explicit control flow** – each basic block ends with exactly one terminator; no fallthrough.
* **Static types** – a minimal set of primitive types (`i1`, `i64`, `f64`, `ptr`, `str`, `void`).

Execution is organized as functions consisting of labelled basic blocks.  Modules may execute either under the IL virtual machine interpreter or after lowering to native code through a C runtime.  Front ends such as BASIC first lower into IL patterns described in [BASIC lowering](lowering/basic-to-il.md).

## Module & Function Syntax
An IL module is a set of declarations and function definitions.  It starts with a version line:

```text
il 0.1.2
```

An optional `target "..."` metadata line may follow.  The VM ignores it, but native back ends can use it as advisory information.

```text
il 0.1.2
target "x86_64-sysv"
```

See [examples/il](examples/il/) for complete programs.

Each function has the form:

```
fn @name(param_list?) -> ret_type {
entry:
  ...
}
```

### Minimal Function
```text
il 0.1.2
fn @main() -> i64 {
entry:
  ret 0
}
```

### Extern Declarations
External functions are declared with `extern` and may be called like normal functions.

```text
il 0.1.2
extern @rt_print_i64(i64) -> void
fn @main() -> i64 {
entry:
  call @rt_print_i64(42)
  ret 0
}
```

### Global Constants
Module-level constants bind a symbol to immutable data such as strings.

```text
il 0.1.2
global const str @.msg = "hi"
fn @main() -> i64 {
entry:
  %s = const_str @.msg
  ret 0
}
```

## Types
| Type | Meaning | Alignment | Notes |
|------|---------|-----------|-------|
| `void` | no value | — | function return only |
| `i1` | boolean | 1 | produced by comparisons and `trunc1` |
| `i64` | 64-bit signed int | 8 | wrap on overflow |
| `f64` | 64-bit IEEE float | 8 | NaN/Inf propagate |
| `ptr` | untyped pointer | 8 | byte-addressed |
| `str` | opaque string handle | 8 | managed by runtime |

## Constants & Literals
Integers use decimal notation (`-?[0-9]+`).  Floats use decimal with optional fraction (`-?[0-9]+(\.[0-9]+)?`) and permit `NaN`, `Inf`, and `-Inf`.  Booleans `true`/`false` sugar to `i1` values `1`/`0`.  Strings appear in quotes with escapes `\"`, `\\`, `\n`, `\t`, `\xNN`.  `const_null` yields a `ptr` null.

## Basic Blocks
Functions contain one or more labelled blocks.  Labels end in `:` and the first block is `entry`.  A block may declare parameters; each predecessor must supply matching arguments.  Omitting the argument list is shorthand for passing no values (for example, `br next` is the same as `br next()`).

```text
il 0.1.2
fn @loop(%n: i64) -> i64 {
entry(%n: i64):
  br body(%n, 0)
body(%i: i64, %acc: i64):
  %cmp = scmp_ge %i, %n
  cbr %cmp, done(%acc), body(%i, %acc) ; illustrative loop
  trap ; unreachable
 done(%r: i64):
  ret %r
}
```

## Control Flow
### `br`
Unconditional branch to a block with optional arguments.

```text
br next(%v)
```

### `cbr`
Conditional branch on an `i1` value.

```text
cbr %cond, then, else
```

### `ret`
Return from the current function.

### `trap`
Abort execution with an unconditional runtime trap.

```text
fn @oops() -> void {
entry:
  trap
}
```

## Instructions
Each non-terminator instruction optionally assigns to a `%temp` and produces a result.  Below, `x` and `y` denote operands.

### Integer Arithmetic
| Instr | Form | Result |
|-------|------|--------|
| `add` | `add x, y` | `i64` |
| `sub` | `sub x, y` | `i64` |
| `mul` | `mul x, y` | `i64` |
| `sdiv` | `sdiv x, y` | `i64` (trap on divide-by-zero or `INT64_MIN / -1`) |
| `udiv` | `udiv x, y` | `i64` (trap on divide-by-zero) |
| `srem` | `srem x, y` | `i64` (trap on divide-by-zero) |
| `urem` | `urem x, y` | `i64` (trap on divide-by-zero) |

`sdiv` and `srem` follow C semantics: the quotient is truncated toward zero and the remainder keeps the dividend's sign.  Front ends such as BASIC map `\` to `sdiv` and `MOD` to `srem`.

```text
il 0.1.2
fn @main() -> i64 {
entry:
  %t0 = add 2, 3
  ret %t0
}
```

### Bitwise and Shifts
| Instr | Form | Result |
|-------|------|--------|
| `and` | `and x, y` | `i64` |
| `or`  | `or x, y`  | `i64` |
| `xor` | `xor x, y` | `i64` |
| `shl` | `shl x, y` | `i64` |
| `lshr`| `lshr x, y`| `i64` |
| `ashr`| `ashr x, y`| `i64` |

Shift counts are masked modulo 64, matching the behaviour of x86-64 shifts.

```text
%r = xor 0b1010, 0b1100
```

### Floating-Point Arithmetic
| Instr | Form | Result |
|-------|------|--------|
| `fadd` | `fadd x, y` | `f64` |
| `fsub` | `fsub x, y` | `f64` |
| `fmul` | `fmul x, y` | `f64` |
| `fdiv` | `fdiv x, y` | `f64` |

```text
%f = fmul 2.0, 4.0
```

### Comparisons
| Instrs | Form | Result |
|--------|------|--------|
| `icmp_eq`, `icmp_ne` | `icmp_eq x, y` | `i1` |
| `scmp_lt`, `scmp_le`, `scmp_gt`, `scmp_ge` | `scmp_lt x, y` | `i1` signed compare |
| `ucmp_lt`, `ucmp_le`, `ucmp_gt`, `ucmp_ge` | `ucmp_lt x, y` | `i1` unsigned compare |
| `fcmp_lt`, `fcmp_le`, `fcmp_gt`, `fcmp_ge`, `fcmp_eq`, `fcmp_ne` | `fcmp_eq x, y` | `i1` (`NaN` makes `fcmp_eq` false and `fcmp_ne` true) |

```text
%cond = scmp_lt %a, %b
```

### Conversions
| Instr | Form | Result |
|-------|------|--------|
| `sitofp` | `sitofp x` | `f64` |
| `fptosi` | `fptosi x` | `i64` (trap on NaN or overflow) |
| `zext1`  | `zext1 x`  | `i64` |
| `trunc1` | `trunc1 x` | `i1` |

```text
%f = sitofp 42
```

### Memory Operations
| Instr | Form | Result |
|-------|------|--------|
| `alloca` | `alloca size` | `ptr` (size < 0 traps; memory zero-initialized) |
| `gep`    | `gep ptr, offs` | `ptr` (no bounds checks) |
| `load`   | `load type, ptr` | `type` (null or misaligned trap) |
| `store`  | `store type, ptr, value` | — (null or misaligned trap) |
| `addr_of`| `addr_of @global` | `ptr` |
| `const_str` | `const_str @label` | `str` |
| `const_null`| `const_null` | `ptr` |

`i64`, `f64`, `ptr`, and `str` loads and stores require 8-byte alignment; misaligned or null accesses trap.  Stack allocations created by `alloca` are zero-initialized and live until the function returns.

```text
fn @main() -> i64 {
entry:
  %p = alloca 8
  store i64, %p, 7
  %v = load i64, %p
  ret %v
}
```

### Calls
Call a function or extern; verifier checks arity and types.

```text
call @f(%x, %y)
```

## Runtime ABI
The IL runtime provides helper functions used by front ends and tests:

| Function | Signature | Notes |
|----------|-----------|-------|
| `@rt_print_str` | `str -> void` | write string to stdout |
| `@rt_print_i64` | `i64 -> void` | write integer to stdout |
| `@rt_print_f64` | `f64 -> void` | write float to stdout |
| `@rt_input_line` | `-> str` | read line from stdin, newline stripped |
| `@rt_len` | `str -> i64` | length in bytes |
| `@rt_concat` | `str × str -> str` | concatenate strings |
| `@rt_substr` | `str × i64 × i64 -> str` | indices clamp; negative bounds trap |
| `@rt_to_int` | `str -> i64` | traps on invalid numeric |
| `@rt_to_float` | `str -> f64` | traps on invalid numeric |
| `@rt_str_eq` | `str × str -> i1` | string equality |
| `@rt_alloc` | `i64 -> ptr` | allocate bytes; negative size traps |
| `@rt_free` | `ptr -> void` | deallocate buffer (optional in v0.1.2) |

Strings are reference-counted by the runtime implementation.  See [runtime/](runtime/) for additional details.

## Memory Model
IL v0.1.2 is single-threaded.  Pointers are plain addresses with no aliasing rules beyond the type requirements of `load` and `store`.  Memory obtained through `alloca` or the runtime follows the alignment rules above, and invalid accesses (null or misaligned) trap deterministically.

## Source Locations
`.loc file line col` annotates instructions with source information.  It has no semantic effect.

```text
.loc 1 3 4
%v = add 1, 2
```

## Verifier Rules
* First block is `entry` and every block ends with exactly one terminator.
* All referenced labels exist in the same function.
* Operand and result types match instruction signatures.
* Calls match callee arity and types.
* `load`/`store` use `ptr` operands and non-void element types.
* `alloca` sizes are `i64`; constant operands must be non-negative.
* Temporaries are defined before use within a block (cross-block dominance checks may be deferred).
* Block parameters have unique names, non-void types, and each predecessor passes matching arguments.
* `cbr` takes an `i1` condition.

Example diagnostics:

```text
L(%x: i64, %x: i64):
              ^ duplicate param %x

br L(1.0)
       ^ arg type mismatch: expected i64, got f64
```

## Text Grammar (EBNF)
```ebnf
module      ::= "il" VERSION (target_decl)? decl*
VERSION     ::= NUMBER "." NUMBER ("." NUMBER)?
target_decl ::= "target" STRING
decl        ::= extern | global | func
extern      ::= "extern" SYMBOL "(" type_list? ")" "->" type
global      ::= "global" ("const")? type SYMBOL "=" ginit
ginit       ::= STRING | INT | FLOAT | "null" | SYMBOL
func        ::= "fn" SYMBOL "(" params? ")" "->" type "{" block+ "}"
params      ::= param ("," param)*
param       ::= IDENT ":" type
type_list   ::= type ("," type)*
block       ::= LABEL ("(" params? ")")? ":" instr* term
instr       ::= TEMP "=" op | op
term        ::= "ret" value? | "br" LABEL ("(" value_list? ")")? | "cbr" value "," LABEL ("(" value_list? ")")?," LABEL ("(" value_list? ")")? | "trap"
op          ::= "add" value "," value | "sub" value "," value | "mul" value "," value | "sdiv" value "," value |
                "udiv" value "," value | "srem" value "," value | "urem" value "," value |
                "and" value "," value | "or" value "," value | "xor" value "," value |
                "shl" value "," value | "lshr" value "," value | "ashr" value "," value |
                "fadd" value "," value | "fsub" value "," value | "fmul" value "," value | "fdiv" value "," value |
                "icmp_eq" value "," value | "icmp_ne" value "," value |
                "scmp_lt" value "," value | "scmp_le" value "," value |
                "scmp_gt" value "," value | "scmp_ge" value "," value |
                "ucmp_lt" value "," value | "ucmp_le" value "," value |
                "ucmp_gt" value "," value | "ucmp_ge" value "," value |
                "fcmp_lt" value "," value | "fcmp_le" value "," value |
                "fcmp_gt" value "," value | "fcmp_ge" value "," value |
                "fcmp_eq" value "," value | "fcmp_ne" value "," value |
                "sitofp" value | "fptosi" value | "zext1" value | "trunc1" value |
                "alloca" value | "gep" value "," value |
                "load" type "," value | "store" type "," value "," value |
                "addr_of" SYMBOL | "const_str" SYMBOL | "const_null" |
                "call" SYMBOL "(" args? ")"
args        ::= value ("," value)*
value_list  ::= value ("," value)*
value       ::= TEMP | SYMBOL | literal
literal     ::= INT | FLOAT | STRING | "true" | "false" | "null"
type        ::= "void" | "i1" | "i64" | "f64" | "ptr" | "str"
```

## Calling Convention (SysV x64)
Native back ends target the System V x86-64 ABI:

* Integer and pointer arguments: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`.
* Floating-point arguments: `xmm0`–`xmm7`.
* Return values: integers/pointers in `rax`, floats in `xmm0`.
* Call sites maintain 16-byte stack alignment; `i1` arguments are zero-extended to 32 bits.

## Versioning & Conformance
Modules must begin with `il 0.1.2`.  A conforming implementation accepts this grammar, obeys the semantics above, and traps on the conditions listed for each instruction.  Implementations are validated against the sample suite under [examples/il](examples/il/).
