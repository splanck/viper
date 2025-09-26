---
status: draft
audience: internal
last-verified: 2025-09-24
---

# Numeric Semantics

This document is the single source of truth for numeric behaviour across the BASIC
front end, IL constant-folder, and the runtime/VM. All language layers must agree
with these rules.

## Primitive Types and Ranges

| BASIC type | Internal representation | Range |
|------------|------------------------|-------|
| `INTEGER`  | signed 16-bit (`i16`)   | −32,768 … 32,767 |
| `LONG`     | signed 32-bit (`i32`)   | −2,147,483,648 … 2,147,483,647 |
| `SINGLE`   | IEEE‑754 binary32 (`f32`) | finite `float` values |
| `DOUBLE`   | IEEE‑754 binary64 (`f64`) | finite `double` values |

Type promotions follow `INTEGER < LONG < SINGLE < DOUBLE`.  An expression adopts
the widest rank among its operands before evaluation; literal suffixes may force a
specific rank.  Integer literals without a suffix start at `INTEGER` and are
promoted as needed.  Floating literals default to `DOUBLE` unless suffixed with `!`
(`SINGLE`).

## Operator Result Types

* `+`, `-`, `*` follow the standard promotion lattice: widen operands to the wider
  of the two ranks before applying the operation.
* `/` always performs floating-point division.  If any operand is `DOUBLE` or both
  operands are integral, the result is `DOUBLE`.  Otherwise the result is `SINGLE`.
* `\` performs integer division on `INTEGER` or `LONG` inputs.  The result adopts
  the promoted integer rank (`INTEGER` stays `INTEGER`, otherwise `LONG`).
* `MOD` matches the rank and sign rules of its inputs; the result keeps the sign of
  the dividend (left operand).
* `^` (power) is computed in `f64`.  The result is `DOUBLE`.  A `DomainError` trap
  occurs when the base is negative and the exponent is non-integral.  If the result
  magnitude overflows to ±∞ or becomes NaN, an `Overflow` trap is raised.

### Explicit MOD Examples

* `-3 MOD 2 = -1`
* `3 MOD -2 = 1`

## Division and Remainder Traps

* `/` never traps; it follows IEEE rules (NaN/Inf propagate).
* `\` truncates the quotient toward zero.  It traps with `DivideByZero` when the
  divisor is zero and with `Overflow` when the mathematically exact quotient would
  fall outside the target integer range (for example, `LONG` `-2_147_483_648 \ -1`).
* `MOD` is defined as `r = a − trunc(a / b) * b` with the sign of `a`.  It traps
  with `DivideByZero` when `b = 0`.

## Rounding and Conversion Functions

All conversions that round from floating point use round-to-nearest, ties-to-even
("banker’s rounding").  Unless otherwise stated, traps are reported as `Overflow`.

| Function | Description |
|----------|-------------|
| `INT(x)` | Floor: greatest integral value ≤ `x`.  Works on any numeric rank. |
| `FIX(x)` | Truncate toward zero.  Works on any numeric rank. |
| `ROUND(x)` | Round-to-nearest-even.  Result rank follows argument rank. |
| `CINT(x)` | Round-to-nearest-even then convert to `INTEGER` (`i16`).  Trap if the rounded value is outside −32,768…32,767.  Example: `CINT(2.5) = 2`, `CINT(3.5) = 4`. |
| `CLNG(x)` | Round-to-nearest-even then convert to `LONG` (`i32`).  Trap if the rounded value is outside −2,147,483,648…2,147,483,647. |
| `CSNG(x)` | Convert to `SINGLE` (`f32`).  Trap if the rounded `f32` result would be non-finite (overflow to ±∞). |
| `CDBL(x)` | Convert to `DOUBLE` (`f64`).  Always succeeds for finite inputs. |

Additional examples:

* `INT(-1.5) = -2`
* `FIX(-1.5) = -1`

## Power Operator Edge Cases

`^` is evaluated via `pow` in `f64`.  Inputs are widened to `DOUBLE` before the
operation.  The evaluator must detect:

* **Negative base with fractional exponent** → `DomainError`.
* **Non-finite results** → `Overflow` trap.
* **Subnormal inputs** are permitted; the result is rounded to `DOUBLE` normally.

## VAL Function

`VAL` parses ASCII/UTF‑8 according to the grammar below (whitespace `ws` is
`[ \t\r\n]*`).

```
VALInput ::= ws Number? (Trailing*)
Number   ::= Sign? Digits ('.' Digits?)? Exponent?
Sign     ::= '+' | '-'
Digits   ::= [0-9]+
Exponent ::= ('E' | 'e') Sign? Digits
Trailing ::= any non-numeric character (parsing stops before it)
```

Rules:

* Leading and trailing whitespace is ignored.
* Parsing stops at the first non-numeric character after the number; the rest of
  the string is ignored.
* If the first non-whitespace character is not part of a number, the result is 0.
* The resulting value is produced as `DOUBLE`.  Overflow to a non-finite value
  traps with `Overflow`.

## STR$ Formatting

`STR$` prints numeric values using invariant decimal formatting:

* Integers use the minimal decimal representation with a leading `-` for negatives
  (no extra spaces or `+`).
* `SINGLE` values round-trip using `printf("%.9g")` semantics.
* `DOUBLE` values round-trip using `printf("%.17g")` semantics.

These guarantees ensure `VAL(STR$(x))` yields `x` for finite numbers.

## Checked IL Operations

The IL exposes checked variants that must be used to enforce BASIC semantics.
Each trap type propagates to the VM/runtime as a terminating error with the
indicated condition.

| IL op | Description | Trap |
|-------|-------------|------|
| `add.ovf` | Signed integer addition with overflow detection. | `Overflow` |
| `sub.ovf` | Signed integer subtraction with overflow detection. | `Overflow` |
| `mul.ovf` | Signed integer multiplication with overflow detection. | `Overflow` |
| `sdiv.chk0` | Signed integer division. | `DivideByZero` on zero divisor; `Overflow` on `MIN / -1`. |
| `srem.chk0` | Signed remainder. | `DivideByZero` on zero divisor. |
| `cast.{src}->{dst}.chk` | Narrowing or sign-changing cast. | `Overflow` when the result is out of range. |

Front ends may use the unchecked versions (`add`, `sub`, …) only when the result
is statically proven to be in range.

## BASIC ↔ IL Lowering Table

| BASIC construct | Operand ranks | IL lowering |
|-----------------|---------------|-------------|
| `a + b`, `a - b`, `a * b` (both integer) | promote to `LONG` as needed | `add.ovf`, `sub.ovf`, `mul.ovf` on promoted integer width; apply `cast.*.chk` to narrow back to `INTEGER` when required. |
| `a + b`, `a - b`, `a * b` (any floating) | promote to `SINGLE`/`DOUBLE` | `fadd`, `fsub`, `fmul` after widening to `f64`; emit `cast.double_to_single.chk` when targeting `SINGLE`. |
| `a / b` | integer or float | `fdiv` in `f64`, followed by optional `cast.double_to_single.chk` when the result rank is `SINGLE`. |
| `a \ b` | integer ranks | `sdiv.chk0` on promoted integer width; narrow with `cast.*.chk` as needed. |
| `a MOD b` | integer ranks | `srem.chk0` on promoted integer width; narrow with `cast.*.chk` as needed. |
| `a ^ b` | any numeric | Call runtime helper `@rt_pow_f64_chkdom(a', b')` where inputs are widened to `f64`; helper enforces `DomainError`/`Overflow`. |
| `INT(x)` | any numeric | For integers: no-op. For floats: runtime call `@rt_int(x')` returning the original rank. |
| `FIX(x)` | any numeric | Runtime call `@rt_fix(x')` implementing truncate-toward-zero. |
| `ROUND(x)` | any numeric | Runtime call `@rt_round_ties_even(x')`. |
| `CINT(x)` | any numeric | Runtime call `@rt_cint_chk(x')` returning `INTEGER` or trapping. |
| `CLNG(x)` | any numeric | Runtime call `@rt_clng_chk(x')` returning `LONG` or trapping. |
| `CSNG(x)` | any numeric | Runtime call `@rt_csng_chk(x')` returning `SINGLE`. |
| `CDBL(x)` | any numeric | Runtime call `@rt_cdbl(x')` returning `DOUBLE`. |
| `VAL(s$)` | string | Runtime call `@rt_val(s$)` returning `DOUBLE` or trapping on overflow. |
| `STR$(x)` | any numeric | Runtime call `@rt_str$(x)` producing a `str`. |

Helper names are illustrative; the ABI must ensure traps propagate as described
and that the round-to-nearest-even rule is observed for every conversion.

