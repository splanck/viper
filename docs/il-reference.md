---
status: active
audience: public
last-updated: 2025-11-13
---

# Viper IL — Reference

Complete reference for Viper IL syntax, types, instructions, and verification rules. Each instruction includes minimal examples from real IL code.

For introductory material, see **[IL Quickstart](il-quickstart.md)** or **[IL Guide](il-guide.md)**.

---

## Table of Contents

- [Types](#types)
- [Functions & Blocks](#functions--blocks)
- [Instruction Catalog](#instruction-catalog-by-category)
  - [Arithmetic](#arithmetic)
  - [Comparison](#comparison)
  - [Memory](#memory)
  - [Control](#control)
  - [Exceptions](#exceptions)
  - [Misc](#misc)
- [Verification Rules](#verification-rules-selected-highlights)
- [Tools](#tools)

---

## Types

Viper IL supports the following primitive types:

| Type       | Description |
|------------|-------------|
| **Integers** | `i1`, `i16`, `i32`, `i64` — Frontends typically use `i64` |
| **Floats** | `f64` — Double-precision floating-point |
| **Pointers** | `ptr` — Opaque pointers or typed pointers (build-dependent) |
| **Strings** | `str` — Runtime handle for managed strings |
| **Void** | `void` — No return value (procedures) |
| **Special** | `error`, `resumetok` — Exception handling types |

> **Note:** The verifier validates type correctness. Use `il-verify` to check your IL code.

---

## Functions & Blocks

Functions are the primary unit of IL code:

```il
func @name(<typed args>) -> <type> {
entry:
  ; instructions
}
```

**Structure:**
- **Symbol names**: `@name` for functions and globals
- **Basic blocks**: Labeled with `label:` (no caret prefix)
- **Terminators**: Every block must end with `ret`, `br`, `cbr`, or `switch`
- **SSA form**: Values (`%vX`) are defined once and used multiple times

---

## Instruction Catalog (by Category)

### Arithmetic

**`add`** — Integer addition (no overflow check).

```il
%sum = add %a, %b
%t0 = add 10, 20
```

**`sub`** — Integer subtraction (no overflow check).

```il
%diff = sub %a, %b
%t0 = sub 10, 5
```

**`mul`** — Integer multiplication (no overflow check).

```il
%prod = mul %a, %b
%t0 = mul 3, 7
```

**`addr_of`** — Take address of global variable or constant.

```il
%t41 = addr_of @.Lstr
```

**`fadd`** — Floating-point addition.

```il
%t1 = fadd 1.2345678901234567, 2.5
%t13 = fadd 1.0, 2.5
```

**`fdiv`** — Floating-point division.

```il
%avg = fdiv %fsum, %fn
%t16 = fdiv %t15, 2.0
```

**`fmul`** — Floating-point multiplication.

```il
%t15 = fmul %t14, 4.0
```

**`fsub`** — Floating-point subtraction.

```il
%t14 = fsub %t13, 1.25
%t10 = fsub %t8, %t9
```

**`iadd.ovf`** — Integer addition with overflow check (traps on overflow).

```il
%t0 = iadd.ovf 1, 2
%sum = iadd.ovf %a, %b
```

**`imul.ovf`** — Integer multiplication with overflow check (traps on overflow).

```il
%t1 = imul.ovf %t0, 3
%prod = imul.ovf %a, %b
```

**`isub.ovf`** — Integer subtraction with overflow check (traps on overflow).

```il
%n2 = isub.ovf %n1, 1
%diff = isub.ovf %lhs, %rhs
```

**`sdiv.chk0`** — Signed integer division with divide-by-zero check (traps if divisor is zero).

```il
%t0 = sdiv.chk0 %a, %b
%t3 = sdiv.chk0 %t2, 5
```

**`udiv.chk0`** — Unsigned integer division with divide-by-zero check (traps if divisor is zero).

```il
%t2 = udiv.chk0 %a, %b
%t4 = udiv.chk0 10, 2
```

**`sdiv`** — Signed integer division (no divide-by-zero check; undefined behavior on zero).

```il
%quot = sdiv %a, %b
%t0 = sdiv 20, 4
```

**`udiv`** — Unsigned integer division (no divide-by-zero check; undefined behavior on zero).

```il
%quot = udiv %a, %b
%t0 = udiv 20, 4
```

**`srem`** — Signed integer remainder (no divide-by-zero check; undefined behavior on zero).

```il
%rem = srem %a, %b
%t0 = srem 17, 5
```

**`urem`** — Unsigned integer remainder (no divide-by-zero check; undefined behavior on zero).

```il
%rem = urem %a, %b
%t0 = urem 17, 5
```

**`srem.chk0`** — Signed integer remainder with divide-by-zero check (traps if divisor is zero).

```il
%rem = srem.chk0 %a, %b
%t0 = srem.chk0 17, 5
```

**`urem.chk0`** — Unsigned integer remainder with divide-by-zero check (traps if divisor is zero).

```il
%rem = urem.chk0 %a, %b
%t0 = urem.chk0 17, 5
```

### Comparison

**`fcmp_eq`** — Floating-point comparison.

```il
%t27 = fcmp_eq 1, 1
```

**`fcmp_ge`** — Floating-point comparison.

```il
%t32 = fcmp_ge 3, 3
```

**`fcmp_gt`** — Floating-point comparison.

```il
%t31 = fcmp_gt 3, 2
```

**`fcmp_le`** — Floating-point comparison.

```il
%t30 = fcmp_le 2, 2
%t4 = fcmp_le %t2, %t3
```

**`fcmp_lt`** — Floating-point comparison.

```il
%t29 = fcmp_lt 1, 2
```

**`fcmp_ne`** — Floating-point comparison.

```il
%t28 = fcmp_ne 1, 2
%t20 = fcmp_ne %t18, %t18
```

**`icmp_eq`** — Integer comparison; suffix indicates predicate.

```il
%t17 = icmp_eq 1, 1
%c1 = icmp_eq 0, 0
```

**`icmp_ne`** — Integer comparison; suffix indicates predicate.

```il
%t18 = icmp_ne 1, 0
%t6 = icmp_ne %t5, 0
```

**`scmp_ge`** — Signed integer greater-than-or-equal comparison; returns i1 (0 or 1).

```il
%cond = scmp_ge %i, 3
%cond = scmp_ge %i, 10
```

**`scmp_gt`** — Signed integer greater-than comparison; returns i1 (0 or 1).

```il
%c = scmp_gt %n1, 1
%cond = scmp_gt %yv1, 8
```

**`scmp_le`** — Signed integer less-than-or-equal comparison; returns i1 (0 or 1).

```il
%c = scmp_le %i0, 10
%oc = scmp_le %i0, %n0
```

**`scmp_lt`** — Signed integer less-than comparison; returns i1 (0 or 1).

```il
%c = scmp_lt %i0, %n1
%c = scmp_lt %i, 10
```

**`ucmp_ge`** — Unsigned integer greater-than-or-equal comparison; returns i1 (0 or 1).

```il
%t26 = ucmp_ge 3, 3
```

**`ucmp_gt`** — Unsigned integer greater-than comparison; returns i1 (0 or 1).

```il
%t25 = ucmp_gt 3, 2
```

**`ucmp_le`** — Unsigned integer less-than-or-equal comparison; returns i1 (0 or 1).

```il
%t24 = ucmp_le 2, 2
```

**`ucmp_lt`** — Unsigned integer less-than comparison; returns i1 (0 or 1).

```il
%t23 = ucmp_lt 1, 2
```

### Memory

**`alloca`** — Allocate stack memory; operand is size in bytes; returns pointer.

```il
%a_slot = alloca 8
%b_slot = alloca 8
```

**`gep`** — Get element pointer; compute address offset from base pointer (base + offset).

```il
%elem_ptr = gep %a0, %off
%t39 = gep %t38, 1
```

**`load`** — Load from memory.

```il
%a0 = load str, %a_slot
%c1 = load str, %c_slot
```

**`store`** — Store to memory.

```il
store str, %a_slot, %sA
store str, %b_slot, %sB
```

### Control

**`br`** — Conditional or unconditional branch to labels.

```il
br label exit
br label exit
```

**`call`** — Call a function; arguments must match parameter types.

```il
%c0 = call @rt_concat(%a0, %sSp)
%c2 = call @rt_concat(%c1, %b0)
```

**`call.indirect`** — Call through a function pointer; first operand is the function pointer, followed by arguments.

```il
%result = call.indirect %fn_ptr(%arg1, %arg2)
%val = call.indirect %callback(%data)
```

**`cbr`** — Conditional branch; takes a boolean condition and two target labels.

```il
cbr %eq, label then1, label else0
cbr %c, label loop_body, label done
```

**`ret`** — Return from function.

```il
ret 0
ret 0
```

**`switch.i32`** — Multi-way branch.

```il
switch.i32 %sel, ^default(%sel), 0 -> ^case0(%sel)
switch.i32 %arg, ^default(%arg) junk, 0 -> ^case0(%arg)
```


### Exceptions

**`eh.entry`** — Mark entry point of an error handler block.

```il
eh.entry
eh.entry
```

**`eh.pop`** — Pop error handler from stack; restores previous error handler.

```il
eh.pop
eh.pop
```

**`eh.push`** — Push error handler onto stack; specifies label to branch to on error.

```il
eh.push label bad
eh.push label handler
```

**`trap`** — Unconditional trap; immediately terminates execution with error.

```il
trap
trap
```

**`trap.err`** — Create an error value from error code (i32) and message string.

```il
%err0 = trap.err 7, %msg0
%err1 = trap.err 9, %msg1
```

**`trap.from_err`** — Trap from error code; terminates with specified error type and code.

```il
trap.from_err i32 7
trap.from_err i32 6
```

**`trap.kind`** — Read current trap kind from most recent error.

```il
%kind = trap.kind
%k0 = trap.kind
```

### Misc

**`and`** — Bitwise AND operation on integer values.

```il
%t7 = and 240, 15
%t24 = and %t21, %t23
```

**`ashr`** — Arithmetic shift right; preserves sign bit for signed integers.

```il
%t12 = ashr -8, 1
```

**`cast.fp_to_si.rte.chk`** — Cast float to signed integer with round-to-even and overflow check (traps on overflow).

```il
%t0 = cast.fp_to_si.rte.chk %fp
%t35 = cast.fp_to_si.rte.chk 5.5
```

**`cast.fp_to_ui.rte.chk`** — Cast float to unsigned integer with round-to-even and overflow check (traps on overflow).

```il
%t1 = cast.fp_to_ui.rte.chk %fp
```

**`cast.si_narrow.chk`** — Narrow signed integer with overflow check (e.g., i64 to i32; traps on overflow).

```il
%t2 = cast.si_narrow.chk %si
```

**`cast.si_to_fp`** — Cast signed integer to floating-point.

```il
%t4 = cast.si_to_fp %si
```

**`cast.ui_narrow.chk`** — Narrow unsigned integer with overflow check (e.g., i64 to i32; traps on overflow).

```il
%t3 = cast.ui_narrow.chk %ui
```

**`cast.ui_to_fp`** — Cast unsigned integer to floating-point.

```il
%t5 = cast.ui_to_fp %ui
```

**`const_null`** — Create null pointer constant.

```il
%t43 = const_null
%p = const_null
```

**`const_str`** — Load string constant from global string literal.

```il
%sA = const_str @.L0
%sB = const_str @.L1
```

**`err.get_kind`** — Extract error kind from error value.

```il
%k = err.get_kind %e
%k = err.get_kind %err
```

**`err.get_code`** — Extract error code from error value.

```il
%code = err.get_code %err
%c = err.get_code %e
```

**`err.get_ip`** — Extract instruction pointer from error value.

```il
%ip = err.get_ip %err
%ptr = err.get_ip %e
```

**`err.get_line`** — Extract line number from error value.

```il
%ln = err.get_line %e
%line = err.get_line %err
```

**`extern`** — Declare external function signature (from runtime or other modules).

```il
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
```

**`fptosi`** — Convert floating-point to signed integer (truncates toward zero; may trap on overflow).

```il
%t0 = fptosi %f
%t34 = fptosi 5.5
```

**`func`** — Begin function definition with signature.

```il
func @main() -> i64 {
func @add(i64, i64) -> i64 {
```

**`global`** — Define global constant or variable.

```il
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
```

**`idx.chk`** — Check array index bounds; traps if index is out of range [lo, hi].

```il
%t0 = idx.chk %idx16, %lo16, %hi16
%t1 = idx.chk %idx32, %lo32, %hi32
```

**`il`** — Specify IL version (header directive at top of file).

```il
il 0.1
il 0.1.2
```

**`lshr`** — Logical shift right; fills with zeros (unsigned shift).

```il
%t11 = lshr %t10, 2
```

**`or`** — Bitwise OR operation on integer values.

```il
%t8 = or %t7, 1
%t16 = or %t13, %t15
```

**`resume.label`** — Resume execution at specified label after error; used with ON ERROR GOTO/RESUME.

```il
resume.label %tok, label after(%err)
resume.label %tok, label target(%err)
```

**`resume.next`** — Resume execution at next statement after error; used with RESUME NEXT.

```il
resume.next %tok
resume.next %token
```

**`resume.same`** — Resume execution at same statement that caused error; used with RESUME 0.

```il
resume.same %tok
resume.same %tok
```

**`shl`** — Shift left; fills with zeros.

```il
%off = shl %i0, 3
%t10 = shl %t9, 1
```

**`sitofp`** — Convert signed integer to floating-point.

```il
%fsum = sitofp %sum2
%fn = sitofp %n2
```

**`target`** — Specify target architecture or platform.

```il
target wasm32-unknown-unknown
target "wasm32-unknown-unknown"
```

**`trunc1`** — Truncate integer to i1 (single bit boolean).

```il
%t37 = trunc1 255
%t1 = trunc1 0
```

**`xor`** — Bitwise exclusive-OR.

```il
%t9 = xor %t8, 3
%t6 = xor %t5, 0
```

**`zext1`** — Zero-extend i1 (single bit) to i64 (0 becomes 0, 1 becomes 1).

```il
%t36 = zext1 %t18
%t7 = zext1 %t6
```

## Verification rules (selected highlights)

The verifier enforces structural and type rules. Typical checks include:

- Block must end with a terminator (`ret`, `br`, or `switch`).
- Types of operands must match instruction variants (e.g., `add.i64`).
- PHI nodes must list all predecessors in order.
- Branch targets must be valid labels in the same function.
- Calls must match callee signature exactly.

## Tools

- `il-verify <file.il>` — static checks with precise diagnostics.
- `il-dis <file.il>` — pretty-printer / disassembler.
- `ilc -run <file.il>` — run on the VM (uses runtime bridges).
- `ilc opt <file.il> -p simplifycfg` — run transforms (e.g., SimplifyCFG).

> Use `--help` on each tool for full options.
