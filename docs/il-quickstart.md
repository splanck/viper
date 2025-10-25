---
status: active
audience: public
last-updated: 2025-10-24
---

# Viper IL — Quickstart

Viper IL is a **typed, readable intermediate language** used by the Viper toolchain. Frontends (e.g., BASIC) lower into IL;
the **VM** executes IL directly; the **verifier** checks safety rules; transforms (e.g., **SimplifyCFG**, **Liveness**) operate on IL;
and backends lower IL toward native code.

This guide shows how to write a tiny IL function, verify it, and run it.

## 1) A tiny function

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_concat(str, str) -> str
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
global const str @.L2 = " "
global const str @.L3 = "JOHN DOE"
global const str @.L4 = "\n"
func @main() -> i64 {
entry:
  %a_slot = alloca 8
```

### Anatomy
- `fn @name(args) -> type { ... }` defines a function.
- Basic blocks are labeled with `^label:`; the first block is the entry.
- Values are in **SSA form** (each `%v` is assigned once).
- Types are explicit (e.g., `.i64`). Use `ret` to return from a function.

## 2) Control flow

Conditional branches jump to labels; `switch` handles multi-way branching.

```il
^entry:
  %c0 = icmp.eq.i64 %a, %b
  br %c0, ^equal, ^notequal

^equal:
  ret %a

^notequal:
  %s0 = add.i64 %a, %b
  ret %s0
```

## 3) Calling functions

```il
fn @square(i64 %x) -> i64 {
^entry:
  %s0 = mul.i64 %x, %x
  ret %s0
}

fn @call_square(i64 %y) -> i64 {
^entry:
  %s1 = call @square(%y)
  ret %s1
}
```

## 4) Switch example

```il
func @switch_missing_brargs(%sel:i32, %x:i32) -> void {
entry(%sel:i32, %x:i32):
  switch.i32 %sel, ^default(%sel), 0 -> ^case0(%sel)
default(%d_sel:i32):
  ret
case0(%c_sel:i32):
  ret
}
```

## 5) Verify and run

- **Verify**: `il-verify program.il` checks types, control-flow, and instruction well-formedness.
- **Disassemble/pretty-print**: `il-dis program.il` (or `ilc dis program.il` if integrated).
- **Run**: `ilc run program.il` executes on the VM.

> Tip: Use small functions and the verifier to debug IL before layering transforms.
