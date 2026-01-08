# BASIC vs ViperLang Feature Comparison

## Executive Summary

| Category | BASIC | ViperLang | Winner |
|----------|-------|-----------|--------|
| Primitives | ✅ 5/5 | ⚠️ 4/5 | BASIC |
| Variables | ✅ 3/3 | ✅ 2/2 | BASIC (has STATIC) |
| Operators | ✅ Full | ⚠️ Partial | BASIC |
| Control Flow | ✅ Full | ✅ Mostly | BASIC (has DO..LOOP) |
| Functions | ✅ Full | ⚠️ Partial | BASIC (VL lambdas broken) |
| Collections | ✅ Arrays | ✅ List | Tie |
| OOP Classes | ✅ Full | ✅ Works | Tie |
| OOP Inheritance | ✅ Works | ❌ Broken | BASIC |
| OOP Interfaces | ⚠️ Bug | ⚠️ Bug | Tie (both buggy) |
| OOP Generics | N/A | ❌ Broken | N/A |
| Error Handling | ✅ TRY/CATCH | ⚠️ guard only | BASIC |
| I/O | ✅ Works | ✅ Works | Tie |
| String Functions | ✅ Full | ✅ Full | Tie |
| Math Functions | ✅ Full | ✅ Full | Tie |
| Modules | ⚠️ Bug | ✅ Works | ViperLang |

**Overall: BASIC is more complete and stable. ViperLang has critical OOP bugs.**

Legend: ✅ Works | ⚠️ Partial/Bugs | ❌ Missing/Broken

---

## Detailed Test Results

### 1. Primitives (01_primitives)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Integer | ✅ `DIM x AS INTEGER` | ✅ `var x: Integer = 42` |
| Float | ✅ `DIM x AS DOUBLE` | ✅ `var x: Number = 3.14` |
| String | ✅ `DIM x AS STRING` | ✅ `var x: String = "hi"` |
| Boolean | ✅ `TRUE/FALSE` | ✅ `true/false` |
| Byte | N/A | ❌ **BUG-VL-001** - can't assign integer literal |

### 2. Variables (02_variables)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Mutable | ✅ `DIM x` | ✅ `var x = ...` |
| Constants | ✅ `CONST X = 10` | ✅ `final x = ...` |
| Static | ✅ `STATIC x` | ❌ Not supported |
| Type inference | ✅ Suffix-based | ✅ Full inference |

### 3. Operators (03_operators)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Arithmetic | ✅ `+ - * / MOD` | ✅ `+ - * / %` |
| Power | ✅ `^` | ❌ `**` not working |
| Comparison | ✅ `= <> < > <= >=` | ✅ `== != < > <= >=` |
| Logical | ✅ `AND OR NOT` | ✅ `&& \|\| !` |
| Bitwise | ✅ Context-based | ❌ **BUG-VL-002** - not parsed |
| String concat | ✅ `+` or `;` | ✅ `+` |

### 4. Control Flow (04_control_flow)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| If/Else | ✅ `IF...THEN...ELSE...END IF` | ✅ `if (cond) {...}` |
| Select/Match | ✅ `SELECT CASE` | ✅ `match value {...}` |
| While | ✅ `WHILE...WEND` | ✅ `while (cond) {...}` |
| Do Loop | ✅ `DO...LOOP WHILE/UNTIL` | ❌ Not supported |
| For | ✅ `FOR...TO...NEXT` | ✅ `for (;;) {...}` |
| For-each | ✅ `FOR EACH...IN` | ✅ `for (x in coll)` |
| Break | ✅ `EXIT FOR/DO` | ✅ `break` |
| Continue | ❌ Not supported | ✅ `continue` |
| Guard | ❌ Not supported | ✅ `guard (cond) else {...}` |

**Note**: ViperLang has **BUG-VL-003** - string concat with `Viper.Fmt.Int()` in loops crashes

### 5. Functions (05_functions)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Functions | ✅ `FUNCTION...END FUNCTION` | ✅ `func name() -> Type` |
| Procedures | ✅ `SUB...END SUB` | ✅ `func name()` |
| Return | ✅ `name = value` or `RETURN` | ✅ `return value` |
| Parameters | ✅ ByVal/ByRef | ✅ By value |
| Default params | ❌ Not supported | ✅ Supported |
| Named args | ❌ Not supported | ✅ `func(x: 1)` |
| Lambdas | ❌ Not supported | ❌ **BUG-VL-004** - runtime errors |

### 6. Collections (06_collections)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Fixed arrays | ✅ `DIM arr(10)` | ❌ N/A |
| Multi-dim | ✅ `DIM arr(10, 10)` | ❌ N/A |
| Dynamic arrays | ✅ `REDIM` | ❌ N/A |
| Lists | ✅ `Viper.Collections.List` | ✅ `List[T]` |
| Iteration | ✅ `FOR EACH` | ✅ `for (x in list)` |

### 7. OOP - Classes/Entities (07_oop_classes)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Definition | ✅ `CLASS...END CLASS` | ✅ `entity name {...}` |
| Constructor | ✅ `PUBLIC SUB NEW()` | ✅ `expose func init()` |
| Destructor | ✅ `PUBLIC SUB DESTROY()` | ❌ N/A (GC) |
| Fields | ✅ `PUBLIC/PRIVATE field AS Type` | ✅ `expose/hide Type field;` |
| Methods | ✅ `PUBLIC/PRIVATE FUNCTION` | ✅ `expose/hide func` |
| Self reference | ✅ `ME` | ✅ implicit |
| Object creation | ✅ `NEW ClassName()` | ✅ `new Entity()` |

### 8. OOP - Inheritance (08_oop_inheritance)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Syntax | ✅ `CLASS Child : Parent` | ⚠️ `entity Child extends Parent` |
| Inherited fields | ✅ Works | ❌ **BUG-VL-006** - not accessible |
| Method override | ✅ `OVERRIDE FUNCTION` | ⚠️ Implicit only (**BUG-VL-005**) |
| Polymorphism | ✅ Works | ❌ **BUG-VL-007** - type mismatch |
| Field ordering | ✅ Works | ❌ **BUG-VL-008** - values swapped |

### 9. OOP - Interfaces (09_oop_interfaces)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Definition | ✅ `INTERFACE...END INTERFACE` | ✅ `interface name {...}` |
| Implementation | ⚠️ `CLASS C IMPLEMENTS I` | ✅ `entity E implements I` |
| Constructor with params | ❌ **BUG-BAS-002** | ✅ Works |
| Interface variable | ✅ Works | ✅ Assignment works |
| Interface method call | ✅ Works | ❌ **BUG-VL-010** - wrong return type |

### 10. OOP - Generics (10_oop_generics)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Generic entities | N/A | ❌ **BUG-VL-009** - not implemented |
| Generic functions | N/A | ❌ **BUG-VL-009** - not implemented |
| Type parameters | N/A | ❌ "Unknown type: T" |

### 11. Error Handling (11_error_handling)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| TRY/CATCH | ✅ `TRY...CATCH...FINALLY` | ❌ Not supported |
| ON ERROR | ✅ `ON ERROR GOTO` | ❌ Not supported |
| Guard | ❌ Not supported | ✅ `guard (cond) else {...}` |
| Optional types | ❌ Not supported | ⚠️ Not tested |

### 12. I/O Console (12_io_console)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Print line | ✅ `PRINT` | ✅ `Viper.Terminal.Say()` |
| Print inline | ✅ `PRINT ...;` | ✅ `Viper.Terminal.Print()` |
| Numbers | ✅ Direct | ✅ `SayInt()`, `SayNum()` |
| Formatting | ✅ `STR$()` | ✅ `Viper.Fmt.*` |

### 13. String Functions (13_string_functions)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Length | ✅ `LEN()` | ✅ `Viper.String.Length()` |
| Substring | ✅ `MID$()` | ✅ `Viper.String.Substring()` |
| Left/Right | ✅ `LEFT$()`, `RIGHT$()` | ✅ Available |
| Case | ✅ `UCASE$()`, `LCASE$()` | ✅ `ToUpper()`, `ToLower()` |
| Trim | ✅ `TRIM$()`, `LTRIM$()`, `RTRIM$()` | ✅ `Trim()` |
| Find | ✅ `INSTR()` | ✅ `IndexOf()` |
| Char/Asc | ✅ `CHR$()`, `ASC()` | ✅ `Chr()`, `Asc()` |

### 14. Math Functions (14_math_functions)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Abs | ✅ `ABS()` | ✅ `Viper.Math.Abs()` |
| Sqrt | ✅ `SQR()` | ✅ `Viper.Math.Sqrt()` |
| Power | ✅ `^` operator | ✅ `Viper.Math.Pow()` |
| Trig | ✅ `SIN()`, `COS()`, `TAN()` | ✅ `Sin()`, `Cos()`, `Tan()` |
| Floor/Ceil | ✅ `INT()`, `FIX()` | ✅ `Floor()`, `Ceil()` |
| Min/Max | ❌ Not built-in | ✅ `Min()`, `Max()` |
| Sign | ✅ `SGN()` | ⚠️ Not tested |
| Log/Exp | ✅ `LOG()`, `EXP()` | ⚠️ Not tested |

### 15. Modules & Namespaces (15_modules)
| Feature | BASIC | ViperLang |
|---------|-------|-----------|
| Declaration | ✅ `NAMESPACE...END NAMESPACE` | ✅ `module name;` |
| Function calls | ❌ **BUG-BAS-001** - unknown callee | ✅ Works |
| Runtime modules | N/A | ✅ `Viper.*` modules |

---

## Critical ViperLang Gaps

### Must Fix (Blocking OOP)
1. **BUG-VL-006**: Inherited fields not accessible in child entities
2. **BUG-VL-007**: Polymorphism broken - can't assign child to parent type
3. **BUG-VL-009**: Generics completely non-functional
4. **BUG-VL-010**: Interface method calls return wrong type

### Should Fix (Functional Issues)
5. **BUG-VL-002**: Bitwise operators not parsed (&, |, ^, ~)
6. **BUG-VL-004**: Lambdas cause runtime errors
7. **BUG-VL-003**: String concat in loops crashes (exit 134)
8. **BUG-VL-008**: Entity field ordering bug - values swapped
9. **BUG-VL-001**: Byte type won't accept integer literals

### Nice to Have
10. **BUG-VL-005**: Add `override` keyword support
11. Add STATIC variable support
12. Add DO..LOOP construct
13. Add proper error handling (try/catch or Result types)

---

## Critical BASIC Bugs

1. **BUG-BAS-001**: Namespace function calls fail at codegen
2. **BUG-BAS-002**: Interface with constructor parameters causes codegen error

---

## Recommendations for ViperLang

### Priority 1: Fix OOP Foundation
- Fix inheritance field visibility (semantic analysis)
- Fix polymorphism type checking (allow child→parent assignment)
- Fix entity field ordering

### Priority 2: Complete Generics
- Implement generic type parameter resolution
- Enable generic entity instantiation
- Enable generic function calls

### Priority 3: Fix Interface Polymorphism
- Fix method return type handling through interface variables

### Priority 4: Operators & Types
- Add bitwise operator parsing
- Fix Byte type integer literal assignment
- Fix power operator

### Priority 5: Higher-Order Functions
- Fix lambda invocation
- Test closures

---

## Test Files

All test files are located at:
- `tests/comparison/basic/*.bas` (15 tests)
- `tests/comparison/viper/*.viper` (15 tests)

Run with:
```bash
./build/src/tools/ilc/ilc front basic -run tests/comparison/basic/XX_name.bas
./build/src/tools/ilc/ilc front viperlang -run tests/comparison/viper/XX_name.viper
```
