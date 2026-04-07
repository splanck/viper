---
status: active
audience: public
last-verified: 2026-04-05
---

# Viper IL — Reference

Complete reference for Viper IL syntax, types, instructions, and verification rules. Each instruction includes minimal
examples from real IL code.

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

| Type         | Description                                               |
|--------------|-----------------------------------------------------------|
| **Integers** | `i1`, `i16`, `i32`, `i64` — Frontends typically use `i64` |
| **Floats**   | `f64` — Double-precision floating-point                   |
| **Pointers** | `ptr` — Opaque pointers (no element type)                 |
| **Strings**  | `str` — Runtime handle for managed strings                |
| **Void**     | `void` — No return value (procedures)                     |
| **Special**  | `error`, `resumetok` — Exception handling types           |

> **Note:** The verifier validates type correctness. Use `il-verify` to check your IL code.

---

## Functions & Blocks

Functions are the primary unit of IL code:

```llvm
func @name(<typed args>) -> <type> {
entry:
  ; instructions
}
```

**Structure:**

- **Symbol names**: `@name` for functions and globals
- **Basic blocks**: Labeled with `label:` (no caret prefix)
- **Terminators**: Every block must end with `ret`, `br`, `cbr`, `switch.i32`, `trap`, `trap.from_err`, or `resume.*`
- **SSA form**: Values (`%vX`) are defined once and used multiple times

---

## Instruction Catalog (by Category)

### Arithmetic

**`add`** — Integer addition (no overflow check).

```llvm
%sum = add %a, %b
%t0 = add 10, 20
```

**`fadd`** — Floating-point addition.

```llvm
%t1 = fadd 1.2345678901234567, 2.5
%t13 = fadd 1.0, 2.5
```

**`fdiv`** — Floating-point division.

```llvm
%avg = fdiv %fsum, %fn
%t16 = fdiv %t15, 2.0
```

**`fmul`** — Floating-point multiplication.

```llvm
%t15 = fmul %t14, 4.0
```

**`fsub`** — Floating-point subtraction.

```llvm
%t14 = fsub %t13, 1.25
%t10 = fsub %t8, %t9
```

**`iadd.ovf`** — Integer addition with overflow check (traps on overflow).

```llvm
%t0 = iadd.ovf 1, 2
%sum = iadd.ovf %a, %b
```

**`imul.ovf`** — Integer multiplication with overflow check (traps on overflow).

```llvm
%t1 = imul.ovf %t0, 3
%prod = imul.ovf %a, %b
```

**`isub.ovf`** — Integer subtraction with overflow check (traps on overflow).

```llvm
%n2 = isub.ovf %n1, 1
%diff = isub.ovf %lhs, %rhs
```

**`mul`** — Integer multiplication (no overflow check).

```llvm
%prod = mul %a, %b
%t0 = mul 3, 7
```

**`sdiv`** — Signed integer division (no divide-by-zero check; undefined behavior on zero).

```llvm
%quot = sdiv %a, %b
%t0 = sdiv 20, 4
```

**`sdiv.chk0`** — Signed integer division with divide-by-zero check (traps if divisor is zero).

```llvm
%t0 = sdiv.chk0 %a, %b
%t3 = sdiv.chk0 %t2, 5
```

**`srem`** — Signed integer remainder (no divide-by-zero check; undefined behavior on zero).

```llvm
%rem = srem %a, %b
%t0 = srem 17, 5
```

**`srem.chk0`** — Signed integer remainder with divide-by-zero check (traps if divisor is zero).

```llvm
%rem = srem.chk0 %a, %b
%t0 = srem.chk0 17, 5
```

**`sub`** — Integer subtraction (no overflow check).

```llvm
%diff = sub %a, %b
%t0 = sub 10, 5
```

**`udiv`** — Unsigned integer division (no divide-by-zero check; undefined behavior on zero).

```llvm
%quot = udiv %a, %b
%t0 = udiv 20, 4
```

**`udiv.chk0`** — Unsigned integer division with divide-by-zero check (traps if divisor is zero).

```llvm
%t2 = udiv.chk0 %a, %b
%t4 = udiv.chk0 10, 2
```

**`urem`** — Unsigned integer remainder (no divide-by-zero check; undefined behavior on zero).

```llvm
%rem = urem %a, %b
%t0 = urem 17, 5
```

**`urem.chk0`** — Unsigned integer remainder with divide-by-zero check (traps if divisor is zero).

```llvm
%rem = urem.chk0 %a, %b
%t0 = urem.chk0 17, 5
```

### Comparison

**`fcmp_eq`** — Floating-point comparison.

```llvm
%t27 = fcmp_eq 1, 1
```

**`fcmp_ge`** — Floating-point comparison.

```llvm
%t32 = fcmp_ge 3, 3
```

**`fcmp_gt`** — Floating-point comparison.

```llvm
%t31 = fcmp_gt 3, 2
```

**`fcmp_le`** — Floating-point comparison.

```llvm
%t30 = fcmp_le 2, 2
%t4 = fcmp_le %t2, %t3
```

**`fcmp_lt`** — Floating-point comparison.

```llvm
%t29 = fcmp_lt 1, 2
```

**`fcmp_ne`** — Floating-point comparison.

```llvm
%t28 = fcmp_ne 1, 2
%t20 = fcmp_ne %t18, %t18
```

**`fcmp_ord`** — Floating-point ordered comparison; returns 1 (true) if both operands are non-NaN.

```llvm
%t0 = fcmp_ord %a, %b
```

**`fcmp_uno`** — Floating-point unordered comparison; returns 1 (true) if either operand is NaN.

```llvm
%t0 = fcmp_uno %a, %b
```

**`icmp_eq`** — Integer comparison; suffix indicates predicate.

```llvm
%t17 = icmp_eq 1, 1
%c1 = icmp_eq 0, 0
```

**`icmp_ne`** — Integer comparison; suffix indicates predicate.

```llvm
%t18 = icmp_ne 1, 0
%t6 = icmp_ne %t5, 0
```

**`scmp_ge`** — Signed integer greater-than-or-equal comparison; returns i1 (0 or 1).

```llvm
%cond = scmp_ge %i, 3
%cond = scmp_ge %i, 10
```

**`scmp_gt`** — Signed integer greater-than comparison; returns i1 (0 or 1).

```llvm
%c = scmp_gt %n1, 1
%cond = scmp_gt %yv1, 8
```

**`scmp_le`** — Signed integer less-than-or-equal comparison; returns i1 (0 or 1).

```llvm
%c = scmp_le %i0, 10
%oc = scmp_le %i0, %n0
```

**`scmp_lt`** — Signed integer less-than comparison; returns i1 (0 or 1).

```llvm
%c = scmp_lt %i0, %n1
%c = scmp_lt %i, 10
```

**`ucmp_ge`** — Unsigned integer greater-than-or-equal comparison; returns i1 (0 or 1).

```llvm
%t26 = ucmp_ge 3, 3
```

**`ucmp_gt`** — Unsigned integer greater-than comparison; returns i1 (0 or 1).

```llvm
%t25 = ucmp_gt 3, 2
```

**`ucmp_le`** — Unsigned integer less-than-or-equal comparison; returns i1 (0 or 1).

```llvm
%t24 = ucmp_le 2, 2
```

**`ucmp_lt`** — Unsigned integer less-than comparison; returns i1 (0 or 1).

```llvm
%t23 = ucmp_lt 1, 2
```

### Memory

**`addr_of`** — Take address of global variable or constant.

```llvm
%t41 = addr_of @.Lstr
```

**`alloca`** — Allocate stack memory; operand is size in bytes; returns pointer.

```llvm
%a_slot = alloca 8
%b_slot = alloca 8
```

**`gaddr`** — Address of a mutable module-level global (modvar).

```llvm
%p = gaddr @.Counter        # pointer to module variable storage
store i64, %p, 1            # write 1 into the counter
```

**`gep`** — Get element pointer; compute address offset from base pointer (base + offset).

```llvm
%elem_ptr = gep %a0, %off
%t39 = gep %t38, 1
```

**`load`** — Load from memory.

```llvm
%a0 = load str, %a_slot
%c1 = load str, %c_slot
```

**`store`** — Store to memory.

```llvm
store str, %a_slot, %sA
store str, %b_slot, %sB
```

### Control

**`br`** — Unconditional branch to a target label.

```llvm
br exit
br loop_header(%i)
```

**`call`** — Call a function; arguments must match parameter types.

```llvm
%c0 = call @Viper.String.Concat(%a0, %sSp)
%c2 = call @Viper.String.Concat(%c1, %b0)
```

**`call.indirect`** — Call through a function pointer; first operand is the function pointer, followed by arguments.

```llvm
%result = call.indirect %fn_ptr(%arg1, %arg2)
%val = call.indirect %callback(%data)
```

**`cbr`** — Conditional branch; takes a boolean condition and two target labels.

```llvm
cbr %eq, then1, else0
cbr %c, loop_body, done
```

**`ret`** — Return from function.

```llvm
ret 0
ret 0
```

**`switch.i32`** — Multi-way branch.

```llvm
switch.i32 %sel, ^default(%sel), 0 -> ^case0(%sel)
switch.i32 %arg, ^default(%arg), 0 -> ^case0(%arg), 1 -> ^case1(%arg)
```

### Exceptions

**`eh.entry`** — Mark entry point of an error handler block.

```llvm
eh.entry
eh.entry
```

**`eh.pop`** — Pop error handler from stack; restores previous error handler.

```llvm
eh.pop
eh.pop
```

**`eh.push`** — Push error handler onto stack; specifies label to branch to on error.

```llvm
eh.push ^bad
eh.push ^handler
```

**`trap`** — Unconditional trap; immediately terminates execution with error.

```llvm
trap
trap
```

**`trap.err`** — Create an error value from error code (i32) and message string.

```llvm
%err0 = trap.err 7, %msg0
%err1 = trap.err 9, %msg1
```

**`trap.from_err`** — Trap from error code; terminates with specified error type and code.

```llvm
trap.from_err i32 7
trap.from_err i32 6
```

**`trap.kind`** — Read current trap kind from most recent error.

```llvm
%kind = trap.kind
%k0 = trap.kind
```

### Misc

**`and`** — Bitwise AND operation on integer values.

```llvm
%t7 = and 240, 15
%t24 = and %t21, %t23
```

**`ashr`** — Arithmetic shift right; preserves sign bit for signed integers.

```llvm
%t12 = ashr -8, 1
```

**`cast.fp_to_si.rte.chk`** — Cast float to signed integer with round-to-even and overflow check (traps on overflow).

```llvm
%t0 = cast.fp_to_si.rte.chk %fp
%t35 = cast.fp_to_si.rte.chk 5.5
```

**`cast.fp_to_ui.rte.chk`** — Cast float to unsigned integer with round-to-even and overflow check (traps on overflow).

```llvm
%t1 = cast.fp_to_ui.rte.chk %fp
```

**`cast.si_narrow.chk`** — Narrow signed integer with overflow check (i64 to i32 or i16; traps on overflow). The result type must be declared on the register: `%name:i32 = cast.si_narrow.chk %val`.

```llvm
%t2:i32 = cast.si_narrow.chk %si
```

**`cast.si_to_fp`** — Cast signed integer to floating-point.

```llvm
%t4 = cast.si_to_fp %si
```

**`cast.ui_narrow.chk`** — Narrow unsigned integer with overflow check (i64 to i32 or i16; traps on overflow). The result type must be declared on the register: `%name:i32 = cast.ui_narrow.chk %val`.

```llvm
%t3:i32 = cast.ui_narrow.chk %ui
```

**`cast.ui_to_fp`** — Cast unsigned integer to floating-point.

```llvm
%t5 = cast.ui_to_fp %ui
```

**`const.f64`** — Load a 64-bit floating-point constant.

```llvm
%pi = const.f64 3.141592653589793
%t0 = const.f64 0.0
```

**`const_null`** — Create null pointer constant.

```llvm
%t43 = const_null
%p = const_null
```

**`const_str`** — Load string constant from global string literal.

```llvm
%sA = const_str @.L0
%sB = const_str @.L1
```

**`err.get_code`** — Extract error code from error value; returns `i32`.

```llvm
%code = err.get_code %err
%c = err.get_code %e
```

**`err.get_ip`** — Extract instruction pointer from error value; returns `i64`.

```llvm
%ip = err.get_ip %err
%ptr = err.get_ip %e
```

**`err.get_kind`** — Extract error kind from error value; returns `i32`.

```llvm
%k = err.get_kind %e
%k = err.get_kind %err
```

**`err.get_line`** — Extract line number from error value; returns `i32`.

```llvm
%ln = err.get_line %e
%line = err.get_line %err
```

**`err.get_msg`** — Extract error message string from the current trap context; returns `str`. Used by `catch(e)` to retrieve the `throw` message.

```llvm
%msg = err.get_msg
```

**`extern`** — Declare external function signature (from runtime or other modules).

```llvm
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
```

Compatibility:

- When built with `-DVIPER_RUNTIME_NS_DUAL=ON`, legacy `@rt_*` externs are accepted as aliases of `@Viper.*`.
- New code should emit `@Viper.*`.

**`fptosi`** — Convert floating-point to signed integer (no check; undefined on NaN or overflow; use `cast.fp_to_si.rte.chk` for checked conversion).

```llvm
%i = fptosi %f
%t0 = fptosi 3.7
```

**`func`** — Begin function definition with signature.

```llvm
func @main() -> i64 {
func @add(i64 %a, i64 %b) -> i64 {
```

**`global`** — Define global constant or variable.

```llvm
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
```

**`idx.chk`** — Check array index bounds; traps if index is out of range [lo, hi) and returns the normalized zero-based index `idx - lo`.

```llvm
%t0 = idx.chk %idx16, %lo16, %hi16
%t1 = idx.chk %idx32, %lo32, %hi32
```

**`il`** — Specify IL version (header directive at top of file).

```llvm
il 0.2.0
```

**`lshr`** — Logical shift right; fills with zeros (unsigned shift).

```llvm
%t11 = lshr %t10, 2
```

**`or`** — Bitwise OR operation on integer values.

```llvm
%t8 = or %t7, 1
%t16 = or %t13, %t15
```

**`resume.label`** — Resume execution at specified label after error; used with ON ERROR GOTO/RESUME.

```llvm
resume.label %tok, ^after(%err)
resume.label %tok, ^target(%err)
```

**`resume.next`** — Resume execution at next statement after error; used with RESUME NEXT.

```llvm
resume.next %tok
resume.next %token
```

**`resume.same`** — Resume execution at same statement that caused error; used with RESUME 0.

```llvm
resume.same %tok
resume.same %tok
```

**`shl`** — Shift left; fills with zeros.

```llvm
%off = shl %i0, 3
%t10 = shl %t9, 1
```

**`sitofp`** — Convert signed integer to floating-point.

```llvm
%fsum = sitofp %sum2
%fn = sitofp %n2
```

**`target`** — Specify target architecture or platform.

```llvm
target wasm32-unknown-unknown
target "wasm32-unknown-unknown"
```

**`trunc1`** — Truncate integer to i1 (single bit boolean).

```llvm
%t37 = trunc1 255
%t1 = trunc1 0
```

**`xor`** — Bitwise exclusive-OR.

```llvm
%t9 = xor %t8, 3
%t6 = xor %t5, 0
```

**`zext1`** — Zero-extend i1 (single bit) to i64 (0 becomes 0, 1 becomes 1).

```llvm
%t36 = zext1 %t18
%t7 = zext1 %t6
```

## Verification rules (selected highlights)

The verifier enforces structural and type rules. Typical checks include:

- Block must end with a terminator (`ret`, `br`, `cbr`, `switch.i32`, `trap`, `trap.from_err`, or `resume.*`).
- Operand types must match instruction requirements (e.g., `add` takes two `i64` operands).
- Block parameters must be passed correctly by all predecessor branches.
- Branch targets must be valid labels in the same function.
- Calls must match callee signature exactly.

## Tools

- `il-verify <file.il>` — static checks with precise diagnostics.
- `il-dis <file.il>` — pretty-printer / disassembler.
- `viper -run <file.il>` — run on the VM (uses runtime bridges).
- `viper il-opt <file.il> --passes "simplify-cfg"` — run transforms (e.g., SimplifyCFG).

> Use `--help` on each tool for full options.
