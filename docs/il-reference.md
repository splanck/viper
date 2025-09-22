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
| Type | Meaning | Notes |
|------|---------|-------|
| `void` | no value | function return only |
| `i1` | boolean | produced by comparisons and `trunc1` |
| `i64` | 64-bit signed int | wrap on overflow |
| `f64` | 64-bit IEEE float | NaN/Inf propagate |
| `ptr` | untyped pointer | byte-addressed |
| `str` | opaque string handle | managed by runtime |

## Constants & Literals
Integers use decimal notation (`-?[0-9]+`).  Floats use decimal with optional fraction (`-?[0-9]+(\.[0-9]+)?`) and permit `NaN`, `Inf`, and `-Inf`.  Booleans `true`/`false` sugar to `i1` values `1`/`0`.  Strings appear in quotes with escapes `\"`, `\\`, `\n`, `\t`, `\xNN`.  `const_null` yields a `ptr` null.

## Basic Blocks
Functions contain one or more labelled blocks.  A block may declare parameters; each predecessor must supply matching arguments.

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
Abort execution with a runtime trap.

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
* Block parameters are unique and all predecessors pass matching arguments.

## Text Grammar (EBNF)
```ebnf
module      ::= "il" VERSION decl*
VERSION     ::= NUMBER "." NUMBER ("." NUMBER)?
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

## Versioning & Conformance
Modules must begin with `il 0.1.2`.  A conforming implementation accepts this grammar, obeys the semantics above, and traps on the conditions listed for each instruction.
