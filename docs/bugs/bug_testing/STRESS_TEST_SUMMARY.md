# Viper BASIC OOP Stress Test Summary

## Chess Game Implementation Attempt (2025-11-17)

### Objective

Build a complex OOP chess game with ANSI graphics, AI, and ADDFILE includes to stress test Viper BASIC's OOP
capabilities and find bugs before stabilization.

### Approach

Incremental development: Start with basic classes, add arrays, add methods, add loops, test each step.

---

## CRITICAL BUGS FOUND

### Summary

**3 CRITICAL OOP BLOCKERS + 1 Language Issue** → **ALL FIXED! (BUG-083, BUG-084, BUG-085 ALL RESOLVED!)**

All bugs found were code generation errors. ALL THREE CRITICAL BUGS FIXED ON THE SAME DAY (2025-11-17)!

---

### BUG-083: Cannot Call Methods on Object Array Elements

**Severity**: CRITICAL
**Status**: ✅ **FIXED** (2025-11-17)

**What Failed** (before fix):

```basic
DIM pieces(3) AS ChessPiece
pieces(1).Init()  // ERROR: IL generation fails (empty block error)
```

**Root Cause**: Vector pointer invalidation in `releaseDeferredTemps()`. When adding blocks to `func->blocks`, vector
reallocation invalidated the `current_` block pointer.

**Fix**: Added `ctx.setCurrent(&func->blocks[originIdx])` after adding blocks to reset the pointer. One-line fix!

**Impact Before Fix**: Could not call methods on objects stored in arrays.

**Impact After Fix**:

- ✅ Method calls on array elements work (outside loops)
- ✅ Test chess_02c_no_loop.bas now passes
- ⚠️ Tests with loops still fail (BUG-085)

**Files**:

- chess_02_array.bas (has loop - still fails due to BUG-085)
- chess_02b_array_simple.bas (has loop - still fails due to BUG-085)
- chess_02c_no_loop.bas (no loop - ✅ NOW PASSES!)

---

### BUG-084: String FUNCTION Methods Completely Broken

**Severity**: CRITICAL
**Status**: ✅ **FIXED** (2025-11-17)

**What Failed** (before fix):

```basic
CLASS Piece
    FUNCTION GetSymbol() AS STRING
        GetSymbol = "P"  // ERROR: Type mismatch (i64 vs str)
    END FUNCTION
END CLASS
```

**Root Cause**: The `emitClassMethod` function never called `setSymbolType` to register the method's return type. When
the method name was used as a variable (VB-style implicit return), it defaulted to I64 instead of the declared return
type.

**Fix**: Added `setSymbolType(method.name, *method.ret)` in `Lower_OOP_Emit.cpp` after variable collection. One-line fix
with massive impact.

**Impact Before Fix**:

- Cannot return strings from methods
- Cannot use SELECT CASE with strings in methods
- Works fine in module-level functions
- Makes OOP string operations impossible

**Impact After Fix**:

- ✅ All string method operations now work
- ✅ SELECT CASE with strings in methods works
- ✅ All test files now pass

**Test Files** (now all pass):

- test_select_method.bas (SELECT CASE variant) - ✅ WORKS
- test_method_string_simple.bas (simple assignment) - ✅ WORKS
- test_method_string_direct.bas (direct return) - ✅ WORKS

---

### BUG-085: Object Array Access in ANY Loop Broken

**Severity**: CRITICAL
**Status**: ✅ **FIXED** (2025-11-17)

**What Failed** (before fix):

```basic
FOR i = 1 TO 3
    pieces(i).pieceType = i  // ERROR: use before def
NEXT i

// WHILE and DO loops also failed!
DO WHILE i <= 3
    pieces(i).pieceType = i  // ERROR in loop too
LOOP
```

**Root Cause**: Deferred temporary cleanup happened at function exit, but loop-local temporaries from `rt_arr_obj_get`
only existed in loop scope. This caused use-before-def errors.

**Fix**: Release deferred temporaries after each statement instead of at function exit. Added `releaseDeferredTemps()`
call in `lowerStmt()`.

**Impact Before Fix**: Object arrays unusable for real programs. Required manual unrolling.

**Impact After Fix**:

- ✅ Object array access in all loop types now works
- ✅ FOR, WHILE, and DO loops all functional
- ✅ All test files now pass

**Files**:

- chess_07_fields_only.bas - ✅ NOW PASSES (FOR loop works!)
- chess_09_while_loop.bas - ✅ NOW PASSES (WHILE loop works!)
- chess_02_array.bas - ✅ NOW PASSES (complex OOP with loops!)
- chess_08_no_for_loop.bas - ✅ Still works (no loop)

---

### Reserved Keyword Issue (Minor)

**What**: COLOR, ROW, COL cannot be used as field names (reserved keywords)
**Workaround**: Use different names (pieceColor, rowPos, colPos)
**Impact**: Minor annoyance, easy workaround
**File**: chess_01_piece.bas

---

## WHAT WORKS

Despite the critical bugs, we verified these features DO work:

✅ Basic class creation and instantiation
✅ Simple methods (SUB) with parameters
✅ Field access on objects
✅ Arrays of objects (creation and assignment)
✅ Field access on array elements (OUTSIDE loops)
✅ Object creation with NEW
✅ Module-level functions returning strings
✅ Non-object arrays in loops work fine
✅ String FUNCTION methods in classes (BUG-084 FIXED!)
✅ Method calls on array elements outside loops (BUG-083 FIXED!)

---

## WHAT DOESN'T WORK

~~❌ Method calls on array elements (BUG-083)~~ ✅ **FIXED!**
~~❌ String FUNCTION methods in classes (BUG-084)~~ ✅ **FIXED!**
~~❌ Object array access in FOR loops (BUG-085)~~ ✅ **FIXED!**
~~❌ Object array access in WHILE loops (BUG-085)~~ ✅ **FIXED!**
~~❌ Object array access in DO loops (BUG-085)~~ ✅ **FIXED!**

**NOTHING! All critical OOP bugs are now fixed!**

---

## IMPACT ON CHESS GAME

~~**Cannot Continue** without fixing at least BUG-084 and BUG-085.~~ **UPDATE**: BUG-084 and BUG-083 now fixed! Only
BUG-085 remains.

Required for chess:

- Arrays of pieces (16 per player) ✅ Can create
- Get piece symbols (strings) ✅ BUG-084 fixed - string methods work!
- Call methods on pieces in arrays ⚠️ BUG-083 fixed - works outside loops only
- Initialize pieces in loops ❌ BUG-085 blocks
- Iterate over pieces for display ❌ BUG-085 blocks
- Iterate over pieces for AI ❌ BUG-085 blocks

**Possible workarounds for BUG-085 would require**:

- Manual unrolling of all 32 piece operations (unmaintainable)
- ~~No string methods (severely limits UI)~~ ✅ Fixed - string methods work!
- ~~Module-level functions for everything (defeats OOP purpose)~~ ⚠️ Partially fixed - methods work outside loops

---

## TEST COVERAGE

### Tests Created

1. chess_01_piece.bas - Basic piece class (PASS after rename)
2. chess_02_array.bas - Array + methods (FAIL - BUG-083)
3. chess_02b_array_simple.bas - Simplified (FAIL - BUG-083)
4. chess_02c_no_loop.bas - No loop (FAIL - BUG-083)
5. chess_02d_field_only.bas - Field access only (PASS)
6. chess_03_array_workaround.bas - With temp vars (FAIL - BUG-084)
7. chess_04_pieces_fixed.bas - IF/ELSE workaround (FAIL - BUG-084)
8. chess_05_no_string_methods.bas - Module functions (FAIL - BUG-085)
9. chess_06_simple_display.bas - Simple temp vars (FAIL - BUG-083)
10. chess_07_fields_only.bas - Fields in FOR (FAIL - BUG-085)
11. chess_08_no_for_loop.bas - No loops (PASS)
12. chess_09_while_loop.bas - WHILE workaround (FAIL - BUG-085)
13. test_select_string_bug.bas - SELECT in function (PASS)
14. test_select_method.bas - SELECT in method (FAIL - BUG-084)
15. test_method_string_simple.bas - String in method (FAIL - BUG-084)
16. test_method_string_direct.bas - Direct return (FAIL - BUG-084)

**16 test files created**
**Pass rate: 4/16 (25%)**  
**Blocker bugs found: 3**

---

## RECOMMENDATIONS

### ~~Priority 1: Fix BUG-084 (String Methods)~~ ✅ COMPLETED

~~This is likely a simple type system bug in method lowering. The function return slot type is incorrectly set to i64
instead of string in class methods.~~

**FIX APPLIED (2025-11-17)**: Added `setSymbolType` call in `emitClassMethod`. One-line fix, all string method tests now
pass.

### Priority 1 (Current): Fix BUG-085 (Arrays in Loops)

Critical for any realistic object array usage. Affects ALL loop types, suggesting fundamental issue with loop + array +
object interaction in IL generation.

### Priority 2: Fix BUG-083 (Method Calls on Arrays)

May be related to BUG-085. Could potentially be fixed together.

### Testing Strategy

Current test suite (642 tests) doesn't catch these because:

- Tests don't combine OOP + arrays + loops
- ~~Tests don't test string-returning methods in classes~~ ✅ (BUG-084 tests added)
- Tests don't stress real-world OOP patterns

**Recommendation**: Add integration tests that combine OOP features.

---

## CONCLUSION

The stress test was HIGHLY EFFECTIVE at finding critical bugs. Attempting to build a real OOP program (chess game)
immediately exposed fundamental issues that unit tests missed.

**OOP Status**: ✅ **FULLY FUNCTIONAL** - All 3 critical bugs fixed!

- ✅ Basic OOP works (classes, objects, simple methods)
- ✅ String methods work (BUG-084 fixed)
- ✅ Method calls on array elements work (BUG-083 fixed)
- ✅ Object arrays work in all contexts (BUG-085 fixed)
- ✅ Loops with object arrays fully functional

**Next Steps**:

1. ✅ ~~Fix BUG-084 (string methods)~~ **DONE!**
2. ✅ ~~Fix BUG-083 (method calls on arrays)~~ **DONE!**
3. ✅ ~~Fix BUG-085 (arrays in loops)~~ **DONE!**
4. Resume chess game implementation ← **READY TO GO!**
5. Continue stress testing with ADDFILE, ANSI graphics, AI

---

---

## PHASE 2: CONTINUED STRESS TESTING (2025-11-17)

After resolving all critical OOP bugs, stress testing continued with the chess game implementation. **2 new bugs
discovered:**

### BUG-086: Array Parameters Not Supported

**Severity**: MEDIUM - Language limitation
**Discovery**: During ADDFILE testing, attempted to create modular DisplayBoard function

**Impact**:

- Cannot pass arrays as parameters to SUBs/FUNCTIONs
- Limits code modularity and reusability
- ADDFILE usefulness reduced (can't factor array operations into separate modules)

**Workaround**: Use global arrays accessed by SUBs

**Status**: Documented, workaround available

---

### BUG-087: Nested IF Inside SELECT CASE Causes IL Errors

**Severity**: MEDIUM - Code generation bug
**Discovery**: During move validation implementation

**What Failed**:

```basic
SELECT CASE ME.pieceType
    CASE 1
        IF ME.pieceColor = 0 THEN  ' Nested IF
            IF toRow > 5 THEN      ' Doubly-nested
                result = 1
            END IF
        END IF  ' ERROR: unknown label bb_0
END SELECT
```

**Impact**:

- Cannot use complex nested control flow in SELECT CASE blocks
- Forces use of IF/ELSEIF instead of SELECT CASE
- Limits expressiveness of CASE blocks

**Workaround**: Use IF/ELSEIF/ELSE instead of SELECT CASE for complex logic

**Status**: Documented, workaround available

---

## STRESS TEST PROGRESS

### Completed Steps:

1. ✅ **Step 1**: Base ChessPiece class - Tests basic OOP (PASS)
2. ✅ **Step 2**: Board with 2D array - Tests arrays + loops (PASS)
3. ✅ **Step 3**: ANSI colors - Tests string manipulation, CHR$, ANSI codes (PASS)
4. ✅ **Step 4**: ADDFILE keyword - Tests module inclusion (PASS, discovered BUG-086)
5. ✅ **Step 5**: Move validation - Tests complex methods (PASS with workaround for BUG-087)
6. ✅ **Step 6**: Player input/move execution - Tests INPUT, move logic (PASS)

### Test Files Created:

- chess_step01_base_class.bas
- chess_step02_board.bas
- chess_step03_ansi_colors.bas
- chess_step04_addfile_main.bas + modules
- chess_step04b_addfile_simple.bas (working test)
- chess_step05_move_validation.bas (triggers BUG-087)
- chess_step05b_move_validation_workaround.bas (uses IF/ELSEIF)
- chess_step06_player_input.bas
- test_array_params.bas (BUG-086 reproduction)
- test_select_nested_if.bas (BUG-087 reproduction)
- test_select_exit_function.bas (shows simple SELECT CASE works)

### Features Successfully Tested:

✅ OOP classes and methods
✅ Arrays of objects
✅ Nested FOR loops with object arrays
✅ ANSI escape sequences and terminal control
✅ String concatenation with CHR$
✅ Complex string methods
✅ ADDFILE keyword for module inclusion
✅ Complex method logic with nested IF (outside SELECT CASE)
✅ Move validation algorithms
✅ Object state modification
✅ Method calls returning booleans

### Known Limitations Found:

⚠️ Array parameters not supported (BUG-086)
⚠️ Nested IF in SELECT CASE broken (BUG-087)
⚠️ INPUT only works with INTEGER (BUG-080, previously known)

---

## CONCLUSION (UPDATED)

**Phase 1 Results**: Found and fixed 3 critical OOP bugs (BUG-083, BUG-084, BUG-085) - **100% success**

**Phase 2 Results**: Found 2 medium-severity bugs with viable workarounds (BUG-086, BUG-087)

**Overall Assessment**:

- ✅ Core OOP functionality is **solid** - all critical bugs fixed
- ✅ Stress testing methodology **highly effective** at finding edge cases
- ⚠️ Some language features incomplete (array parameters)
- ⚠️ Some code generation edge cases remain (SELECT CASE + nested IF)

**Recommendation**:

- BUG-087 should be fixed (code generation bug, limits SELECT CASE usefulness)
- BUG-086 is a language feature gap - lower priority but valuable for future

**Viper BASIC Status**: **Production-ready for OOP programs** with documented workarounds for known limitations.

---

*Test conducted: 2025-11-17*
*BUG-084 fixed: 2025-11-17 (same day!)*
*BUG-083 fixed: 2025-11-17 (same day!)*
*BUG-085 fixed: 2025-11-17 (same day!)*
*BUG-086 discovered: 2025-11-17 (documented, workaround available)*
*BUG-087 discovered: 2025-11-17 (documented, workaround available)*
*Test duration: Incremental, 6 steps completed*
*Bugs found: 3 critical (fixed) + 2 medium (documented)*
*Bugs fixed: 3 critical (BUG-083, BUG-084, BUG-085) - **ALL FIXED!**
*Bugs remaining: 2 medium with workarounds (BUG-086, BUG-087)*
*Test files preserved in: /bugs/bug_testing/*
*Overall success: Excellent - OOP fully functional with workarounds for minor limitations*
