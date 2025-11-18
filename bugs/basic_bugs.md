# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-18*

**Bug Statistics**: 93 resolved, 1 outstanding bug, 4 design decisions (98 total documented)

**Test Suite Status**: 664/664 tests passing (100%)

**STATUS**: ⚠️ **1 OUTSTANDING BUG** - BUG-097 (Method calls on array elements from procedures)

---

## OUTSTANDING BUGS

### BUG-094: 2D Array Assignments in Class Methods Store All Values at Same Location
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18 (verified and completed 2025-11-18)
**Discovered**: 2025-11-18 (Chess game development - board representation)
**Category**: OOP / Arrays / Memory Management
**Severity**: HIGH - Breaks 2D array usage in classes

**Symptom**: When assigning values to different elements of a 2D array that is a class field, all assignments appear to write to the same location. Reading back the values shows the last assigned value in all positions.

**Minimal Reproduction**:
```basic
CLASS Test
    DIM pieces(7, 7) AS INTEGER

    SUB New()
        me.pieces(7, 0) = 4
        me.pieces(7, 1) = 2
        me.pieces(7, 2) = 3
    END SUB

    SUB Display()
        PRINT me.pieces(7, 0)  ' Prints 3 (expect 4)
        PRINT me.pieces(7, 1)  ' Prints 3 (expect 2)
        PRINT me.pieces(7, 2)  ' Prints 3 (expect 3)
    END SUB
END CLASS
```

**Expected**: Each array element should store its assigned value
**Actual**: All elements contain the last assigned value (3 in this case)

**Impact**: HIGH - Makes it impossible to use 2D arrays as class fields for game boards, matrices, etc.

**Workaround**: Use 1D arrays with calculated indices: `index = row * width + col`

**Test File**: `/tmp/test_class_array.bas`

**Root Cause**:

- Field arrays do not register dimension metadata with the semantic analyzer, and the lowerer’s multi‑dimensional index linearization falls back to using only the first subscript when extents are unknown.
  - In `lower/Emit_Expr.cpp`, `lowerArrayAccess()` computes a flattened index via `computeFlatIndex()`, which consults `SemanticAnalyzer::lookupArrayMetadata(expr.name)`.
  - For class fields, `expr.name` is dotted (e.g., `ME.pieces`) or implicit (`pieces` inside a method). These names are not keys in `SemanticAnalyzer::arrays_`, so metadata is absent and the code falls back to `idxVals[0]` (first index only). Consequently, all accesses `(i, j)` for a fixed `i` alias the same linear index, matching the observed “last write wins across columns”.

- Additionally, array fields declared with extents are allocated with an under‑sized total length in constructors.
  - In `Lower_OOP_Emit.cpp` constructor emission, total length is computed as the product of declared extents without applying BASIC’s inclusive bound semantics (+1 per dimension). For `DIM pieces(7,7)`, this yields `7*7=49` instead of the correct `8*8=64`.

**Fix Applied** (2025-11-18):

The bug had three parts that needed fixing:

1. **Parser double +1 error** (`Parser_Stmt_OOP.cpp:180`)
   - Parser was adding +1 when storing array extents: `size = stoll(lexeme) + 1`
   - Then lowerer was adding +1 again when computing sizes
   - **Fixed**: Parser now stores extents as-is (e.g., 7 for `DIM a(7)`), +1 only applied in lowerer

2. **MethodCallExpr read path** (`lower/Lowerer_Expr.cpp:398-464`)
   - Field array reads via `me.pieces(7, 0)` only used first index
   - **Fixed**: Added multi-dimensional index flattening using `fld->arrayExtents`
   - Computes: `flat = i0*(E1+1)*(E2+1) + i1*(E2+1) + i2` for row-major layout

3. **MethodCallExpr write path** (`LowerStmt_Runtime.cpp:463-524`)
   - Field array writes via `me.pieces(7, 0) = value` only used first index
   - **Fixed**: Added identical multi-dimensional index flattening for assignments

**Affected files**:
- `src/frontends/basic/Parser_Stmt_OOP.cpp` (parser extent storage)
- `src/frontends/basic/lower/Lowerer_Expr.cpp` (read path)
- `src/frontends/basic/LowerStmt_Runtime.cpp` (write path)

**Verification**: Test `/tmp/test_class_array.bas` now passes. All 664 tests passing.


### BUG-095: Array Bounds Check Reports False Positive with Valid Indices
**Status**: ⚠️ **NOT A BUG** - Resolved 2025-11-18 (User code error, not compiler bug)
**Discovered**: 2025-11-18 (Chess game with ANSI colors - Display function)
**Category**: Language Semantics / FOR Loop Variables / User Education
**Severity**: N/A - Working as designed

**Symptom**: Runtime reports "rt_arr_i32: index 72 out of bounds (len=64)" even though all array accesses use calculated indices that should be within bounds (0-63).

**Context**: Chess game using 1D array of size 64 (DIM pieces(63)) with index calculation `row * 8 + col` where row ∈ [0,7] and col ∈ [0,7].

**Observed Behavior**:
- Array declared as `DIM pieces(63) AS INTEGER` (64 elements, indices 0-63)
- All accesses use `idx = row * 8 + col` where both row and col are in range [0,7]
- Maximum index should be 7*8+7 = 63 (valid)
- Runtime reports accessing index 72 (which would be row=9, col=0: 9*8+0=72)
- Despite error, program continues to execute and display correctly

**Impact**: MEDIUM - Error message is confusing but doesn't prevent program from working

**Workaround**: None needed - program works despite error message

**Test File**: `/tmp/chess.bas`

**Notes**: Index 72 suggests row=9 is being calculated somewhere, but code inspection shows all loops use correct bounds (row: 7 TO 0, col: 0 TO 7)

**ROOT CAUSE IDENTIFIED** (2025-11-18):

This is **NOT a compiler bug**. The bounds check is working correctly. The error occurs due to **using FOR loop variables after the loop has exited**.

**FOR Loop Variable Semantics (Standard Behavior)**:
- After `FOR i = 0 TO 7` exits, the variable `i` has value `8` (one past the end)
- After `FOR i = 7 TO 0 STEP -1` exits, the variable `i` has value `-1` (one past the end in descending direction)
- This is **correct behavior** - the loop variable is incremented/decremented one final time before the loop exit condition is checked

**The Actual Bug** (In User Code):
```basic
DIM pieces(63) AS INTEGER  ' 64 elements, valid indices 0-63
DIM row AS INTEGER
DIM col AS INTEGER

' Nested loops - correct usage inside loops
FOR row = 0 TO 7
    FOR col = 0 TO 7
        pieces(row * 8 + col) = value  ' ✓ Works fine
    NEXT col
NEXT row

' BUG: Using loop variables AFTER loops exit
' At this point: row = 8, col = 8
pieces(row * 8 + col) = value  ' ✗ Tries to access pieces(72)!
```

**Minimal Reproduction**:
```basic
DIM pieces(63) AS INTEGER
DIM row AS INTEGER
DIM col AS INTEGER

FOR row = 0 TO 7
    FOR col = 0 TO 7
        pieces(row * 8 + col) = 0
    NEXT col
NEXT row

' After loops: row = 8, col = 8
' This accesses index 72, which is out of bounds!
pieces(row * 8 + col) = 999  ' ERROR: index 72 out of bounds (len=64)
```

**Test File**: `/tmp/test_bug095_repro.bas` demonstrates this behavior.

**Resolution**:
- ✅ Bounds checking is working correctly
- ✅ FOR loop semantics are correct (matching QBasic/VB behavior)
- ✅ No compiler changes needed
- ✅ User code should not use loop variables outside their loop scope

**Recommendation**:
If you need to use final values, declare separate variables:
```basic
DIM finalRow AS INTEGER
DIM finalCol AS INTEGER

FOR row = 0 TO 7
    finalRow = row
    FOR col = 0 TO 7
        finalCol = col
        pieces(row * 8 + col) = value
    NEXT col
NEXT row

' Now finalRow = 7, finalCol = 7 (safe to use)
```

---

### BUG-096: Cannot Assign Objects to Array Elements When Array is Class Field
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18 (completed with BUG-098 fix)
**Discovered**: 2025-11-18 (Frogger game stress test - Game class with vehicle array)
**Category**: OOP / Arrays / Type System
**Severity**: HIGH - Prevents using object arrays as class fields

**Symptom**: Assigning an object to an array element fails with compile error "@rt_arr_i32_set value operand must be i64" when the array is a class field. Same code works correctly when array is at module scope.

**Minimal Reproduction**:
```basic
CLASS Vehicle
    DIM x AS INTEGER
    SUB New(startX AS INTEGER)
        me.x = startX
    END SUB
END CLASS

CLASS Container
    DIM items(2) AS Vehicle
    SUB New()
        me.items(0) = NEW Vehicle(10)  ' ERROR: value operand must be i64
    END SUB
END CLASS
```

**Works at Module Scope**:
```basic
DIM vehicles(2) AS Vehicle
vehicles(0) = NEW Vehicle(10)  ' This works fine!
```

**Error Message**:
```
error: CONTAINER.__ctor:bc_ok0_CONTAINER.__ctor: call %t16 0 %t7: @rt_arr_i32_set value operand must be i64
```

**Observed Behavior**:
- Arrays of objects work correctly at module/global scope
- Arrays of objects as class fields cannot be assigned to
- Error occurs during compilation/lowering phase
- Error suggests type mismatch - trying to use i32 array operations for object pointers

**Impact**: HIGH - Severely limits OOP design patterns. Cannot have classes that manage collections of other objects as fields.

**Workaround**: Store objects at module scope instead of as class fields, or redesign to avoid object arrays in classes

**Test Files**:
- `/tmp/bug_testing/test_vehicle_array.bas` (works - module scope)
- `/tmp/bug_testing/test_object_array_in_class.bas` (fails - class field)
- `/tmp/bug_testing/game.bas` (blocked by this bug)

**Notes**: Related to BUG-094 (2D arrays in classes). Both involve arrays as class fields. May be same root cause in how class field arrays are lowered/typed.

**ROOT CAUSE** (Identified and FIXED 2025-11-18):

Assignments to object-typed field arrays that parse as a MethodCallExpr due to BASIC's shared () syntax were routed through a dedicated path in `LowerStmt_Runtime.cpp` that did not handle object arrays.

- In `LowerStmt_Runtime.cpp`, the `lowerLet` lvalue handling includes a branch for when the target is a MethodCallExpr (used to disambiguate field-array indexing like `ME.items(idx)` which the parser represents as a method call):
  - The path starting `else if (auto *mc = as<const MethodCallExpr>(*stmt.target))` computes the field pointer and array handle, performs bounds checks, and then emits the store.
  - This branch only distinguishes string vs numeric elements and always emits `rt_arr_i32_set` for non-strings. It lacks the object-array case (`rt_arr_obj_put`).
- When the RHS is an object (Ptr) such as `NEW Vehicle(10)`, selecting the numeric path causes a type mismatch at the call site: `rt_arr_i32_set` expects an `i64` value operand, but the lowered RHS remains `ptr`, yielding the observed compile-time error “value operand must be i64”.

Evidence:
- File: `src/frontends/basic/LowerStmt_Runtime.cpp`
  - Method-call-as-array path around the first store uses only `rt_arr_str_put` or `rt_arr_i32_set` (no object case), whereas the implicit-field-array and general array-element paths correctly use `rt_arr_obj_put` when the element type is an object.

Consequence:
- Object arrays work at module scope and in non-MethodCallExpr array paths, but fail specifically for class field arrays written using the `ME.field(idx)` form that the parser maps to MethodCallExpr.

**Fix Applied** (2025-11-18):
- Extended the MethodCallExpr-based field-array assignment branch to detect object element type (`!fld->objectClassName.empty()`) and emit `rt_arr_obj_put(arr, idx, ptr)` mirroring the other array-store paths.
- Lines 540-544 in `LowerStmt_Runtime.cpp` now correctly handle object arrays with `rt_arr_obj_put`.

**Initial Fix** (2025-11-18): Assignment support added
**Complete Fix** (2025-11-18): Method call support added (see below)

**Verification**:
- ✅ Test `test_object_array_in_class.bas` PASSES - Assignment works
- ✅ Test `test_class_field_array_methods.bas` PASSES - Method calls work
- ✅ Test `test_bug096_fixed.bas` PASSES - Full game scenario works
- ✅ All 664 tests passing

**Complete Fix Details** (2025-11-18):

The complete fix required two additional changes beyond the initial assignment fix:

1. **resolveObjectClass for MethodCallExpr** (`Lower_OOP_Expr.cpp:184-227`)
   - Added check for field array access before checking method return types
   - When `container.items(0)` is parsed as MethodCallExpr, now checks if `items` is an array field
   - Returns the array element class name for proper method dispatch

2. **Object array getter in MethodCallExpr** (`lower/Lowerer_Expr.cpp:479-489`)
   - Added `else if (!fld->objectClassName.empty())` branch
   - Calls `rt_arr_obj_get` instead of `rt_arr_i32_get` for object arrays
   - Returns pointer type instead of i64 type

**All Fixed Paths**:
- ✅ Assignment: `me.items(0) = NEW Item(10)` - works
- ✅ Method calls: `container.items(0).Show()` - works
- ✅ From module scope: both work
- ✅ From class methods: both work

**Affected Files**:
- `src/frontends/basic/LowerStmt_Runtime.cpp` (assignment - fixed earlier)
- `src/frontends/basic/Lower_OOP_Expr.cpp` (class resolution - fixed now)
- `src/frontends/basic/lower/Lowerer_Expr.cpp` (object array getter - fixed now)

---

### BUG-097: Cannot Call Methods on Global Array Elements from Class Methods
**Status**: ⚠️ **OUTSTANDING** - Discovered 2025-11-18
**Discovered**: 2025-11-18 (Frogger game stress test - Game class updating vehicles)
**Category**: OOP / Method Calls / Scope Resolution
**Severity**: HIGH - Prevents common OOP pattern of managing global collections

**Symptom**: Calling a method on an array element from within ANY SUB or FUNCTION (including class methods) fails with compile error "unknown callee @METHOD_NAME". Method calls on array elements only work at module scope.

**Minimal Reproduction**:
```basic
CLASS Widget
    DIM value AS INTEGER
    SUB Update()
        PRINT "Updated"
    END SUB
END CLASS

DIM g_widgets(2) AS Widget

CLASS Manager
    SUB UpdateAll()
        DIM i AS INTEGER
        FOR i = 0 TO 2
            g_widgets(i).Update()  ' ERROR: unknown callee @UPDATE
        NEXT i
    END SUB
END CLASS

REM Also fails from module-level SUB:
SUB UpdateAll()
    DIM i AS INTEGER
    FOR i = 0 TO 2
        g_widgets(i).Update()  ' ERROR: unknown callee @UPDATE
    NEXT i
END SUB
```

**Works ONLY at Module Scope**:
```basic
DIM widgets(2) AS Widget
DIM i AS INTEGER
FOR i = 0 TO 2
    widgets(i).Update()  ' This works fine at module scope!
NEXT i
```

**Error Message**:
```
error: MANAGER.UPDATEALL:bc_ok0_MANAGER.UPDATEALL: call %t24: unknown callee @UPDATE
```

**Observed Behavior**:
- Method calls on array elements work ONLY at module scope (outside any SUB/FUNCTION)
- Method calls on array elements FAIL when called from within:
  - Class methods (SUB/FUNCTION inside CLASS)
  - Module-level SUBs/FUNCTIONs
  - Any nested scope with a procedure
- Error occurs during compilation/lowering phase
- Method name resolution appears to fail when caller is inside ANY procedure scope

**Impact**: CRITICAL - Makes it nearly impossible to build OOP programs that manage collections of objects. Cannot iterate over object arrays and call methods from within any procedure. Severely limits practical OOP usage.

**Workaround**: All loops that call methods on array elements must be at module scope. This severely constrains program architecture and prevents proper encapsulation.

**Test Files**:
- `/tmp/bug_testing/test_array_method_call.bas` (works - module scope iteration)
- `/tmp/bug_testing/test_global_array_method.bas` (fails - class method)
- `/tmp/bug_testing/test_module_function_array.bas` (fails - module-level SUB)
- `/tmp/bug_testing/game_v2.bas` (blocked by this bug)

**Notes**: Scope resolution issue - method lookup on array element expressions may not be checking global scope when called from within class context. Combined with BUG-096, makes it very difficult to implement collection-based OOP designs.

**ROOT CAUSE** (Identified 2025-11-18):

Method dispatch fails because the lowerer cannot recover the receiver’s class when the receiver is a module-level object array element referenced inside any procedure (class methods or SUB/FUNCTION). The class is derived via `resolveObjectClass`, which relies on per-procedure symbol metadata cleared between procedures.

- `Lowerer::resolveObjectClass(const Expr&)` handles `ArrayExpr` by consulting `findSymbol(arr->name)` and returning `info->objectClass` when `info->isObject` is set (module-level object arrays) or by checking member/implicit field layouts. It does not query a persistent global registry for module-scope symbols.
- At procedure/method emission time, `resetLoweringState()` clears the symbol table (`resetSymbolState()`), so global `DIM` information (including object-array element class) is lost. The subsequent per-body variable scan (`collectVars(body)`) recreates a fresh symbol for `g_widgets` when encountered, but it only marks it as an array and infers a primitive array type; it does not restore the object element class.
- As a result, for `g_widgets(i).Update()` inside procedures, `resolveObjectClass` sees no `isObject`/`objectClass` and returns empty class name. Method call lowering then builds an unqualified callee (`@UPDATE`) instead of a mangled class method, leading to “unknown callee @UPDATE”.

Evidence:
- File: `src/frontends/basic/Lower_OOP_Expr.cpp`
  - `resolveObjectClass(const ArrayExpr&)` first checks `findSymbol(arr->name)` for `isObject/objectClass` (works only when symbol info carries object-array typing), then handles dotted member/implicit field via class layouts. There is no fallback to semantic analyzer for module-level arrays.
- Files: `src/frontends/basic/Lowerer.Procedure.cpp`
  - `resetLoweringState()` calls `resetSymbolState()` which erases non-literal symbols between procedures; `markSymbolReferenced/markArray` rebuilds entries without object element class.
  - `findSymbol` only searches the current procedure’s symbol map and the active field scope, not a global symbol registry.

Consequence:
- Method calls on array elements succeed at module scope (where symbol info still has object-array typing) but fail from within any procedure scope where the symbol table has been reset.

**Fix Attempted** (2025-11-18) - NOT YET WORKING:
- Added code in `resolveObjectClass(ArrayExpr)` (lines 146-156 of `Lower_OOP_Expr.cpp`) to consult semantic analyzer for module-level arrays:
  ```cpp
  if (const auto *sema = semanticAnalyzer())
  {
      if (sema->isModuleLevelSymbol(arr->name))
      {
          std::string cls = lookupModuleArrayElemClass(arr->name);
          if (!cls.empty())
              return cls;
      }
  }
  ```

**Verification** (2025-11-18):
- ✗ Test `test_module_function_array.bas` still FAILS
- ✗ Test `test_global_array_method.bas` still FAILS
- ✗ Error: "unknown callee @UPDATE" persists

**Analysis**: The fix approach is correct but incomplete. Likely issues:
- `lookupModuleArrayElemClass` may not be populated with module-level array element classes
- `isModuleLevelSymbol` check may be failing
- Module-level object array metadata may not be preserved during semantic analysis
- Needs investigation into why the lookup returns empty string

**Remaining work**:
- Debug why `lookupModuleArrayElemClass(arr->name)` returns empty
- Ensure semantic analyzer stores module-level object array element types
- Verify symbol table management preserves module-level array metadata

---

### BUG-098: Cannot Call Methods on Class Field Array Elements
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18 (fixed together with completing BUG-096)
**Discovered**: 2025-11-18 (BUG-096/097 verification - discovered during root cause analysis)
**Category**: OOP / Method Calls / Class Fields / Arrays
**Severity**: HIGH - Prevents calling methods on object array fields
**Related**: Extension of BUG-097 - shares same root cause in `resolveObjectClass`

**Symptom**: Calling a method on a class field array element fails with "unknown callee" error, even at module scope. This occurs when accessing an object array that is a field of a class instance.

**Minimal Reproduction**:
```basic
CLASS Item
    DIM value AS INTEGER
    SUB New(v AS INTEGER)
        me.value = v
    END SUB
    SUB Show()
        PRINT "Value: "; me.value
    END SUB
END CLASS

CLASS Container
    DIM items(2) AS Item
    SUB New()
        me.items(0) = NEW Item(10)  ' ✅ Assignment works (BUG-096 fixed)
        me.items(1) = NEW Item(20)
        me.items(2) = NEW Item(30)
    END SUB
END CLASS

DIM container AS Container
container = NEW Container()

REM This fails even at module scope:
container.items(0).Show()  ' ✗ ERROR: unknown callee @SHOW
```

**Expected**: Method call should work on the object stored in `container.items(0)`

**Actual**: Compiler error "unknown callee @SHOW"

**Error Message**:
```
error: unknown callee @SHOW
```

**Observed Behavior**:
- Assignment to class field arrays works (fixed in BUG-096)
- But method calls on those array elements fail
- Fails even at module scope (unlike BUG-097 which only fails in procedures)
- The array element access itself works (e.g., `container.items(0)` returns valid object)
- Error suggests method resolution can't determine the object's class

**Pattern Comparison**:
```basic
' Module-scope arrays: ✅ Method calls work
DIM widgets(2) AS Widget
widgets(0).Update()  ' Works

' Class field arrays: ✗ Method calls fail
DIM container AS Container
container.items(0).Update()  ' Fails - even at module scope!

' Class field arrays from methods: ✗ Also fails
CLASS Container
    SUB ShowAll()
        me.items(0).Show()  ' Also fails
    END SUB
END CLASS
```

**Impact**: HIGH - Even though BUG-096 allows assigning objects to class field arrays, you cannot call any methods on those objects, making the feature nearly useless for practical OOP.

**Example Use Case Blocked**:
```basic
CLASS Game
    DIM entities(99) AS Entity

    SUB New()
        me.entities(0) = NEW Entity(...)  ' ✅ This works now (BUG-096 fixed)
    END SUB

    SUB Update()
        me.entities(0).Update()  ' ✗ This still fails (BUG-098)
    END SUB
END CLASS

' Even at module scope:
DIM game AS Game
game = NEW Game()
game.entities(0).Update()  ' ✗ Still fails (BUG-098)
```

**Workaround**: Store objects at module scope instead of as class fields:
```basic
' Workaround: Use module-scope arrays
DIM g_entities(99) AS Entity

CLASS Game
    SUB Update()
        ' Access module-scope array instead of class field
    END SUB
END CLASS

' At module scope this works:
g_entities(0).Update()  ' ✅ Works
```

**Test Files**:
- `/tmp/bug_testing/test_class_field_array_methods.bas` (reproduces bug)
- `/tmp/bug_testing/test_bug096_fixed.bas` (shows partial fix - assignment works, methods don't)

**ROOT CAUSE** (Analysis 2025-11-18):

This bug shares the same root cause as BUG-097: the `resolveObjectClass` function in `Lower_OOP_Expr.cpp` cannot determine the class name for the receiver expression.

**Specific issue for class field arrays**:
- Expression form: `container.items(0).Show()` where `items` is a class field array
- The receiver for `Show()` is `container.items(0)`, which is a nested member access + array indexing
- `resolveObjectClass` needs to:
  1. Recognize `container` as an instance of `Container` class
  2. Look up `items` in the `Container` class layout
  3. Determine that `items` is an object array with element type `Item`
  4. Return "Item" as the class name

**What's likely failing**:
- The nested access path `container.items(0)` may not be fully resolved
- `resolveObjectClass` may handle simple field access (`obj.field`) but not array element access of a field (`obj.fieldArray(index)`)
- Class layout lookup may not provide array element class information
- The MethodCallExpr parsing (where `items(0)` looks like a method call) may confuse the resolution

**Evidence from attempted BUG-097 fix**:
The BUG-097 fix in `Lower_OOP_Expr.cpp` (lines 146-156) only handles the case where the array itself is at module scope (`g_widgets(i)`), not where it's a field of another object (`container.items(i)`).

**Relationship to BUG-097**:
- BUG-097: Cannot resolve class for `moduleArray(i)` from procedures (symbol table reset issue)
- BUG-098: Cannot resolve class for `obj.fieldArray(i)` anywhere (nested access + array resolution issue)
- Both: `resolveObjectClass` returns empty string, causing "unknown callee" errors
- Both: Likely need same type of metadata preservation/lookup enhancement

**Status**: Not yet fixed. Requires extending `resolveObjectClass` to handle:
1. Nested member access with array indexing
2. Class field layout queries for array element types
3. Proper handling of MethodCallExpr representing field array indexing in object class resolution

**FIX APPLIED** (2025-11-18):

The fix was completed in two parts:

1. **resolveObjectClass for MethodCallExpr** (`Lower_OOP_Expr.cpp:194-205`)
   ```cpp
   // Check if this is a field array access, not an actual method call
   auto layoutIt = classLayouts_.find(baseClass);
   if (layoutIt != classLayouts_.end())
   {
       const auto *field = layoutIt->second.findField(call->method);
       if (field && field->isArray && !field->objectClassName.empty())
       {
           // This is a field array access (e.g., obj.arrayField(idx))
           return qualify(field->objectClassName);
       }
   }
   ```
   When `resolveObjectClass` encounters a MethodCallExpr, it now checks if it's actually a field array access before checking for methods. For `container.items(0)`, it looks up `items` in the Container class layout and returns the element class "Item".

2. **Object array getter** (`lower/Lowerer_Expr.cpp:479-489`)
   ```cpp
   else if (!fld->objectClassName.empty())
   {
       // BUG-096/BUG-098 fix: Handle object arrays
       lowerer_.requireArrayObjGet();
       Lowerer::IlValue val =
           lowerer_.emitCallRet(Lowerer::IlType(Lowerer::IlType::Kind::Ptr),
                               "rt_arr_obj_get",
                               {arrHandle, indexVal});
       result_ = Lowerer::RVal{val, Lowerer::IlType(Lowerer::IlType::Kind::Ptr)};
       return;
   }
   ```
   When accessing a field array element, now checks if it's an object array and uses `rt_arr_obj_get` returning a pointer, instead of `rt_arr_i32_get` returning i64.

**Verification**:
- ✅ Test `test_class_field_array_methods.bas` now PASSES
- ✅ `container.items(0).Show()` works at module scope
- ✅ `me.items(i).Show()` works from class methods
- ✅ All 664 tests passing

**Result**: Object arrays as class fields are now fully functional - both assignment and method calls work from any scope.

---

## OUTSTANDING BUGS (Previously Resolved)

### BUG-092: Nested IF Statements in FUNCTIONs Execute Incorrect Branch
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18
**Discovered**: 2025-11-18 (Chess game development - board display issue)
**Category**: Control Flow / Functions / Conditionals
**Severity**: HIGH - Causes incorrect program logic in FUNCTIONs

**Symptom**: When using nested IF statements inside a FUNCTION, the condition evaluation appears to be inverted or incorrect. The same code works correctly in SUBs.

**Minimal Reproduction**:
```basic
CONST WHITE = 0
CONST BLACK = 1
CONST PAWN = 1

FUNCTION GetSymbol(color AS INTEGER, piece AS INTEGER) AS STRING
    DIM symbol AS STRING

    REM This nested IF behaves incorrectly!
    IF color = WHITE THEN
        IF piece = PAWN THEN symbol = "P"
    ELSE
        IF piece = PAWN THEN symbol = "p"
    END IF

    GetSymbol = symbol
END FUNCTION

REM Calling GetSymbol(WHITE, PAWN) returns "p" instead of "P"
REM Calling GetSymbol(BLACK, PAWN) returns "" instead of "p"
```

**Expected**:
- `GetSymbol(WHITE, PAWN)` should return "P"
- `GetSymbol(BLACK, PAWN)` should return "p"

**Actual**:
- `GetSymbol(WHITE, PAWN)` returns "p" (ELSE branch executed instead of THEN)
- `GetSymbol(BLACK, PAWN)` returns "" (THEN branch executed but nested IF doesn't match)

**Test Files**: `/tmp/test_piece_symbol.bas`, `/tmp/test_if_condition.bas`, `/tmp/test_compound.bas`

**Impact**: HIGH - Affects program correctness in any FUNCTION using nested IF statements.

**Workaround**: Use flat compound conditions with AND instead of nesting:
```basic
FUNCTION GetSymbol(color AS INTEGER, piece AS INTEGER) AS STRING
    DIM symbol AS STRING
    IF color = WHITE AND piece = PAWN THEN symbol = "P"
    IF color = BLACK AND piece = PAWN THEN symbol = "p"
    GetSymbol = symbol
END FUNCTION
```

**Notes**:
- IF statements work correctly in SUBs and at module level
- Compound conditions with AND/OR work correctly as workaround
- SELECT CASE cannot be used as workaround (doesn't support CONST identifiers in labels)

**ROOT CAUSE** (Identified 2025-11-18):

The bug is **specifically with single-line IF statements (without END IF) nested inside IF/ELSE blocks in FUNCTIONs**. Investigation revealed:

1. **Pattern that triggers the bug**:
   ```basic
   IF outer_condition THEN
       IF inner1 THEN statement1    REM Single-line IF
       IF inner2 THEN statement2    REM Single-line IF
   ELSE
       IF inner3 THEN statement3    REM Single-line IF
   END IF
   ```

2. **Pattern that works correctly**:
   ```basic
   IF outer_condition THEN
       IF inner1 THEN           REM Block IF with END IF
           statement1
       END IF
   ELSE
       IF inner3 THEN
           statement3
       END IF
   END IF
   ```

3. **Test Results**:
   - Single-level IFs in FUNCTIONs: ✓ Work correctly
   - Block-form nested IFs in FUNCTIONs: ✓ Work correctly
   - Single-line nested IFs in FUNCTIONs: ✗ BROKEN (outer condition inverted)
   - All IF forms in SUBs: ✓ Work correctly
   - All IF forms at module level: ✓ Work correctly

4. **Specific Behavior**: When the outer IF condition evaluates to TRUE, the ELSE branch is executed. When it evaluates to FALSE, the THEN branch is executed (but nested single-line IFs don't match, leaving variables unassigned).

**Location**: Parser bug in `src/frontends/basic/Parser_Stmt_If.cpp:184`

**FIX** (Implemented 2025-11-18):

The bug was in the parser, not the lowerer. The `parseElseChain` function (which handles single-line IF statements) was calling `skipOptionalLineLabelAfterBreak` to look for ELSE/ELSEIF keywords. This function **skips line breaks**, allowing single-line IFs to incorrectly consume ELSE keywords from the next line when nested inside multi-line IF blocks.

**Example of the bug**:
```basic
IF 1 = 1 THEN
    IF 2 = 2 THEN PRINT "A"
ELSE               REM This ELSE should belong to outer IF
    PRINT "B"
END IF
```

Before the fix, the parser produced this AST:
```
IF (1 = 1) THEN
  (IF (2 = 2) THEN PRINT "A" ELSE PRINT "B")  ← ELSE wrongly attached to inner IF!
```

After the fix, the parser produces the correct AST:
```
IF (1 = 1) THEN
  (IF (2 = 2) THEN PRINT "A")
ELSE
  PRINT "B"
```

**Changed**: `src/frontends/basic/Parser_Stmt_If.cpp:184`
- **Before**: Called `skipOptionalLineLabelAfterBreak` which skips line breaks looking for ELSE
- **After**: Removed the line break skipping; single-line IFs now only consume ELSE on the same line

**Testing**: All 276 BASIC tests pass with the fix. The parser now correctly distinguishes between:
- Single-line IFs with ELSE on same line: `IF cond THEN stmt1 ELSE stmt2`
- Single-line IFs nested in multi-line blocks: ELSE belongs to the outer block

**Files Changed**: `src/frontends/basic/Parser_Stmt_If.cpp`
**Test Files**: `/tmp/test_parser_else_bug.bas`, `/tmp/test_single_line_if_func.bas`

### BUG-093: INPUT Statement Treats STRING Variables as Numbers
**Status**: ✅ **RESOLVED** - Fixed 2025-11-18
**Discovered**: 2025-11-18 (Chess game development - interactive input attempt)
**Category**: I/O / Type System / Variables
**Severity**: HIGH - Blocks interactive string input in programs

**Symptom**: When using INPUT to read into a STRING variable, the variable is subsequently treated as a numeric type by the compiler, causing type mismatch errors when passing to string functions.

**Minimal Reproduction**:
```basic
DIM move AS STRING
PRINT "Enter move: ";
INPUT move
REM This causes error: "LEFT$: arg 1 must be string (got number)"
PRINT LEFT$(move, 1)
```

**Expected**: Variable `move` should remain type STRING after INPUT

**Actual**: After INPUT, the variable is treated as a numeric type

**Test Files**: `/tmp/test_input_string.bas`, `/tmp/chess_v2_moves.bas`

**Impact**: HIGH - Makes it impossible to create interactive programs that read string input from users.

**Workaround**: None currently. Must use hardcoded moves or file-based input instead.

**ROOT CAUSE** (Identified 2025-11-18):

The bug is in the semantic analyzer's `resolveAndTrackSymbol` function in `src/frontends/basic/SemanticAnalyzer.cpp` at lines 149-169.

**The Problem**:
When INPUT is analyzed, it calls `resolveAndTrackSymbol(name, SymbolKind::InputTarget)`. This function has logic that **unconditionally resets the variable's type to the default type** when `kind == SymbolKind::InputTarget`:

```cpp
const bool forceDefault = kind == SymbolKind::InputTarget;  // Line 149
auto itType = varTypes_.find(name);
if (forceDefault || itType == varTypes_.end())
{
    Type defaultType = Type::Int;  // Line 153
    if (!name.empty())
    {
        if (name.back() == '$')
            defaultType = Type::String;
        else if (name.back() == '#' || name.back() == '!')
            defaultType = Type::Float;
    }
    // ...
    varTypes_[name] = defaultType;  // Line 168 - OVERWRITES existing type!
}
```

**Execution Flow for `DIM move AS STRING; INPUT move`**:
1. `DIM move AS STRING` sets `varTypes_["move"] = Type::String` ✓
2. `INPUT move` calls `resolveAndTrackSymbol("move", SymbolKind::InputTarget)`
3. `forceDefault = true` because kind is InputTarget (line 149)
4. Since "move" doesn't end with $, default type is `Type::Int` (line 153)
5. **BUG**: `varTypes_["move"] = Type::Int` (line 168) - **overwrites the STRING type!**
6. All subsequent uses of `move` now treat it as INTEGER

**FIX** (Implemented 2025-11-18):

The fix was simple: remove the `forceDefault` logic that unconditionally overwrites variable types when processing INPUT statements.

**Changed**: `src/frontends/basic/SemanticAnalyzer.cpp:149-152`

**Before**:
```cpp
const bool forceDefault = kind == SymbolKind::InputTarget;
auto itType = varTypes_.find(name);
if (forceDefault || itType == varTypes_.end())
{
    // Sets default type, overwriting DIM types!
    varTypes_[name] = defaultType;
}
```

**After**:
```cpp
// BUG-093 fix: Do not override explicitly declared types (from DIM) when processing
// INPUT statements. Only set default type if the variable has no type yet.
auto itType = varTypes_.find(name);
if (itType == varTypes_.end())
{
    // Only sets default type for undeclared variables
    varTypes_[name] = defaultType;
}
```

**Result**:
- Variables declared with `DIM name AS STRING` now retain their STRING type through INPUT
- Undeclared variables (e.g., `INPUT name$`) still get suffix-based default types
- All 276 BASIC tests pass

**Testing**:
- ✅ `DIM move AS STRING; INPUT move; PRINT LEFT$(move, 1)` - Works correctly
- ✅ `INPUT name$` (undeclared) - Still uses suffix-based STRING type
- ✅ `INPUT age` (undeclared) - Still uses default INTEGER type
- ✅ Mixed declared/undeclared variables - All work correctly

**Files Changed**: `src/frontends/basic/SemanticAnalyzer.cpp`
**Test Files**: `/tmp/test_input_string.bas`, `/tmp/test_input_undeclared.bas`, `/tmp/test_input_mixed.bas`

---

## RECENTLY RESOLVED BUGS

### BUG-091: Compiler Crash with 2D Array Access in Expressions
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: Arrays / Expression Analysis / Compiler Crash
**Severity**: CRITICAL - Compiler crash, blocks multi-dimensional array usage

**Symptom**: Accessing 2D (or higher) arrays in expressions causes compiler crash with assertion failure in expression type scanner.

**Error Message**:
```
Assertion failed: (!stack_.empty()), function pop, file Scan_ExprTypes.cpp, line 108
```

**Minimal Reproduction**:
```basic
DIM board(8, 8) AS INTEGER
board(1, 1) = 5

DIM x AS INTEGER
x = board(1, 1) + 10  REM CRASH!
```

**ROOT CAUSE**: In `Scan_ExprTypes.cpp`, the `after(const ArrayExpr &expr)` method only handled the deprecated single-index field (`expr.index`), not the multi-dimensional indices vector (`expr.indices`). For 2D arrays with two indices, only one value was popped from the expression type stack, leaving the stack imbalanced.

**FIX**: Updated `after(const ArrayExpr &expr)` to loop through all indices in `expr.indices` and pop each one:
```cpp
void after(const ArrayExpr &expr)
{
    if (expr.index != nullptr)
    {
        (void)pop();  // Legacy single index
    }
    else if (!expr.indices.empty())
    {
        // Pop all indices for multi-dimensional arrays
        for (size_t i = 0; i < expr.indices.size(); ++i)
        {
            (void)pop();
        }
    }
    push(ExprType::I64);
}
```

**Files Modified**: `src/frontends/basic/lower/Scan_ExprTypes.cpp`

### BUG-090: Cannot Pass Object Array Field as Parameter (Runtime Crash)
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17 (resolved together with BUG-089)
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: OOP / Runtime / Arrays / Parameters
**Severity**: CRITICAL - Runtime crash

**Symptom**: Passing an object array that is a class field as a parameter causes runtime crash with assertion failure.

**Error Message**:
```
Assertion failed: (hdr->elem_kind == RT_ELEM_NONE), function rt_arr_obj_assert_header, file rt_array_obj.c, line 34.
```

**Minimal Reproduction**:
```basic
CLASS Item
    val AS INTEGER
END CLASS

SUB InitArray(items() AS Item)
    items(1) = NEW Item()
END SUB

CLASS Container
    items(3) AS Item
    SUB InitItems()
        InitArray(items)  REM CRASH!
    END SUB
END CLASS
```

**ROOT CAUSE**: Constructor generation in `Lower_OOP_Emit.cpp` allocated object array fields using `rt_arr_i32_new` (which sets `elem_kind = RT_ELEM_I32`) instead of `rt_arr_obj_new` (which sets `elem_kind = RT_ELEM_NONE`).

**FIX**: Added object array detection in constructor generation:
```cpp
else if (!field.objectClassName.empty())
{
    requireArrayObjNew();
    handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_obj_new", {length});
}
```

**Files Modified**: `src/frontends/basic/Lower_OOP_Emit.cpp`

### BUG-089: Cannot Call Methods on Object Array Fields from Class Methods
**Status**: ✅ **RESOLVED** - Fixed 2025-11-17
**Discovered**: 2025-11-17 (Chess Engine v2 stress test)
**Category**: OOP / Arrays / Method Calls / Code Generation
**Severity**: CRITICAL - Unknown callee error, blocks object-oriented programming

**Symptom**: Attempting to call a method on an object array element that is a class field results in "unknown callee" compiler error.

**Error Message**:
```
error: unknown callee @SETVAL
```

**Minimal Reproduction**:
```basic
CLASS Item
    SUB SetVal(v AS INTEGER)
        val = v
    END SUB
    val AS INTEGER
END CLASS

CLASS Container
    items(10) AS Item

    SUB InitItem(idx AS INTEGER)
        items(idx).SetVal(42)  REM ERROR: unknown callee @SETVAL
    END SUB
END CLASS
```

**ROOT CAUSE**: Multiple code paths didn't properly detect and handle object arrays in class fields:
1. Array access expressions didn't track if they were member object arrays
2. Method call lowering didn't recognize object array elements as objects
3. Object class resolution didn't handle array expressions for implicit fields

**FIX**: Enhanced 5 different code paths to detect and handle object arrays:
- `Lower_OOP_Emit.cpp`: Constructor generation using `rt_arr_obj_new`
- `Emit_Expr.cpp`: Track `isMemberObjectArray` for array access
- `LowerStmt_Runtime.cpp`: Handle object arrays in assignments and calls
- `Lower_OOP_Expr.cpp`: Resolve object class for array expressions
- `Lowerer_Expr.cpp`: Detect implicit field arrays

**Key Insight**: In BASIC, `items(i)` is parsed as `CallExpr`, not `ArrayExpr`.

**Files Modified**:
- `src/frontends/basic/Lower_OOP_Emit.cpp`
- `src/frontends/basic/lower/Emit_Expr.cpp`
- `src/frontends/basic/lower/LowerStmt_Runtime.cpp`
- `src/frontends/basic/Lower_OOP_Expr.cpp`
- `src/frontends/basic/lower/Lowerer_Expr.cpp`

---

## OLDER RESOLVED BUGS (from Chess Engine v2 Stress Test)

- ✅ **BUG-086**: Array Parameters Not Supported in SUBs/FUNCTIONs - RESOLVED 2025-11-17
- ✅ **BUG-087**: Nested IF Statements Inside SELECT CASE Causes IL Errors - RESOLVED 2025-11-17
- ✅ **BUG-083**: Cannot Call Methods on Object Array Elements - RESOLVED 2025-11-17
- ✅ **BUG-075**: Arrays of Custom Class Types Use Wrong Runtime Function - RESOLVED 2025-11-17
- ✅ **BUG-076**: Object Assignment in SELECT CASE Causes Type Mismatch - RESOLVED 2025-11-17
- ✅ **BUG-078**: FOR Loop Variable Stuck at Zero When Global Variable Used in SUB - RESOLVED 2025-11-17
- ✅ **BUG-079**: Global String Arrays Cannot Be Assigned From SUBs - RESOLVED 2025-11-17
- ✅ **BUG-080**: INPUT Statement Only Works with INTEGER Type - RESOLVED 2025-11-17
- ✅ **BUG-082**: Cannot Call Methods on Nested Object Members - RESOLVED 2025-11-17
- ✅ **BUG-084**: String FUNCTION Methods Completely Broken - RESOLVED 2025-11-17
- ✅ **BUG-085**: Object Array Access in ANY Loop Causes Code Generation Errors - RESOLVED 2025-11-17
- ✅ **BUG-077**: Cannot Use Object Member Variables in Expressions - RESOLVED 2025-11-17

---

## DESIGN DECISIONS (Not Bugs)

### BUG-095: FOR Loop Variables After Loop Exit
**Status**: ⚠️  **DESIGN DECISION** - Not a bug
**Category**: Language Semantics / FOR Loops
**Resolution**: FOR loop variables are set to one value past the end condition when the loop exits. This matches QBasic/VB behavior and is intentional. See full documentation above in the OUTSTANDING BUGS section.

### BUG-088: COLOR Keyword Collision with Class Field Names
**Status**: ⚠️  **DESIGN DECISION** - Not a bug
**Category**: Keywords / Naming
**Resolution**: Reserved keywords cannot be used as identifiers. This is intentional language design.

### BUG-054: EXIT DO statement not supported
**Status**: ⚠️  **DESIGN DECISION** - Use EXIT WHILE/EXIT FOR instead

### BUG-055: Boolean expressions require extra parentheses
**Status**: ⚠️  **DESIGN DECISION** - Intentional for clarity

---

## RESOLVED BUGS (Previously Documented)

### Recently Resolved (2025-11-17)
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17
- ✅ **BUG-081**: FOR loop with object member variables - RESOLVED 2025-11-17

### All Previously Resolved Bugs (BUG-001 through BUG-066)
- ✅ **BUG-001**: String concatenation requires $ suffix - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
- ✅ **BUG-007**: Multi-dimensional arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-008**: REDIM PRESERVE syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-009**: CONST keyword not implemented - RESOLVED 2025-11-12
- ✅ **BUG-010**: STATIC keyword not implemented - RESOLVED 2025-11-14
- ✅ **BUG-011**: SWAP statement not implemented - RESOLVED 2025-11-12
- ✅ **BUG-012**: BOOLEAN type incompatibility with TRUE/FALSE - RESOLVED 2025-11-14
- ✅ **BUG-013**: SHARED keyword not supported - RESOLVED 2025-11-13
- ✅ **BUG-014**: String arrays not supported (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-015**: String properties in classes cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-016**: Local string variables in methods cause error - RESOLVED 2025-11-13
- ✅ **BUG-017**: Accessing global strings from methods causes segfault - RESOLVED 2025-11-14
- ✅ **BUG-018**: FUNCTION methods in classes cause code generation error - RESOLVED 2025-11-12
- ✅ **BUG-019**: Float literals assigned to CONST truncated to integers - RESOLVED 2025-11-14
- ✅ **BUG-020**: String constants cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-021**: SELECT CASE doesn't support negative integer literals - RESOLVED 2025-11-12
- ✅ **BUG-022**: Float literals without explicit type default to INTEGER - RESOLVED 2025-11-12
- ✅ **BUG-023**: DIM with initializer not supported - RESOLVED 2025-11-12
- ✅ **BUG-024**: CONST with type suffix causes assertion failure - RESOLVED 2025-11-12
- ✅ **BUG-025**: EXP of large values causes overflow trap - RESOLVED 2025-11-13
- ✅ **BUG-026**: DO WHILE loops with GOSUB cause empty block error - RESOLVED 2025-11-13
- ✅ **BUG-027**: MOD operator doesn't work with INTEGER type - RESOLVED 2025-11-12
- ✅ **BUG-028**: Integer division operator doesn't work with INTEGER type - RESOLVED 2025-11-12
- ✅ **BUG-029**: EXIT FUNCTION and EXIT SUB not supported - RESOLVED 2025-11-12
- ✅ **BUG-030**: SUBs and FUNCTIONs cannot access global variables - RESOLVED 2025-11-14
- ✅ **BUG-031**: String comparison operators not supported - RESOLVED 2025-11-12
- ✅ **BUG-032**: String arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-033**: String array assignment causes type mismatch (duplicate) - RESOLVED 2025-11-13
- ✅ **BUG-034**: MID$ does not convert float arguments to integer - RESOLVED 2025-11-13
- ✅ **BUG-035**: Global variables not accessible in SUB/FUNCTION (duplicate) - RESOLVED 2025-11-14
- ✅ **BUG-036**: String comparison in OR condition causes IL error - RESOLVED 2025-11-13
- ✅ **BUG-037**: SUB methods on class instances cannot be called - RESOLVED 2025-11-15
- ✅ **BUG-038**: String concatenation with method results fails - RESOLVED 2025-11-14
- ✅ **BUG-039**: Cannot assign method call results to variables - RESOLVED 2025-11-15
- ✅ **BUG-040**: Cannot use custom class types as function return types - RESOLVED 2025-11-15
- ✅ **BUG-041**: Cannot create arrays of custom class types - RESOLVED 2025-11-14
- ✅ **BUG-042**: Reserved keyword 'LINE' cannot be used as variable name - RESOLVED 2025-11-14
- ✅ **BUG-043**: String arrays not working (duplicate) - RESOLVED 2025-11-13
- ✅ **BUG-044**: CHR() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-045**: STRING arrays not working with AS STRING syntax - RESOLVED 2025-11-15
- ✅ **BUG-046**: Cannot call methods on array elements - RESOLVED 2025-11-15
- ✅ **BUG-047**: IF/THEN/END IF inside class methods causes crash - RESOLVED 2025-11-15
- ✅ **BUG-048**: Cannot call module-level SUB/FUNCTION from class methods - RESOLVED 2025-11-15
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL error - NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-052**: ON ERROR GOTO handler blocks missing terminators - RESOLVED 2025-11-15
- ✅ **BUG-053**: Cannot access global arrays in SUB/FUNCTION - RESOLVED 2025-11-15
- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-057**: BOOLEAN return type in class methods causes type mismatch - RESOLVED 2025-11-15
- ✅ **BUG-058**: String array fields don't retain values - RESOLVED 2025-11-15
- ✅ **BUG-059**: Cannot access array fields within class methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class objects passed as parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot assign class field value to local variable - RESOLVED 2025-11-15
- ✅ **BUG-062**: CONST with CHR$() not evaluated at compile time - RESOLVED 2025-11-15
- ✅ **BUG-063**: Module-level initialization cleanup code leaks - RESOLVED 2025-11-15
- ✅ **BUG-064**: ASC() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-065**: Array field assignments silently dropped - RESOLVED 2025-11-15
- ✅ **BUG-066**: VAL() function not implemented - RESOLVED 2025-11-15

---

## ADDITIONAL INFORMATION

**For detailed bug descriptions and test cases:**
- See `/bugs/bug_testing/` directory for test cases and stress tests
- Recent bugs (089-091) documented above with full details
- Older bugs condensed to preserve space while maintaining history

**Testing Sources:**
- Chess Engine v2 stress test (discovered 12 critical bugs)
- Language audit and systematic feature testing
- Real-world application development (chess, baseball games)
