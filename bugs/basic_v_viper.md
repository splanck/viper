# BASIC vs ViperLang Feature Comparison

A comprehensive comparison of Viper BASIC and ViperLang language capabilities based on systematic testing.

## Summary

| Category | BASIC | ViperLang | Notes |
|----------|-------|-----------|-------|
| Primitives | ✅ Full | ✅ Full | VL has hex/binary literals, BASIC has type suffixes |
| Variables | ✅ Full | ✅ Full | BASIC has STATIC, VL has type inference |
| Operators | ✅ Full | ✅ Full | VL has null coalescing (??) |
| Control Flow | ✅ Full | ⚠️ Partial | VL match statement broken (BUG-VL-012) |
| Functions | ✅ Full | ✅ Full | VL has lambdas, BASIC has BYREF |
| Arrays | ✅ Full | ❌ None | VL uses List instead |
| Collections | ⚠️ Via runtime | ✅ Built-in | VL has List, Map, Set built-in |
| OOP Classes | ✅ Full | ✅ Full | Both work well |
| Inheritance | ✅ Full | ⚠️ Partial | VL lacks virtual dispatch (BUG-VL-011) |
| Interfaces | ✅ Full | ⚠️ Partial | VL interface method calls broken (BUG-VL-010) |
| Generics | ❌ None | ❌ Broken | VL has syntax but not implemented (BUG-VL-009) |
| Error Handling | ✅ Full | ⚠️ Partial | BASIC has TRY/CATCH, VL has guard/optionals |
| I/O | ✅ Full | ✅ Full | Both work well |
| String Functions | ✅ Full | ✅ Full | Both have full set |
| Math Functions | ✅ Full | ✅ Full | Both have full set |
| Modules | ✅ Full | ✅ Full | Both work well |

---

## Detailed Comparison

### 1. Primitive Types & Literals

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Integer | `DIM x AS INTEGER` / `x%` | `var x: Integer` |
| Long | `DIM x AS LONG` / `x&` | (use Integer - 64-bit) |
| Float (single) | `DIM x AS SINGLE` / `x!` | `var x: Number` |
| Float (double) | `DIM x AS DOUBLE` / `x#` | `var x: Number` |
| String | `DIM x AS STRING` / `x$` | `var x: String` |
| Boolean | `DIM x AS BOOLEAN` | `var x: Boolean` |
| Byte | ❌ | `var x: Byte` ✅ (fixed) |
| Hex literals | ❌ | `0xFF` ✅ |
| Binary literals | ❌ | `0b1010` ✅ |
| Type suffixes | `%`, `#`, `$`, `!`, `&` ✅ | ❌ |
| Type inference | ❌ | `var x = 42` ✅ |

**Test Results:** Both pass all primitive tests.

---

### 2. Variables & Constants

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Mutable variable | `DIM x AS ...` | `var x = ...` |
| Constant | `CONST PI = 3.14` | `final pi = 3.14` |
| Static variable | `STATIC x` ✅ | ❌ (use entity fields) |
| Multiple declaration | `DIM a, b, c AS INTEGER` | `var a = 1; var b = 2;` |
| Reassignment | `x = newValue` | `x = newValue` |

**Test Results:** Both pass. BASIC has STATIC variables, ViperLang doesn't.

---

### 3. Operators

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Arithmetic | `+ - * / \ MOD ^` | `+ - * / %` |
| Integer division | `\` ✅ | ❌ (/ for integers) |
| Power/Exponent | `^` ✅ | `Viper.Math.Pow()` |
| Comparison | `= <> < > <= >=` | `== != < > <= >=` |
| Logical | `AND OR NOT` | `&& \|\| !` |
| Bitwise AND | ❌ | `&` ✅ (fixed) |
| Bitwise OR | ❌ | `\|` ✅ (fixed) |
| Bitwise XOR | ❌ | `^` ✅ (fixed) |
| String concat | `+` | `+` |
| Null coalescing | ❌ | `??` ✅ |
| Optional chain | ❌ | `?.` ✅ |

**Test Results:** Both pass. ViperLang bitwise operators now work.

---

### 4. Control Flow

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| If/Then/Else | `IF...THEN...ELSE...END IF` ✅ | `if (...) {...} else {...}` ✅ |
| ElseIf | `ELSEIF` ✅ | `else if` ✅ |
| Select Case | `SELECT CASE...END SELECT` ✅ | `match` ❌ (BUG-VL-012) |
| For loop | `FOR...TO...STEP...NEXT` ✅ | `for (;;) {...}` ✅ |
| For-Each | `FOR EACH...IN...NEXT` ✅ | `for (x in list) {...}` ✅ |
| While | `WHILE...WEND` ✅ | `while (...) {...}` ✅ |
| Do While | `DO WHILE...LOOP` ✅ | ❌ (use while) |
| Do Until | `DO UNTIL...LOOP` ✅ | ❌ |
| Do...Loop While | `DO...LOOP WHILE` ✅ | ❌ |
| Exit/Break | `EXIT FOR`, `EXIT DO` ✅ | `break` ✅ |
| Continue | ❌ | `continue` ✅ |
| Guard | ❌ | `guard (...) else {...}` ✅ |

**Test Results:** BASIC has more loop variants. ViperLang match statement is broken.

---

### 5. Functions

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Function (returns value) | `FUNCTION...END FUNCTION` ✅ | `func name() -> Type` ✅ |
| Procedure (no return) | `SUB...END SUB` ✅ | `func name()` ✅ |
| Parameters | `(x AS INTEGER)` | `(x: Integer)` |
| ByVal (default) | `BYVAL x` ✅ | (default) |
| ByRef | `BYREF x` ✅ | ❌ |
| Return statement | `RETURN value` or `name = value` | `return value` |
| Recursion | ✅ | ✅ |
| Lambdas | ❌ | `(x) => x + 1` ✅ (fixed) |
| Closures | ❌ | ✅ |

**Test Results:** Both pass. BASIC has BYREF, ViperLang has lambdas.

---

### 6. Arrays & Collections

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Fixed array | `DIM arr(10)` ✅ | ❌ |
| 2D array | `DIM arr(10, 10)` ✅ | ❌ |
| Dynamic array | `REDIM` ✅ | ❌ |
| Array bounds | `LBOUND()`, `UBOUND()` ✅ | ❌ |
| List | Via `Viper.Collections` | `List[T]` ✅ built-in |
| Map | Via `Viper.Collections` | `Map[K,V]` ✅ built-in |
| Set | Via `Viper.Collections` | `Set[T]` ✅ built-in |

**Test Results:** BASIC has native arrays. ViperLang uses List/Map/Set instead.

---

### 7. OOP - Classes/Entities

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Class definition | `CLASS...END CLASS` ✅ | `entity Name {...}` ✅ |
| Value type | `TYPE...END TYPE` ✅ | `value Name {...}` ✅ |
| Fields | `PUBLIC/PRIVATE field AS Type` | `expose/hide field: Type` |
| Methods | `PUBLIC/PRIVATE SUB/FUNCTION` | `expose/hide func` |
| Constructor | `PUBLIC SUB NEW()` ✅ | `expose func init()` ✅ |
| Destructor | `PUBLIC SUB DESTROY()` ✅ | ❌ (GC) |
| Self reference | `ME` | `self` |
| Object creation | `NEW ClassName()` | `new Entity()` |
| Object deletion | `DELETE obj` ✅ | ❌ (automatic GC) |

**Test Results:** Both pass basic OOP tests.

---

### 8. OOP - Inheritance

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Single inheritance | `CLASS Child : Parent` ✅ | `entity Child extends Parent` ✅ |
| Access parent fields | ✅ | ✅ (fixed - BUG-VL-006) |
| Override methods | `OVERRIDE` ✅ | `override` ✅ (fixed - BUG-VL-005) |
| Polymorphic assignment | `DIM a AS Animal: SET a = dog` ✅ | `var a: Animal = dog` ✅ (fixed - BUG-VL-007) |
| Virtual dispatch | ✅ | ❌ (BUG-VL-011) |
| Super call | Implicit | `super(...)` |

**Test Results:** BASIC has full virtual dispatch. ViperLang polymorphic assignment works but virtual dispatch doesn't.

---

### 9. OOP - Interfaces

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Interface definition | `INTERFACE...END INTERFACE` ✅ | `interface Name {...}` ✅ |
| Implementation | `IMPLEMENTS IName` ✅ | `implements IName` ✅ |
| Multiple interfaces | ✅ | ✅ |
| Interface variable | `DIM s AS IShape` ✅ | `var s: IShape` ✅ |
| Call via interface | ✅ | ❌ (BUG-VL-010) |

**Test Results:** BASIC interfaces work fully. ViperLang interface method calls broken.

---

### 10. Generics

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Generic entity | ❌ | `entity Box[T]` ❌ (BUG-VL-009) |
| Generic function | ❌ | `func foo[T]()` ❌ |
| Type constraints | ❌ | ❌ |

**Test Results:** Neither has working generics. ViperLang has syntax but it's not implemented.

---

### 11. Error Handling

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Try/Catch | `TRY...CATCH...END TRY` ✅ | ❌ |
| Finally | `FINALLY` ✅ | ❌ |
| On Error | `ON ERROR GOTO` ✅ | ❌ |
| Resume | `RESUME NEXT` ✅ | ❌ |
| Optional types | ❌ | `T?` ✅ |
| Null check | ❌ | `value ?? default` ✅ |
| Guard statement | ❌ | `guard (cond) else {...}` ✅ |

**Test Results:** Different approaches - BASIC uses exceptions, ViperLang uses optionals/guard.

---

### 12. I/O

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Print with newline | `PRINT` ✅ | `Viper.Terminal.Say()` ✅ |
| Print without newline | `PRINT x;` ✅ | `Viper.Terminal.Print()` ✅ |
| Read line | `INPUT` ✅ | `Viper.Terminal.ReadLine()` ✅ |
| Read key | `INKEY$` ✅ | `Viper.Terminal.ReadKey()` ✅ |

**Test Results:** Both pass I/O tests.

---

### 13. String Functions

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Length | `LEN()` | `Viper.String.Length()` |
| Substring | `MID$()` | `Viper.String.Substring()` |
| Left/Right | `LEFT$()`, `RIGHT$()` | `Viper.String.Left/Right()` |
| Find | `INSTR()` | `Viper.String.IndexOf()` |
| Upper/Lower | `UCASE$()`, `LCASE$()` | `Viper.String.ToUpper/Lower()` |
| Trim | `TRIM$()`, `LTRIM$()`, `RTRIM$()` | `Viper.String.Trim()` |
| Char/Code | `CHR$()`, `ASC()` | `Viper.String.Chr/Asc()` |
| Convert | `VAL()`, `STR$()` | `Viper.Fmt.Int()` etc. |

**Test Results:** Both pass string function tests.

---

### 14. Math Functions

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Absolute | `ABS()` | `Viper.Math.Abs()` |
| Square root | `SQR()` | `Viper.Math.Sqrt()` |
| Power | `POW()` / `^` | `Viper.Math.Pow()` |
| Trig | `SIN()`, `COS()`, `TAN()` | `Viper.Math.Sin/Cos/Tan()` |
| Floor/Ceiling | `INT()`, `FIX()` | `Viper.Math.Floor/Ceil()` |
| Sign | `SGN()` ✅ | ❌ |
| Log/Exp | `LOG()`, `EXP()` ✅ | ❌ |
| Min/Max | ❌ | `Viper.Math.Min/Max()` ✅ |
| Random | `RND()`, `RANDOMIZE` | `Viper.Math.Random()` |

**Test Results:** Both pass math tests. Some functions differ.

---

### 15. Modules/Namespaces

| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Namespace | `NAMESPACE...END NAMESPACE` ✅ | `module Name;` ✅ |
| Qualified access | `Namespace.Function()` ✅ | `Module.Function()` ✅ |
| Import | `USING namespace` ✅ | `import path` ✅ |

**Test Results:** Both pass module tests.

---

## ViperLang Gaps (Features BASIC Has That ViperLang Lacks)

### Critical (Blocking real-world use)
1. **Virtual method dispatch** (BUG-VL-011) - Polymorphism incomplete
2. **Interface method calls** (BUG-VL-010) - Interface polymorphism broken
3. **Match statement** (BUG-VL-012) - Pattern matching unusable
4. **Generics** (BUG-VL-009) - No generic programming

### Important
5. **Native arrays** - Must use List instead of fixed arrays
6. **Try/Catch error handling** - Only guard/optionals available
7. **ByRef parameters** - Cannot pass by reference
8. **Static variables** - Must use entity fields

### Nice to Have
9. **DO WHILE/UNTIL loops** - Use while instead
10. **Destructor support** - GC handles cleanup
11. **Integer division operator** - Use regular division
12. **Power operator** - Use Viper.Math.Pow()
13. **SGN, LOG, EXP functions** - Could add to Viper.Math

---

## Open Bugs Summary

| Bug | Description | Severity |
|-----|-------------|----------|
| BUG-VL-009 | Generics not implemented | High |
| BUG-VL-010 | Interface method calls broken | High |
| BUG-VL-011 | No virtual method dispatch | High |
| BUG-VL-012 | Match statement runtime trap | High |

## Fixed Bugs (This Session)

| Bug | Description | Status |
|-----|-------------|--------|
| BUG-VL-001 | Byte literals | ✅ Fixed |
| BUG-VL-002 | Bitwise operators | ✅ Fixed |
| BUG-VL-003 | String concat crash | ✅ Fixed |
| BUG-VL-004 | Lambda allocation | ✅ Fixed |
| BUG-VL-005 | Override keyword | ✅ Fixed |
| BUG-VL-006 | Inherited fields | ✅ Fixed |
| BUG-VL-007 | Polymorphic assignment | ✅ Fixed |
| BUG-VL-008 | Entity field ordering | ✅ Fixed |

---

## Recommendations

### Priority 1: Complete OOP (Required for production use)
1. Implement vtables for virtual method dispatch
2. Fix interface method calls with vtable lookup
3. Consider using same vtable mechanism for both

### Priority 2: Control Flow
1. Fix match statement codegen
2. Consider adding DO WHILE/UNTIL as sugar

### Priority 3: Generics
1. Register type parameters in scope
2. Implement type substitution during instantiation
3. Generate monomorphized code

### Priority 4: Error Handling
1. Consider adding try/catch syntax
2. Or enhance Result type pattern

---

## Test Files

All test programs are located in:
- `/tests/comparison/basic/` - BASIC test programs
- `/tests/comparison/viper/` - ViperLang test programs

Each test file covers one category (01_primitives, 02_variables, etc.)
