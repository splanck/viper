# Viper BASIC Frontend Bugs

This document tracks known bugs in the Viper BASIC frontend (parser, lowerer, type system).

---

## BUG-BASIC-001: Array dimensions must be literal numbers

**Status**: RESOLVED (class field array dimensions now use `parseExpression()` -- see BUG-056 fix in Parser_Stmt_OOP.cpp)
**Severity**: Medium (workaround available)
**Component**: Parser

### Description

Array declarations do not support constant expressions or variables for array dimensions. Only literal integer numbers
are accepted.

### Reproduction

```basic
Const MAX_SIZE = 100
Dim arr(MAX_SIZE) As Integer  ' ERROR: expected label or number
```

```basic
Dim size As Integer
size = 100
Dim arr(size) As Integer  ' ERROR: expected label or number
```

### Error Message

```
error[B0001]: expected label or number
    Dim arr(MAX_SIZE) As Integer
            ^
```

### Expected Behavior

Both `Const` expressions and variables should be usable as array dimensions, like in traditional BASIC:

```basic
Const MAX_SIZE = 100
Dim arr(MAX_SIZE) As Integer  ' Should work

Dim size As Integer
size = 100
Dim arr(size) As Integer  ' Should work (dynamic array)
```

### Workaround

Use literal numbers for array sizes:

```basic
' Instead of: Dim arr(MAX_SIZE) As Integer
Dim arr(100) As Integer  ' Works
```

### Root Cause Analysis

The bug affects **class field array declarations only**. Module-level `DIM` statements already support expressions (they
call `parseExpression()`).

In `src/frontends/basic/Parser_Stmt_OOP.cpp:235`, the class field parser explicitly requires literal numbers:

```cpp
Token sizeTok = expect(TokenKind::Number);  // Line 235
```

This is different from the module-level DIM parser in `Parser_Stmt_Runtime.cpp:190` which correctly uses:

```cpp
node->dimensions.push_back(parseExpression());
```

### Affected Files

- `src/frontends/basic/Parser_Stmt_OOP.cpp:235` - class field array dimension parsing (THE BUG)
- `src/frontends/basic/Parser_Stmt_Runtime.cpp:190` - module-level DIM (works correctly)

### Fix Required

Change `Parser_Stmt_OOP.cpp:235` from:

```cpp
Token sizeTok = expect(TokenKind::Number);
```

To use `parseExpression()` like the module-level DIM parser, then constant-fold the result at parse time for class field
arrays (which require compile-time known sizes).

---

## BUG-BASIC-002: Viper.Random.Next() MOD operator type mismatch

**Status**: OPEN
**Severity**: Medium (workaround available)
**Component**: Type System / Runtime

### Description

`Viper.Random.Next()` returns `f64` (Double), but using it with the `MOD` operator against an integer literal causes a
type mismatch error. The MOD operator doesn't support floating-point operands.

### Reproduction

```basic
Dim x As Integer
x = Viper.Random.Next() Mod 100  ' ERROR: operand type mismatch
```

### Error Message

```
error[B2001]: operand type mismatch
    x = Viper.Random.Next() Mod 100
                            ^
```

### Root Cause Analysis

The MOD operator in the semantic analyzer explicitly requires integer operands.

In `src/frontends/basic/sem/Check_Expr_Binary.cpp:318`, the MOD operator uses `validateIntegerOperands`:

```cpp
{BinaryExpr::Op::Mod, &validateIntegerOperands, &integerResult, "B2001"},
```

The `validateIntegerOperands` function (lines 209-219) rejects floating-point:

```cpp
void validateIntegerOperands(...) {
    if (!isIntegerType(lhs) || !isIntegerType(rhs))
        sem::emitOperandTypeMismatch(context.diagnostics(), expr, diagId);
    ...
}
```

Since `Viper.Random.Next()` returns `f64` (Type::Float), it fails the `isIntegerType(lhs)` check.

Note: The binary expression analyzer (lines 414-433) has logic to implicitly convert operands for Add/Sub/Mul
operations, but this was not extended to MOD/IDiv.

### Expected Behavior

Either:

1. Add a `Viper.Random.NextInt(max)` function that returns an integer in range [0, max)
2. Or document that users should use `Int(Viper.Random.Next() * max)`

### Workaround

Use the traditional BASIC `Rnd()` function which returns a float 0-1:

```basic
Dim x As Integer
x = Int(Rnd() * 100)  ' Works - returns 0-99
```

Or use `RandInt()` which is already available:

```basic
Dim x As Integer
x = RandInt(100)  ' Returns 0-99
```

### Affected Files

- `src/frontends/basic/sem/Check_Expr_Binary.cpp:318` - MOD validation rule
- `src/frontends/basic/sem/Check_Expr_Binary.cpp:209-219` - `validateIntegerOperands` function
- `src/il/runtime/classes/RuntimeClasses.inc:167-168` - Viper.Random class definition

### Fix Options

1. **Preferred**: Add `Viper.Random.NextInt(max As Integer)` that returns Integer in [0, max)
2. Add implicit `Int()` conversion in `analyzeBinaryExpr` when Float operands are used with MOD/IDiv (add similar
   promotion logic to lines 414-433 for integer operators)
3. Document that `RandInt(max)` already exists as an alternative

---
