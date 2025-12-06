# Viper BASIC Frontend Bugs

This document tracks known bugs in the Viper BASIC frontend (parser, lowerer, type system).

---

## BUG-BASIC-001: Array dimensions must be literal numbers

**Status**: OPEN
**Severity**: Medium (workaround available)
**Component**: Parser

### Description
Array declarations do not support constant expressions or variables for array dimensions. Only literal integer numbers are accepted.

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

### Affected Files
- `src/frontends/basic/Parser.cpp` - array dimension parsing

### Fix Required
Modify the parser to accept:
1. Constant expressions (evaluate at parse time)
2. Variable references (for dynamic arrays, would require runtime allocation)

At minimum, `Const` values should be substituted at parse time since they are compile-time constants.

---

## BUG-BASIC-002: Viper.Random.Next() MOD operator type mismatch

**Status**: OPEN
**Severity**: Medium (workaround available)
**Component**: Type System / Runtime

### Description
`Viper.Random.Next()` returns `f64` (Double), but using it with the `MOD` operator against an integer literal causes a type mismatch error. The MOD operator doesn't support floating-point operands.

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

### Root Cause
- `Viper.Random.Next()` is defined in `RuntimeClasses.inc` with signature `f64()` (returns 0.0 to 1.0)
- The MOD operator requires integer operands
- There's no implicit conversion from f64 to integer for MOD

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

### Affected Files
- `src/il/runtime/classes/RuntimeClasses.inc:167-168` - Viper.Random class definition

### Fix Options
1. Add `Viper.Random.NextInt(max As Integer)` that returns Integer in [0, max)
2. Add implicit `Int()` conversion when using f64 with MOD
3. Document the `Int(Viper.Random.Next() * max)` pattern

---
