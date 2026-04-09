---
status: active
audience: public
last-verified: 2026-04-09
---

# Viper Arithmetic Semantics Reference

This document specifies the exact arithmetic semantics that Viper guarantees across
all execution layers (VM, AArch64 native, x86-64 native) and both frontends
(Zia, BASIC). These guarantees are tested by the conformance test suite in
`src/tests/conformance/`.

## Integer Arithmetic

All integer arithmetic operates on **I64** (64-bit signed two's complement).
There are no sub-width arithmetic instructions; narrowing is explicit via cast
opcodes.

### Checked (Overflow-Trapping) Arithmetic — the only legal signed forms

| Opcode | Behavior | On Overflow |
|--------|----------|-------------|
| `iadd.ovf` | Signed addition with overflow check | **Trap** |
| `isub.ovf` | Signed subtraction with overflow check | **Trap** |
| `imul.ovf` | Signed multiplication with overflow check | **Trap** |

These support sub-width type annotations (I32, I16). The overflow check uses
the type's range: `iadd.ovf : i32` traps when the result exceeds `INT32_MAX` or
falls below `INT32_MIN`.

> **Plain `add`/`sub`/`mul` are verifier-rejected.** The opcodes still exist
> in `Opcode.def` for legacy lowering, but the IL verifier (see
> `src/il/verify/generated/SpecTables.cpp`) rejects them with messages like
> *"signed integer add must use iadd.ovf (traps on overflow)"*. Frontends
> must emit the `.ovf` forms; there is no public "wrapping arithmetic" path
> at the IL level for signed integers.

Zia uses these checked variants by default (`overflowChecks` is `true`). The
`--no-overflow-checks` flag is reserved for future use; currently the verifier
still requires `.ovf` opcodes regardless of the flag.

### Division

| Opcode | Truncation | Div-by-zero | MIN/-1 |
|--------|-----------|-------------|--------|
| `sdiv.chk0` | Toward zero (C99) | **Trap** | **Trap** |
| `udiv.chk0` | Toward zero | **Trap** | N/A |

The plain `sdiv` / `udiv` opcodes are verifier-rejected with
*"signed/unsigned division must use sdiv.chk0/udiv.chk0"*. Use the `.chk0`
forms exclusively.

**Division truncation direction**: `7 / -2 = -3`, `-7 / 2 = -3`.

### Remainder (Modulo)

| Opcode | Sign rule | Div-by-zero | MIN%-1 |
|--------|-----------|-------------|--------|
| `srem.chk0` | Dividend's sign | **Trap** | `0` (no trap) |
| `urem.chk0` | Unsigned | **Trap** | N/A |

The plain `srem` / `urem` opcodes are verifier-rejected (matches BASIC `MOD`
semantics per the verifier message). Use the `.chk0` forms exclusively.

**Remainder sign rule** (C99): `-7 % 2 = -1`, `7 % -2 = 1`, `-7 % -2 = -1`.

`MIN % -1 = 0` is mathematically correct and does not trap.

### Bitwise Operations

| Opcode | Behavior |
|--------|----------|
| `and`  | Bitwise AND (I64) |
| `or`   | Bitwise OR (I64) |
| `xor`  | Bitwise XOR (I64) |

All operate on full 64-bit values.

## Shift Operations

| Opcode | Direction | Extension | Masking |
|--------|-----------|-----------|---------|
| `shl`  | Left | N/A | `shift & 63` |
| `ashr` | Right | Sign-extending (arithmetic) | `shift & 63` |
| `lshr` | Right | Zero-extending (logical) | `shift & 63` |

**Shift masking**: Shift amounts are masked to `[0, 63]`, matching x86-64
hardware behavior. `shl(1, 64)` produces `1` (shift amount becomes 0).
This is **not** undefined behavior.

**AShr sign extension**: `ashr(-1, 63)` produces `-1` (all ones).
**LShr zero extension**: `lshr(-1, 63)` produces `1`.

## Floating-Point Arithmetic

All floating-point operations use **F64** (IEEE-754 binary64, double precision).

### Basic Operations

| Opcode | Behavior |
|--------|----------|
| `fadd` | IEEE-754 addition |
| `fsub` | IEEE-754 subtraction |
| `fmul` | IEEE-754 multiplication |
| `fdiv` | IEEE-754 division |

**Rounding mode**: Round-to-nearest-even (hardware default). Not configurable.

### Special Values

| Expression | Result |
|-----------|--------|
| `x + NaN` | `NaN` (propagates) |
| `0.0 / 0.0` | `NaN` |
| `1.0 / 0.0` | `+Inf` |
| `-1.0 / 0.0` | `-Inf` |
| `Inf + Inf` | `Inf` |
| `Inf - Inf` | `NaN` |
| `Inf * 0.0` | `NaN` |

### Floating-Point Comparisons

| Opcode | `NaN` behavior | Meaning |
|--------|---------------|---------|
| `fcmp_eq` | `false` | Ordered equal |
| `fcmp_ne` | `true` | Unordered not-equal |
| `fcmp_lt` | `false` | Ordered less-than |
| `fcmp_le` | `false` | Ordered less-or-equal |
| `fcmp_gt` | `false` | Ordered greater-than |
| `fcmp_ge` | `false` | Ordered greater-or-equal |
| `fcmp_ord` | `false` | Both operands are not NaN |
| `fcmp_uno` | `true` | At least one operand is NaN |

All ordered comparisons return `false` when either operand is NaN.
`fcmp_ne` is unordered: returns `true` when either operand is NaN.

## Type Conversions

### Integer-to-Float

| Opcode | Behavior |
|--------|----------|
| `sitofp` | Signed I64 to F64. Exact for values with <= 53 significant bits; rounded otherwise. |
| `cast.si_to_fp` | Same semantics as `sitofp`. |
| `cast.ui_to_fp` | Unsigned I64 to F64. |

### Float-to-Integer

| Opcode | Behavior | NaN | Out-of-range |
|--------|----------|-----|-------------|
| `fptosi` | Truncation toward zero | **Trap** | **Trap** |
| `cast.fp_to_si.rte.chk` | Round-to-even, then convert | **Trap** | **Trap** |
| `cast.fp_to_ui.rte.chk` | Round-to-even, then convert (unsigned) | **Trap** | **Trap** |

In the VM, `fptosi` traps on NaN and overflow (not UB). Native backends must
preserve this behavior.

`fptosi(1.9)` produces `1` (truncation toward zero).
`fptosi(-1.9)` produces `-1` (truncation toward zero).

### Integer Narrowing

| Opcode | Behavior | Out-of-range |
|--------|----------|-------------|
| `cast.si_narrow.chk` | I64 to I32 or I16 (signed) | **Trap** |
| `cast.ui_narrow.chk` | I64 to I32 or I16 (unsigned) | **Trap** |

## Frontend Promotion Rules

### Zia

Type hierarchy: `Byte < Integer < Number`

| Expression | IL Emitted | Notes |
|-----------|-----------|-------|
| `Integer + Integer` | `iadd.ovf` | Trap on signed overflow |
| `Number + Number` | `fadd` | |
| `Integer + Number` | `sitofp` on Integer operand, then `fadd` | Implicit widening |
| `Integer / Integer` | `sdiv.chk0` | Truncation toward zero, trap on `/0` and `MIN/-1` |
| `Integer % Integer` | `srem.chk0` | Dividend sign, trap on `/0` |
| `Byte + Integer` | `iadd.ovf` (both I64 at IL level) | Byte is I32 in sema, but I64 in IL |

### BASIC

Type hierarchy: `INTEGER% (I16) < LONG& (I64) < SINGLE! (F64) < DOUBLE# (F64)`

| Expression | IL Emitted | Notes |
|-----------|-----------|-------|
| `A% + B%` | `iadd.ovf` | I64 result |
| `A + B` (float) | `fadd` | |
| `A \ B` (integer div) | `sdiv.chk0` | Always checked, always I64 |
| `A MOD B` | `srem.chk0` | Always checked, always I64 |
| `A / B` (float div) | `fdiv` | Both promoted to F64 |
| `TRUE` | `-1` (I64) | BASIC uses `-1` for true |

Both frontends emit the checked variants because the IL verifier rejects the
plain (non-`.ovf` / non-`.chk0`) signed integer opcodes. The BASIC frontend
exposes an internal `OverflowPolicy::Wrap` switch (in `EmitCommon.cpp:135`),
but every current call site passes `OverflowPolicy::Checked`, so the wrapping
path is dead code at lowering time and would fail verification if enabled.
