# Viper BASIC OOP Implementation - Bug Report

**Date:** 2025-12-05
**Tested with:** vbasic compiler (VM execution)

## Executive Summary

The OOP implementation has significant gaps. While basic class definition, instantiation, and simple methods work, *
*inheritance is fundamentally broken** - derived classes cannot access inherited fields or methods. Additionally, *
*INTERFACE/IMPLEMENTS is not implemented** (parser doesn't recognize the keywords), and **IS/AS operators are not
implemented**.

---

## Feature Status Overview

| Feature                       | Status              | Notes                                                               |
|-------------------------------|---------------------|---------------------------------------------------------------------|
| Class definition              | **WORKS**           | `CLASS ... END CLASS`                                               |
| Field declaration             | **WORKS**           | `PUBLIC field AS Type`                                              |
| Methods (Sub/Function)        | **WORKS**           | Instance methods work correctly                                     |
| Constructors (SUB NEW)        | **WORKS**           | With parameters                                                     |
| Object instantiation          | **WORKS**           | `NEW ClassName()`                                                   |
| Field access                  | **WORKS**           | `obj.field`                                                         |
| Method calls                  | **WORKS**           | `obj.Method()`                                                      |
| ME keyword                    | **WORKS**           | Self-reference in methods                                           |
| Object composition            | **WORKS**           | Object fields in classes                                            |
| Arrays of objects             | **WORKS**           | `DIM arr(n) AS ClassName`                                           |
| Object as parameter           | **WORKS**           | Pass by reference                                                   |
| Object as return value        | **PARTIAL**         | Single function works; multiple object-returning functions may fail |
| Inheritance (CLASS B : A)     | **BROKEN**          | Derived class can't access inherited members                        |
| VIRTUAL methods               | **PARTIAL**         | Works only with explicit OVERRIDE                                   |
| OVERRIDE                      | **WORKS**           | When used with VIRTUAL                                              |
| BASE.Method()                 | **WORKS**           | Call parent implementation                                          |
| VIRTUAL ABSTRACT              | **WORKS**           | Abstract method declaration                                         |
| FINAL                         | **WORKS**           | Prevents override                                                   |
| INTERFACE/IMPLEMENTS          | **NOT IMPLEMENTED** | Parser error: "unknown statement 'INTERFACE'"                       |
| IS operator                   | **NOT IMPLEMENTED** | Parser error: "expected THEN, got IS"                               |
| AS operator (cast)            | **NOT IMPLEMENTED** | Parser error: "unknown statement 'AS'"                              |
| PROPERTY (GET/SET)            | **PARTIAL**         | Parser accepts syntax but may not work at runtime                   |
| PRIVATE (access modifier)     | **NOT IMPLEMENTED** | Parser error in class body                                          |
| Type coercion in constructors | **BROKEN**          | Integer literal not auto-converted to DOUBLE                        |

---

## Critical Bugs

### BUG-OOP-001: Inherited Fields Not Accessible on Derived Class

**Severity:** CRITICAL
**Impact:** Makes inheritance unusable for any practical purpose

**Description:**
When a class inherits from another class, the derived class cannot access fields defined in the parent class.

**Test Case:**

```basic
CLASS Parent
    PUBLIC value AS INTEGER
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
c.value = 100  ' ERROR: no such property 'VALUE' on 'CHILD'
```

**Error:**

```
error[E_PROP_NO_SUCH_PROPERTY]: no such property 'VALUE' on 'CHILD'
```

**Workaround:** None. This breaks basic inheritance.

---

### BUG-OOP-002: Inherited Methods Not Accessible on Derived Class

**Severity:** CRITICAL
**Impact:** Makes inheritance unusable

**Description:**
Non-virtual methods defined in a parent class cannot be called on derived class instances.

**Test Case:**

```basic
CLASS Parent
    PUBLIC SUB Greet()
        PRINT "Hello"
    END SUB
END CLASS

CLASS Child : Parent
END CLASS

DIM c AS Child
c = NEW Child()
c.Greet()  ' ERROR: no matching overload for 'GREET()'
```

**Error:**

```
error[E_OVERLOAD_NO_MATCH]: no matching overload for 'GREET()'
```

**Workaround:** Use VIRTUAL on all methods you want to inherit, and OVERRIDE in every subclass.

---

### BUG-OOP-003: Virtual Methods Not Inherited Without Override

**Severity:** HIGH
**Impact:** Requires boilerplate OVERRIDE in every subclass

**Description:**
Even VIRTUAL methods are not accessible on derived class instances unless explicitly overridden.

**Test Case:**

```basic
CLASS Parent
    VIRTUAL SUB Speak()
        PRINT "Parent speaks"
    END SUB
END CLASS

CLASS Child : Parent
    ' No override - should inherit parent's implementation
END CLASS

DIM c AS Child
c = NEW Child()
c.Speak()  ' ERROR: no matching overload for 'SPEAK()'
```

**Workaround:** Add `OVERRIDE SUB Speak() : BASE.Speak() : END SUB` in every subclass.

**Note:** This works correctly when using a parent-type variable:

```basic
DIM p AS Parent
p = NEW Child()
p.Speak()  ' WORKS - prints "Parent speaks"
```

---

### BUG-OOP-004: INTERFACE/IMPLEMENTS Not Implemented

**Severity:** HIGH
**Impact:** No interface-based polymorphism

**Description:**
The INTERFACE keyword is not recognized by the parser.

**Test Case:**

```basic
INTERFACE IGreeter
    SUB Greet()
END INTERFACE
```

**Error:**

```
error[B0001]: unknown statement 'INTERFACE'; expected keyword or procedure call
```

**Note:** Golden test files exist in `src/tests/golden/basic_oop_iface/` but these tests are NOT run - the interface
feature was never completed.

---

### BUG-OOP-005: IS/AS Operators Not Implemented

**Severity:** HIGH
**Impact:** No runtime type checking or casting

**Description:**
The IS and AS keywords for type testing and casting are not recognized.

**Test Case:**

```basic
DIM a AS Parent
a = NEW Child()
IF a IS Child THEN  ' ERROR
    PRINT "is child"
END IF
```

**Error:**

```
error[B0001]: expected THEN, got IS
```

---

### BUG-OOP-006: Multiple Object-Returning Functions Cause IL Type Error [FIXED]

**Status:** FIXED

**Root Cause:** The semantic analyzer's `analyzeReturn` method had an early return when
`activeFunctionExplicitRet_ == BasicType::Unknown` (which is the case for object-returning
functions). This skipped the `visitExpr(*stmt.value)` call that resolves variable names
in the return expression. Without this resolution, local variables like `mid` in `RETURN mid`
were not renamed to their mangled form (`MID_1`), causing the lowerer to look up the wrong
symbol and emit incorrect IL types.

**Fix:** Moved `visitExpr(*stmt.value)` before the early return check in
`SemanticAnalyzer_Stmts_Control.cpp:analyzeReturn()`. Also simplified the `lowerReturn`
workaround code since proper variable resolution now ensures the correct symbol is found.
Additionally, allowed implicit float-to-int argument conversions in call expressions
to support BASIC's traditional type coercion rules.

**Test Case (now works):**

```basic
CLASS Point
    PUBLIC x AS INTEGER
    PUBLIC y AS INTEGER
END CLASS

FUNCTION CreatePoint(px AS INTEGER, py AS INTEGER) AS Point
    DIM p AS Point
    p = NEW Point()
    p.x = px
    p.y = py
    RETURN p
END FUNCTION

FUNCTION MidPoint(p1 AS Point, p2 AS Point) AS Point
    DIM mid AS Point
    mid = NEW Point()
    mid.x = (p1.x + p2.x) / 2
    mid.y = (p1.y + p2.y) / 2
    RETURN mid
END FUNCTION

DIM a AS Point
a = CreatePoint(0, 0)
DIM b AS Point
b = CreatePoint(10, 20)
DIM m AS Point
m = MidPoint(a, b)
PRINT m.x  ' Outputs: 5
PRINT m.y  ' Outputs: 10
```

---

### BUG-OOP-007: No Implicit Type Coercion in Constructors

**Severity:** LOW
**Impact:** Must use explicit type literals

**Description:**
Integer literals are not automatically converted to DOUBLE when passed to constructor parameters expecting DOUBLE.

**Test Case:**

```basic
CLASS Account
    SUB NEW(initial AS DOUBLE)
        ' ...
    END SUB
END CLASS

DIM acc AS Account
acc = NEW Account(1000)  ' ERROR: expects f64 but got i64
```

**Workaround:** Use explicit double literal: `NEW Account(1000.0)`

---

### BUG-OOP-008: PRIVATE Keyword Not Recognized in Class Body

**Severity:** LOW
**Impact:** Cannot create private fields

**Description:**
The PRIVATE access modifier is not recognized in class field declarations.

**Test Case:**

```basic
CLASS Person
    PRIVATE name AS STRING  ' ERROR
END CLASS
```

**Error:**

```
error[B0001]: expected END, got ?
```

---

### BUG-OOP-009: "Base" is a Reserved Keyword

**Severity:** TRIVIAL
**Impact:** Cannot use "Base" as class name

**Description:**
"Base" is reserved for the BASE.Method() syntax and cannot be used as a class name.

**Test Case:**

```basic
CLASS Base  ' ERROR
END CLASS
```

**Error:**

```
error[B0001]: expected ident, got BASE
```

---

## What Works Correctly

1. **Basic class definition and instantiation**
2. **Fields of all primitive types** (INTEGER, STRING, DOUBLE, BOOLEAN)
3. **Instance methods** (SUB and FUNCTION)
4. **Constructors with parameters** (SUB NEW)
5. **ME keyword** for self-reference
6. **Object composition** (object fields within classes)
7. **Arrays of objects**
8. **Object as method parameter** (pass by reference)
9. **Single object-returning function** per file
10. **VIRTUAL + OVERRIDE** for polymorphism (when properly used)
11. **BASE.Method()** calls to parent implementation
12. **VIRTUAL ABSTRACT** methods
13. **FINAL** modifier
14. **Multi-level inheritance** (A -> B -> C) for virtual methods

---

## Recommendations

### Priority 1 (Critical - blocks basic OOP usage)

1. Fix inherited field/method visibility (BUG-OOP-001, BUG-OOP-002, BUG-OOP-003)

### Priority 2 (High - missing core features)

2. Implement INTERFACE/IMPLEMENTS (BUG-OOP-004)
3. Implement IS/AS operators (BUG-OOP-005)

### Priority 3 (Medium)

4. Fix multiple object-returning functions (BUG-OOP-006)
5. Implement PRIVATE access modifier (BUG-OOP-008)
6. Add implicit type coercion for constructors (BUG-OOP-007)

---

## Test Files Location

All test files created during this investigation are in `/tmp/`:

- `oop_test_01_basic_class.bas` - Basic class (PASS)
- `oop_test_02_fields.bas` - Field types (PASS)
- `oop_test_03_methods.bas` - Methods (PASS)
- `oop_test_04_constructor.bas` - Constructors (PASS)
- `oop_inherit_01_field_int.bas` - Field inheritance (FAIL)
- `oop_inherit_02_method.bas` - Method inheritance (FAIL)
- `oop_inherit_03_virtual.bas` - Virtual with override (PASS)
- `oop_inherit_04_virtual_no_override.bas` - Virtual without override (FAIL)
- `oop_inherit_05_virtual_via_base.bas` - Virtual via base type (PASS)
- `oop_inherit_06_base_call.bas` - BASE.Method() (PASS)
- `oop_interface_01_basic.bas` - Interface (FAIL - not implemented)
- `oop_abstract_02.bas` - VIRTUAL ABSTRACT (PASS)
- `oop_is_as_01.bas` - IS/AS operators (FAIL - not implemented)
- `oop_property_02.bas` - Properties (FAIL)
- `oop_array_01.bas` - Arrays of objects (PASS)
- `oop_param_01.bas` - Object as parameter (PASS)
- `oop_return_01.bas` - Object return (FAIL with multiple functions)
- `oop_return_02.bas` - Simple object return (PASS)
- `oop_composition_01.bas` - Object composition (PASS)
- `oop_multilevel_01.bas` - Multi-level inheritance (PASS)
- `oop_final_01.bas` - FINAL modifier (PASS)
- `oop_realistic_02.bas` - Bank account (PASS)
- `oop_realistic_03.bas` - Derived class usage (FAIL - inheritance)

---

## Root Cause Analysis (Deep Dive)

### BUG-OOP-001 — Inherited fields inaccessible on derived instances

- Cause: Field lookup and object layout omit base-class fields.
    - Lowering searches only `cinfo->fields` (declaring class). See `Lower_OOP_MemberAccess.cpp` (resolveMemberField) —
      no base walk, no use of `OopIndex::findFieldInHierarchy`.
    - Layout (`Lower_OOP_Scan.cpp`) is built from `decl.fields` only; base fields are not prefixed into derived layouts.
- Fix:
    - Merge base layout into derived at scan time (vptr at offset 0, then base fields, then derived fields).
    - Use `findFieldInHierarchy` (or merged layout) for member access; keep PRIVATE access checks.
- Challenges: ABI shift for field offsets; ensure base is resolved before derived.

### BUG-OOP-002 — Inherited methods not accessible on derived instances

- Cause: Overload resolution and slot lookup don’t consult bases.
    - `sem::resolveMethodOverload` inspects only the current class’s `methods` map (no base walk), leading to
      `E_OVERLOAD_NO_MATCH` for `Child.Greet()`.
    - `getVirtualSlot` returns `-1` for derived receivers when only the base declares the method.
- Fix:
    - Extend resolution to walk `baseQualified` chain (or use `OopIndex::findMethodInHierarchy`) with access filters;
      prefer most-derived match.
    - Have `getVirtualSlot` fall back to the first matching base slot so dynamic dispatch can be emitted for derived
      receivers.
- Challenges: Respect PRIVATE across class boundaries; avoid ambiguous picks when both base/derived supply property
  accessors.

### BUG-OOP-003 — Virtual methods not inherited without override

- Cause: Same root as BUG-OOP-002. For a `Child` receiver, slot discovery fails when the method is declared only in
  `Parent`, so indirect dispatch isn’t used.
- Fix: Same as BUG-OOP-002; preserve BASE-qualified semantics for direct base calls.

### BUG-OOP-004 — INTERFACE/IMPLEMENTS not implemented

- Cause: Parser has no handler for `INTERFACE` and does not parse `IMPLEMENTS` on `CLASS` headers.
    - Lexing + AST + OOP index exist; parsing is missing (`registerOopParsers` does not register INTERFACE; no
      `parseInterfaceDecl()`).
- Fix:
    - Implement `parseInterfaceDecl()` and register it; add `IMPLEMENTS` clause parsing in `Parser_Stmt_OOP.cpp` to
      populate `ClassDecl::implementsQualifiedNames`.
    - Lowering/runtime hooks already exist (`Lower_OOP_Emit.cpp`, `rt_type_registry.c`).
- Challenges: Grammar placement (e.g., `CLASS B : A IMPLEMENTS I1, I2`), diagnostics clarity.

### BUG-OOP-005 — IS/AS operators not implemented

- Cause: `IsExpr`/`AsExpr` exist in AST/semantics/lowering, but `Parser_Expr.cpp` never constructs them; `IS` appears
  only in `SELECT CASE` relations.
- Fix: Teach the expression parser to parse `value IS <QualifiedType>` and `value AS <QualifiedType>`, constructing
  `IsExpr`/`AsExpr`. Name resolution is already handled in the semantic analyzer.
- Challenges: None significant; ensure no collision with statement-level `AS` in declarations.

### BUG-OOP-006 — Multiple object-returning functions: IL ret type mismatch

- Symptom: `ret value type mismatch: expected ptr but got i64` on the second object-returning FUNCTION.
- Likely Cause: Some return paths for ptr-typed FUNCTIONs fall through to `emitRet(v.value)` where `v` is `i64`. This
  can happen when symbol typing for the returned local object is stale or when the specialized `VarExpr` path in
  `lowerReturn` doesn’t trigger.
- Fix:
    - Harden `lowerReturn`: when enclosing FUNCTION returns `ptr`, ensure `RETURN <var>` always loads the slot as `ptr`
      for object variables; reject or diagnose non-`ptr` expressions.
    - Verify symbol typing is deterministically set for `DIM name AS Class` (both in `VarCollectWalker::before(DimStmt)`
      and `Scan_RuntimeNeeds::before(DimStmt)`), and per-proc state is reset before each function.
- Challenges: Avoid reinterpreting integers as pointers; preserve string/object lifetimes on return.

### BUG-OOP-007 — No implicit coercion for constructor arguments

- Cause: `lowerNewExpr` passes arguments through unchanged; ctor parameter types are ignored.
- Fix: Look up ctor signature in `OopIndex` and coerce args (`i64`→`f64`, boolean normalization) just like method calls.
- Challenges: Constructor overloads; reuse existing overload rules if multiple ctors are supported.

### BUG-OOP-008 — PRIVATE not recognized in class body

- Status now: Supported. Parser consumes single-use `PUBLIC`/`PRIVATE` and applies to the next field/member; shorthand
  field forms are accepted.
- Earlier failure was likely pre-fix behavior; retain better error recovery for malformed members.

### BUG-OOP-009 — "Base" reserved as class name

- Cause: `BASE` is a keyword for base-qualified calls.
- Decision: Keep reserved; optionally improve diagnostic suggesting a different class name.

---

## Proposed Fix Plan (Summary)

- Fields/inheritance: Merge base fields into derived layouts; use hierarchical lookup; enforce PRIVATE.
- Methods/inheritance: Walk bases in overload resolution and slot lookup; prefer most-derived; keep BASE semantics.
- Interfaces: Implement `parseInterfaceDecl` and `IMPLEMENTS` parsing; reuse existing lowering/runtime hooks.
- IS/AS: Add expression parsing; semantics/lowering already in place.
- Object-return: Make returns for ptr-typed FUNCTIONs robust; add failing test with two object-returners.
- Constructors: Coerce NEW-args to ctor param types.

## Risks & Challenges

- ABI shifts (field layout) require recompiling; acceptable but must be documented.
- Parser additions must not regress statement disambiguation; add golden tests.
- Base-walk in overload resolution should be cached to avoid performance regressions.
