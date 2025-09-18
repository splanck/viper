<!--
SPDX-License-Identifier: MIT
File: archive/docs/references/basic.md
Purpose: BASIC language reference and examples.
-->

# BASIC Language Reference

## Comments

Single-line comments begin with an apostrophe `'` or the keyword `REM`.
`REM` is case-insensitive and must appear at the start of a line or after
whitespace. Both forms consume characters until the end of the line.

```basic
' Using apostrophe
REM Using REM
10 PRINT "HI" ' trailing comment
20 REM trailing REM comment
```

## Program Structure

A BASIC source file may begin with zero or more `FUNCTION` or `SUB`
declarations followed by top-level statements that form the main program. The
procedure section and the main section are both optional, and procedures may be
invoked before or after their textual definitions.

## FUNCTION

Defines a procedure that returns a value. The return type is determined by the
name suffix: `#` → `f64`, `$` → `str`, no suffix → `i64`. Parameters are passed
by value unless an argument is an array, in which case a bare array variable
must be supplied and is passed by reference. See the lowering tables for
[procedure definitions](lowering.md#procedure-definitions) and
[procedure calls](lowering.md#procedure-calls).

## SUB

Declares a procedure with no return value. Parameter rules match those of
`FUNCTION`, including ByRef array parameters. Lowering details are listed under
[procedure definitions](lowering.md#procedure-definitions) and
[procedure calls](lowering.md#procedure-calls).

## BOOLEAN

`BOOLEAN` values carry logical truth. The literals `TRUE` and `FALSE` (case
insensitive) produce the canonical `BOOLEAN` constants and may be used anywhere
an expression is expected.

Boolean operators follow the precedence table below (higher rows bind more
tightly):

| Precedence | Operators         | Notes |
|------------|-------------------|-------|
| 1          | `NOT`              | Unary logical negation. |
| 2          | `ANDALSO`, `AND`   | `ANDALSO` short-circuits; `AND` evaluates both operands. |
| 3          | `ORELSE`, `OR`     | `ORELSE` short-circuits; `OR` evaluates both operands. |

`ANDALSO` evaluates its right-hand operand only when the left-hand operand is
`TRUE`. `ORELSE` evaluates its right-hand operand only when the left-hand
operand is `FALSE`. The non-short-circuiting forms (`AND`, `OR`) always evaluate
both operands.

Conditions in `IF`, `WHILE`, and `UNTIL` statements must be `BOOLEAN`
expressions. Numeric values do not implicitly convert to `BOOLEAN`; use
comparisons (for example `X <> 0`) to derive explicit conditions.

## RETURN

Transfers control out of the current `FUNCTION` or `SUB`. In a `FUNCTION`,
assign the result to the function name before `RETURN`. This lowers directly to
an IL `ret`; see [Return statements](lowering.md#return-statements).

## Array Parameters ByRef

Array parameters in `FUNCTION` and `SUB` declarations are passed by reference. The caller must supply an array variable declared with `DIM`; expressions or indexed elements are rejected.

```basic
10 SUB S(X())
20 END SUB
30 DIM A(10), B(10)
40 LET Z = S(B)       ' OK
50 LET Z = S(A(1))    ' error: argument 1 to S must be an array variable (ByRef)
60 LET Z = S(A + 0)   ' error: argument 1 to S must be an array variable (ByRef)
```

Only bare array names may be passed; expressions create temporaries and are rejected.
