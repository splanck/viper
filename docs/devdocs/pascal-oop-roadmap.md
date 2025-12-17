# Pascal OOP Implementation Roadmap

**Date:** December 2025
**Status:** Active Development
**Goal:** Implement Viper Pascal OOP per specification (docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md)

---

## Executive Summary

Pascal OOP infrastructure is **remarkably complete** at the parser, semantic analysis, and lowering levels (~4,500 lines
of dedicated OOP code). However, several practical bugs prevent end-to-end functionality. This roadmap prioritizes bug
fixes to unlock the existing infrastructure.

**Important:** The Viper Pascal spec deliberately omits some BASIC OOP features:

- No `final` modifier (not in spec)
- No `is`/`as` RTTI operators (not in spec)
- No `Delete`/`Dispose` (automatic memory management)
- No static members (not in spec)
- No `abstract` keyword documented (though in reserved words)

These omissions are **intentional design decisions**, not gaps to fill.

---

## Current Implementation Status

### Parser Level: COMPLETE ✅

| Feature                             | Status     | Location               |
|-------------------------------------|------------|------------------------|
| `class` declarations                | ✅ Complete | Parser_OOP.cpp:23-136  |
| `interface` declarations            | ✅ Complete | Parser_OOP.cpp:138-261 |
| Heritage clause (base + interfaces) | ✅ Complete | Parser_OOP.cpp:53-70   |
| Method signatures                   | ✅ Complete | Parser_OOP.cpp:450-540 |
| Constructor declarations            | ✅ Complete | Parser_OOP.cpp:542-603 |
| Destructor declarations             | ✅ Complete | Parser_OOP.cpp:605-654 |
| Property declarations               | ✅ Complete | Parser_OOP.cpp:300-340 |
| Visibility (public/private)         | ✅ Complete | Parser_OOP.cpp:278-298 |
| Virtual/Override/Abstract           | ✅ Complete | Parser_OOP.cpp:507-530 |
| Weak fields                         | ✅ Complete | Parser_OOP.cpp:420-430 |
| Inherited statement                 | ✅ Complete | AST.hpp:771-784        |

### AST Nodes: COMPLETE ✅

| Node Type         | Fields                                  | Location          |
|-------------------|-----------------------------------------|-------------------|
| `ClassDecl`       | name, baseClass, interfaces, members    | AST.hpp:1101-1112 |
| `InterfaceDecl`   | name, baseInterfaces, methods           | AST.hpp:1114-1125 |
| `ClassMember`     | visibility, kind, field/method/property | AST.hpp:1038-1065 |
| `PropertyDecl`    | name, type, getter, setter              | AST.hpp:1023-1035 |
| `ConstructorDecl` | name, params, body, isForward           | AST.hpp:1068-1081 |
| `DestructorDecl`  | name, body, isVirtual, isOverride       | AST.hpp:1084-1098 |
| `MethodSig`       | name, params, returnType, modifiers     | AST.hpp:1011-1020 |
| `InheritedStmt`   | methodName, args                        | AST.hpp:771-784   |
| `IsExpr`          | value, typeName                         | AST.hpp:60        |

### Semantic Analysis: COMPREHENSIVE ✅

| Feature                  | Status     | Location                           |
|--------------------------|------------|------------------------------------|
| Class registration       | ✅ Complete | SemanticAnalyzer_Decl.cpp:358-624  |
| Interface registration   | ✅ Complete | SemanticAnalyzer_Decl.cpp:626-657  |
| Override validation      | ✅ Complete | SemanticAnalyzer_Class.cpp:109-132 |
| Signature matching       | ✅ Complete | SemanticAnalyzer_Class.cpp:162-182 |
| Interface conformance    | ✅ Complete | SemanticAnalyzer_Class.cpp:189-244 |
| Abstract class detection | ✅ Complete | SemanticAnalyzer_Class.cpp:369-420 |
| Weak field validation    | ✅ Complete | SemanticAnalyzer_Class.cpp:269-290 |
| Inheritance hierarchy    | ✅ Complete | SemanticAnalyzer_Class.cpp:343-367 |

### Lowering to IL: COMPREHENSIVE ✅

| Feature                    | Status     | Location                |
|----------------------------|------------|-------------------------|
| Class layout computation   | ✅ Complete | Lowerer_OOP.cpp:108-163 |
| Vtable layout              | ✅ Complete | Lowerer_OOP.cpp:165-218 |
| Runtime class registration | ✅ Complete | Lowerer_OOP.cpp:250-372 |
| Constructor calls          | ✅ Complete | Lowerer_OOP.cpp:411-465 |
| Virtual method dispatch    | ✅ Complete | Lowerer_OOP.cpp:471-559 |
| Object field access        | ✅ Complete | Lowerer_OOP.cpp:565-618 |
| IS expression              | ✅ Complete | Lowerer_Expr.cpp:60-89  |
| Inherited statement        | ✅ Complete | Lowerer_Stmt.cpp:96-140 |
| Interface tables           | ✅ Complete | Lowerer_OOP.cpp:709-815 |
| Interface method dispatch  | ✅ Complete | Lowerer_OOP.cpp:869-975 |

---

## Known Bugs (Blocking End-to-End)

### BUG-PAS-OOP-001: Class Field Access in Methods ✅ FIXED

**Status:** FIXED (verified December 2025)
**Resolution:** Implicit field access now works correctly. Fields can be accessed without `Self.` prefix in methods.

```pascal
function TCircle.Area: Real;
begin
  Result := 3.14159 * Radius * Radius;  // Works - Radius resolved as Self.Radius
end;
```

### BUG-PAS-OOP-002: Constructor Calls ✅ FIXED

**Status:** FIXED (verified December 2025)
**Resolution:** `TClassName.Create` syntax now correctly recognized and lowered to allocation + constructor call.

```pascal
var c: TCircle;
begin
  c := TCircle.Create(5.0);  // Works - generates rt_obj_new_i64 + ctor call
end.
```

### BUG-PAS-OOP-003: Record/Class Field Access Not Lowered ✅ FIXED

**Status:** FIXED (verified December 2025)
**Resolution:** Global record and class field access now correctly lowered to IL. The fix added:

- Global variable lookup in `lowerField()` for record field reads (Lowerer_Expr.cpp)
- Global record field assignment handling in `lowerAssign()` (Lowerer_Stmt.cpp)

```pascal
type TPoint = record X, Y: Integer; end;
var p: TPoint;
begin
  p.X := 5;      // Works - generates GEP + store
  WriteLn(p.X);  // Works - generates GEP + load, outputs 5
end.
```

---

## Feature Gap Analysis: Pascal vs BASIC

| Feature              | BASIC | Pascal | Gap     |
|----------------------|-------|--------|---------|
| Classes              | ✅     | ✅      | None    |
| Single inheritance   | ✅     | ✅      | None    |
| Interfaces           | ✅     | ✅      | None    |
| VIRTUAL methods      | ✅     | ✅      | None    |
| OVERRIDE methods     | ✅     | ✅      | None    |
| ABSTRACT methods     | ✅     | ✅      | None    |
| FINAL methods        | ✅     | ❓      | Verify  |
| FINAL classes        | ✅     | ❌      | **Gap** |
| Properties           | ✅     | ✅      | None    |
| Constructors         | ✅     | ✅*     | Bug #2  |
| Destructors          | ✅     | ✅      | None    |
| IS operator          | ✅     | ✅      | None    |
| AS operator          | ✅     | ❓      | Verify  |
| Self/ME keyword      | ✅     | ✅      | None    |
| BASE/Inherited calls | ✅     | ✅      | None    |
| DELETE statement     | ✅     | ❌      | **Gap** |
| PUBLIC/PRIVATE       | ✅     | ✅      | None    |
| Static fields        | ✅     | ❌      | **Gap** |
| Static methods       | ✅     | ❌      | **Gap** |
| Static constructors  | ✅     | ❌      | **Gap** |

---

## Staged Implementation Plan

### Stage 1: Bug Fixes (CRITICAL PATH) 🔴

**Goal:** Make existing OOP infrastructure actually work end-to-end.

**Status:** ✅ COMPLETE - All 3 bugs fixed

**Completed:**

- ✅ BUG-PAS-OOP-001: Field access in methods - FIXED
- ✅ BUG-PAS-OOP-002: Constructor calls - FIXED
- ✅ BUG-PAS-OOP-003: Field access lowering - FIXED (December 2025)

**Verification:** All tests pass in `test_pascal_oop_status`

### Stage 2: AS Expression Support 🟡

**Goal:** Complete RTTI with safe downcasting.

**Tasks:**

1. Add `AsExpr` to AST (similar to IsExpr)
2. Add parsing for `expr as TypeName`
3. Add semantic analysis (verify type compatibility)
4. Add lowering using `rt_cast_as` / `rt_cast_as_iface`

**Estimated Files:** 4 (AST.hpp, Parser_Expr.cpp, SemanticAnalyzer_Expr.cpp, Lowerer_Expr.cpp)

### Stage 3: DELETE Statement 🟡

**Goal:** Explicit object destruction.

**Tasks:**

1. Add `DeleteStmt` to AST
2. Add parsing for `Delete(expr)` or `FreeAndNil(expr)`
3. Add semantic analysis (verify object type)
4. Add lowering: call destructor if exists, then free memory

**Estimated Files:** 4 (AST.hpp, Parser_Stmt.cpp, SemanticAnalyzer_Stmt.cpp, Lowerer_Stmt.cpp)

### Stage 4: FINAL Modifier for Methods 🟡

**Goal:** Prevent method override in descendants.

**Tasks:**

1. Add `isFinal` flag to MethodSig and MethodInfo
2. Parse `final` keyword after method signature
3. Validate: cannot override final methods in semantic analysis
4. No lowering changes needed (final is compile-time only)

**Estimated Files:** 3 (AST.hpp, Parser_OOP.cpp, SemanticAnalyzer_Class.cpp)

### Stage 5: FINAL Modifier for Classes 🟢

**Goal:** Prevent class inheritance.

**Tasks:**

1. Add `isFinal` flag to ClassDecl and ClassInfo
2. Parse `final` keyword in class declaration
3. Validate: cannot inherit from final class

**Estimated Files:** 3 (AST.hpp, Parser_OOP.cpp, SemanticAnalyzer_Class.cpp)

### Stage 6: Static Members 🟢

**Goal:** Class-level fields and methods.

**Tasks:**

1. Add `isStatic` flag to FieldInfo and MethodInfo
2. Parse `class var` and `class function/procedure` syntax
3. Semantic analysis: static members don't have Self
4. Lowering: static fields as module-level variables, static methods as regular functions

**Estimated Files:** 6+ (AST.hpp, Parser_OOP.cpp, SemanticAnalyzer_*.cpp, Lowerer_OOP.cpp)

### Stage 7: Property Access Control 🟢

**Goal:** Separate visibility for getter/setter.

**Tasks:**

1. Add per-accessor visibility to PropertyDecl
2. Parse `private set` or `protected get` modifiers
3. Validate access at call sites

**Estimated Files:** 3 (AST.hpp, Parser_OOP.cpp, SemanticAnalyzer_Expr.cpp)

### Stage 8: Diagnostics and Error Messages 🟢

**Goal:** Match BASIC's diagnostic quality.

**Tasks:**

1. Add specific error codes for all OOP errors
2. Add "did you mean" suggestions for method names
3. Add "signature expected" hints for override mismatches

**Estimated Files:** 2 (SemanticAnalyzer_Class.cpp, Diagnostic codes)

### Stage 9: Documentation 🟢

**Goal:** Complete Pascal OOP documentation.

**Tasks:**

1. Update docs/pascal-language.md with OOP section
2. Create docs/devdocs/pascal-oop-semantics.md
3. Document differences from Delphi/FreePascal

### Stage 10: Interop Testing 🟢

**Goal:** Verify Pascal OOP works with BASIC OOP.

**Tasks:**

1. Create test cases mixing Pascal and BASIC classes
2. Verify vtable layout compatibility
3. Verify interface dispatch compatibility

---

## Acceptance Criteria for Each Stage

### Stage 1 Complete When:

- [x] `TMyClass.Create` compiles and instantiates ✅ DONE
- [x] `Self.field` resolves inside methods ✅ DONE
- [x] Implicit `field` (without Self) resolves in methods ✅ DONE
- [x] `obj.field` read/write works in expressions ✅ DONE (BUG-003 fixed)
- [x] `record.field` read/write works in expressions ✅ DONE (BUG-003 fixed)

### Stage 2 Complete When:

- [ ] `expr as TClass` returns typed pointer or nil
- [ ] `expr as IInterface` returns typed pointer or nil

### Stage 3 Complete When:

- [ ] `Delete(obj)` calls destructor and frees memory
- [ ] `FreeAndNil(obj)` calls destructor and sets to nil

### Stage 4-5 Complete When:

- [ ] `final` methods cannot be overridden
- [ ] `final` classes cannot be inherited

### Stage 6 Complete When:

- [ ] `class var` fields shared across instances
- [ ] `class function` callable without instance

---

## Test Coverage Plan

### Unit Tests (src/tests/unit/frontends/pascal/)

- `PascalOOPParserTests.cpp` - parsing all OOP constructs
- `PascalOOPSemanticTests.cpp` - type checking and validation
- `PascalOOPLoweringTests.cpp` - IL generation

### Integration Tests (src/tests/data/pascal/)

- `oop_class_basic.pas` - simple class with fields and methods
- `oop_inheritance.pas` - base/derived class hierarchy
- `oop_interface.pas` - interface implementation
- `oop_virtual.pas` - virtual method dispatch
- `oop_is_as.pas` - RTTI operators
- `oop_property.pas` - property access

### Golden Tests

- IL output verification for each OOP construct
- Compare Pascal and BASIC IL for equivalent programs

---

## Risk Assessment

| Risk                              | Impact | Mitigation                        |
|-----------------------------------|--------|-----------------------------------|
| Bug fixes break existing code     | Medium | Comprehensive test coverage first |
| Vtable incompatibility with BASIC | High   | Share Lowerer_OOP.cpp patterns    |
| Static member complexity          | Medium | Implement after core bugs fixed   |
| Documentation lag                 | Low    | Update docs with each stage       |

---

## Timeline Recommendation

**Not providing time estimates per CLAUDE.md instructions.**

Priority order based on impact:

1. **Stage 1** - Most critical, unlocks all other work
2. **Stage 2** - Completes RTTI story
3. **Stage 3** - Enables proper memory management
4. **Stages 4-7** - Feature completeness
5. **Stages 8-10** - Polish and interop

---

## Appendix: File Reference

### Core OOP Files

- `src/frontends/pascal/Parser_OOP.cpp` (766 lines)
- `src/frontends/pascal/SemanticAnalyzer_Class.cpp` (468 lines)
- `src/frontends/pascal/Lowerer_OOP.cpp` (1000 lines)
- `src/frontends/pascal/AST.hpp` (OOP nodes at lines 1011-1125)

### Supporting Files

- `src/frontends/pascal/SemanticAnalyzer.hpp` (type system)
- `src/frontends/pascal/SemanticAnalyzer_Decl.cpp` (class registration)
- `src/frontends/pascal/Lowerer_Expr.cpp` (IS expression)
- `src/frontends/pascal/Lowerer_Stmt.cpp` (inherited statement)

### Bug Documentation

- `bugs/pascal_bugs.md` - Known Pascal bugs
