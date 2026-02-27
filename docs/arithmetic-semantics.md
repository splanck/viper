# Viper Arithmetic Semantics Reference

This document specifies the exact arithmetic semantics that Viper guarantees across
all execution layers (VM, AArch64 native, x86-64 native) and both frontends
(Zia, BASIC). These guarantees are tested by the conformance test suite in
`src/tests/conformance/`.

## Integer Arithmetic

All integer arithmetic operates on **I64** (64-bit signed two's complement).
There are no sub-width arithmetic instructions; narrowing is explicit via cast
opcodes.

### Wrapping Arithmetic

| Opcode | Behavior | On Overflow |
|--------|----------|-------------|
| `add`  | Two's complement addition | Silent wrap |
| `sub`  | Two's complement subtraction | Silent wrap |
| `mul`  | Two's complement multiplication | Silent wrap |

These never trap. `INT64_MAX + 1` silently wraps to `INT64_MIN`.

### Checked (Overflow-Trapping) Arithmetic

| Opcode | Behavior | On Overflow |
|--------|----------|-------------|
| `iadd.ovf` | Signed addition with overflow check | **Trap** |
| `isub.ovf` | Signed subtraction with overflow check | **Trap** |
| `imul.ovf` | Signed multiplication with overflow check | **Trap** |

These support sub-width type annotations (I32, I16). The overflow check uses
the type's range: `iadd.ovf : i32` traps when the result exceeds `INT32_MAX` or
falls below `INT32_MIN`.

Zia uses checked variants when `overflowChecks` is enabled in compiler options.

### Division

| Opcode | Truncation | Div-by-zero | MIN/-1 |
|--------|-----------|-------------|--------|
| `sdiv` | Toward zero (C99) | **Trap** | **Trap** |
| `sdiv.chk0` | Toward zero (C99) | **Trap** | **Trap** |
| `udiv` | Toward zero | **Trap** | N/A |
| `udiv.chk0` | Toward zero | **Trap** | N/A |

Both `sdiv` and `sdiv.chk0` trap on divide-by-zero and `MIN / -1`. The `.chk0`
suffix is a naming convention inherited from early design; behavior is identical
in the current VM.

**Division truncation direction**: `7 / -2 = -3`, `-7 / 2 = -3`.

### Remainder (Modulo)

| Opcode | Sign rule | Div-by-zero | MIN%-1 |
|--------|-----------|-------------|--------|
| `srem` | Dividend's sign | **Trap** | `0` (no trap) |
| `srem.chk0` | Dividend's sign | **Trap** | `0` (no trap) |
| `urem` | Unsigned | **Trap** | N/A |
| `urem.chk0` | Unsigned | **Trap** | N/A |

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
| `fcmp.eq` | `false` | Ordered equal |
| `fcmp.ne` | `true` | Unordered not-equal |
| `fcmp.lt` | `false` | Ordered less-than |
| `fcmp.le` | `false` | Ordered less-or-equal |
| `fcmp.gt` | `false` | Ordered greater-than |
| `fcmp.ge` | `false` | Ordered greater-or-equal |
| `fcmp.ord` | `false` | Both operands are not NaN |
| `fcmp.uno` | `true` | At least one operand is NaN |

All ordered comparisons return `false` when either operand is NaN.
`fcmp.ne` is unordered: returns `true` when either operand is NaN.

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
| `Integer + Integer` | `add` (or `iadd.ovf` if overflow checks) | |
| `Number + Number` | `fadd` | |
| `Integer + Number` | `sitofp` on Integer operand, then `fadd` | Implicit widening |
| `Integer / Integer` | `sdiv` (or `sdiv.chk0` if overflow checks) | Truncation toward zero |
| `Integer % Integer` | `srem` (or `srem.chk0` if overflow checks) | Dividend sign |
| `Byte + Integer` | `add` (both I64 at IL level) | Byte is I32 in sema, but I64 in IL |

### BASIC

Type hierarchy: `INTEGER% (I16) < LONG& (I64) < SINGLE! (F64) < DOUBLE# (F64)`

| Expression | IL Emitted | Notes |
|-----------|-----------|-------|
| `A% + B%` | `add` | I64 result |
| `A + B` (float) | `fadd` | |
| `A \ B` (integer div) | `sdiv.chk0` | Always checked, always I64 |
| `A MOD B` | `srem.chk0` | Always checked, always I64 |
| `A / B` (float div) | `fdiv` | Both promoted to F64 |
| `TRUE` | `-1` (I64) | BASIC uses `-1` for true |

**Key difference**: BASIC integer division and modulo always use checked variants
(`sdiv.chk0`, `srem.chk0`), while Zia defaults to unchecked (`sdiv`, `srem`)
unless `overflowChecks` is enabled.
