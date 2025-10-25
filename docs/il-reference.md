---
status: active
audience: public
last-updated: 2025-10-24
---

# Viper IL — Reference

This reference documents the **syntax**, **types**, **instructions**, and **verification rules** of Viper IL.
Where possible, each instruction includes a minimal example taken from real test IL or synthesised to match the parser.

## Types

- **Integers:** `i1`, `i8`, `i16`, `i32`, `i64` (frontends typically use `i64`).
- **Floats:** `f32`, `f64`.
- **Pointers/handles:** `ptr` (opaque), or typed pointers if present in your build.
- **Strings:** `str` (runtime handle), produced/consumed by runtime calls.
- **Void:** `void` for procedures.

> Exact type set is validated by the verifier. Use `il-verify` to confirm.

## Functions & blocks

```
fn @name(<typed args>) -> <type> {
^entry:
  ; instructions
}
```

- Functions have a symbol name `@name` and typed parameters.
- Basic blocks are labeled `^label:` and end in a **terminator** (`ret`, `br`, `switch`).
- Values are in SSA: `%vX` is defined once, then used.

## Instruction catalog (by category)

### Arithmetic

**`addr_of`** — Integer/float addition.

```il
%t41 = addr_of @.Lstr
```

**`fadd`** — Integer/float addition.

```il
%t1 = fadd 1.2345678901234567, 2.5
%t13 = fadd 1, 2.5
```

**`fdiv`** — Division (check signed/unsigned semantics).

```il
%avg = fdiv %fsum, %fn
%t16 = fdiv %t15, 2
```

**`fmul`** — Multiplication.

```il
%t15 = fmul %t14, 4
```

**`fsub`** — Subtraction.

```il
%t14 = fsub %t13, 1.25
%t10 = fsub %t8, %t9
```

**`iadd.ovf`** — Integer/float addition.

```il
%t0 = iadd.ovf 1, 2
%t0 = iadd.ovf 1, 2
```

**`imul.ovf`** — Multiplication.

```il
%t1 = imul.ovf %t0, 3
%t2 = imul.ovf %t0, 3
```

**`isub.ovf`** — Subtraction.

```il
%n2 = isub.ovf %n1, 1
%t1 = isub.ovf %lhs, %rhs
```

**`sdiv.chk0`** — Division (check signed/unsigned semantics).

```il
%t0 = sdiv.chk0 %a, %b
%t3 = sdiv.chk0 %t2, 5
```

**`udiv.chk0`** — Division (check signed/unsigned semantics).

```il
%t2 = udiv.chk0 %a, %b
%t4 = udiv.chk0 10, 2
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

**`scmp_ge`** — Instruction.

```il
%cond = scmp_ge %i, 3
%cond = scmp_ge %i, 10
```

**`scmp_gt`** — Instruction.

```il
%c = scmp_gt %n1, 1
%cond = scmp_gt %yv1, 8
```

**`scmp_le`** — Instruction.

```il
%c = scmp_le %i0, 10
%oc = scmp_le %i0, %n0
```

**`scmp_lt`** — Instruction.

```il
%c = scmp_lt %i0, %n1
%c = scmp_lt %i, 10
```

**`ucmp_ge`** — Instruction.

```il
%t26 = ucmp_ge 3, 3
```

**`ucmp_gt`** — Instruction.

```il
%t25 = ucmp_gt 3, 2
```

**`ucmp_le`** — Instruction.

```il
%t24 = ucmp_le 2, 2
```

**`ucmp_lt`** — Instruction.

```il
%t23 = ucmp_lt 1, 2
```

### Memory

**`alloca`** — Allocate stack memory.

```il
%a_slot = alloca 8
%b_slot = alloca 8
```

**`gep`** — Instruction.

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

**`cbr`** — Instruction.

```il
cbr %eq, label then1, label else0
cbr %c, label loop_body, label done
```

**`ret`** — Return from function.

```il
ret 0
ret 0
```

**`ret_void`** — Instruction.

```il
ret_void
```

**`switch.i32`** — Multi-way branch.

```il
switch.i32 %sel, ^default(%sel), 0 -> ^case0(%sel)
switch.i32 %arg, ^default(%arg) junk, 0 -> ^case0(%arg)
```


### Exceptions

**`eh.entry`** — Exception/Trap; integrates with runtime and verifier.

```il
eh.entry
eh.entry
```

**`eh.pop`** — Exception/Trap; integrates with runtime and verifier.

```il
eh.pop
eh.pop
```

**`eh.push`** — Exception/Trap; integrates with runtime and verifier.

```il
eh.push ^bad
eh.push ^handler
```

**`trap`** — Exception/Trap; integrates with runtime and verifier.

```il
trap
trap
```

**`trap.err`** — Exception/Trap; integrates with runtime and verifier.

```il
%err0 = trap.err 7, %msg0
%err1 = trap.err 9, %msg1
```

**`trap.from_err`** — Exception/Trap; integrates with runtime and verifier.

```il
trap.from_err i32 7
trap.from_err i32 6
```

**`trap.kind`** — Exception/Trap; integrates with runtime and verifier.

```il
%kind = trap.kind
%k0 = trap.kind
```

### Misc

**`and`** — Instruction.

```il
%t7 = and 240, 15
%t24 = and %t21, %t23
```

**`ashr`** — Instruction.

```il
%t12 = ashr -8, 1
```

**`cast.fp_to_si.rte.chk`** — Instruction.

```il
%t0 = cast.fp_to_si.rte.chk %fp
%t35 = cast.fp_to_si.rte.chk 5.5
```

**`cast.fp_to_ui.rte.chk`** — Instruction.

```il
%t1 = cast.fp_to_ui.rte.chk %fp
```

**`cast.si_narrow.chk`** — Instruction.

```il
%t2 = cast.si_narrow.chk %si
```

**`cast.si_to_fp`** — Instruction.

```il
%t4 = cast.si_to_fp %si
```

**`cast.ui_narrow.chk`** — Instruction.

```il
%t3 = cast.ui_narrow.chk %ui
```

**`cast.ui_to_fp`** — Instruction.

```il
%t5 = cast.ui_to_fp %ui
```

**`const_null`** — Instruction.

```il
%t43 = const_null
%p = const_null
```

**`const_str`** — Instruction.

```il
%sA = const_str @.L0
%sB = const_str @.L1
```

**`err.get_kind`** — Instruction.

```il
%k = err.get_kind %e
%k = err.get_kind %err
```

**`err.get_line`** — Instruction.

```il
%ln = err.get_line %e
```

**`extern`** — Instruction.

```il
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
```

**`fptosi`** — Instruction.

```il
%t0 = fptosi %f
%t34 = fptosi 5.5
```

**`func`** — Instruction.

```il
func @main() -> i64 {
func @main() -> i64 {
```

**`global`** — Instruction.

```il
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
```

**`idx.chk`** — Instruction.

```il
%t0 = idx.chk %idx16, %lo16, %hi16
%t1 = idx.chk %idx32, %lo32, %hi32
```

**`il`** — Instruction.

```il
il 0.1
il 0.1 ; // File: examples/il/random_three.il // Purpose: Prints three random numbers using the runtime PRNG. // Links: docs/runtime-abi.md#random
```

**`lshr`** — Instruction.

```il
%t11 = lshr %t10, 2
```

**`or`** — Instruction.

```il
%t8 = or %t7, 1
%t16 = or %t13, %t15
```

**`resume.label`** — Instruction.

```il
resume.label %tok, ^after(%err)
resume.label %tok, ^after(%err)
```

**`resume.next`** — Instruction.

```il
resume.next %tok
resume.next %token
```

**`resume.same`** — Instruction.

```il
resume.same %tok
resume.same %tok
```

**`shl`** — Instruction.

```il
%off = shl %i0, 3
%t10 = shl %t9, 1
```

**`sitofp`** — Instruction.

```il
%fsum = sitofp %sum2
%fn = sitofp %n2
```

**`srem`** — Instruction.

```il
%t0 = srem %a, %b
```

**`srem.chk0`** — Instruction.

```il
%t1 = srem.chk0 %a, %b
%t5 = srem.chk0 7, 3
```

**`target`** — Instruction.

```il
target wasm32-unknown-unknown
target "wasm32-unknown-unknown"
```

**`trunc1`** — Instruction.

```il
%t37 = trunc1 255
%t1 = trunc1 0
```

**`urem.chk0`** — Instruction.

```il
%t3 = urem.chk0 %a, %b
%t6 = urem.chk0 9, 4
```

**`xor`** — Instruction.

```il
%t9 = xor %t8, 3
%t6 = xor %t5, 0
```

**`zext1`** — Instruction.

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
- `ilc run <file.il>` — run on the VM (uses runtime bridges).
- `ilc opt <file.il> -p simplifycfg` — run transforms (e.g., SimplifyCFG).

> Use `--help` on each tool for full options.
