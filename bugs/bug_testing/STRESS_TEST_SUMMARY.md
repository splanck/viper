# Viper BASIC OOP Stress Test Summary
## Chess Game Implementation Attempt (2025-11-17)

### Objective
Build a complex OOP chess game with ANSI graphics, AI, and ADDFILE includes to stress test Viper BASIC's OOP capabilities and find bugs before stabilization.

### Approach
Incremental development: Start with basic classes, add arrays, add methods, add loops, test each step.

---

## CRITICAL BUGS FOUND

### Summary
**3 CRITICAL OOP BLOCKERS + 1 Language Issue** → **1 REMAINING (BUG-083 & BUG-084 FIXED!)**

All bugs found were code generation errors. BUG-084 fixed (2025-11-17). BUG-083 fixed (2025-11-17 same day!).

---

### BUG-083: Cannot Call Methods on Object Array Elements
**Severity**: CRITICAL
**Status**: ✅ **FIXED** (2025-11-17)

**What Failed** (before fix):
```basic
DIM pieces(3) AS ChessPiece
pieces(1).Init()  // ERROR: IL generation fails (empty block error)
```

**Root Cause**: Vector pointer invalidation in `releaseDeferredTemps()`. When adding blocks to `func->blocks`, vector reallocation invalidated the `current_` block pointer.

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

**Root Cause**: The `emitClassMethod` function never called `setSymbolType` to register the method's return type. When the method name was used as a variable (VB-style implicit return), it defaulted to I64 instead of the declared return type.

**Fix**: Added `setSymbolType(method.name, *method.ret)` in `Lower_OOP_Emit.cpp` after variable collection. One-line fix with massive impact.

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
**Status**: Cannot iterate over object arrays with any loop type

**What Fails**:
```basic
FOR i = 1 TO 3
    pieces(i).pieceType = i  // ERROR in loop
NEXT i

// WHILE and DO loops also fail!
DO WHILE i <= 3
    pieces(i).pieceType = i  // ERROR in loop too
LOOP
```

**What Works**:
```basic
// Only manual unrolling works
pieces(1).pieceType = 1
pieces(2).pieceType = 2
pieces(3).pieceType = 3
```

**Impact**: Makes object arrays useless for real programs. Must manually unroll ALL operations.

**Files**:
- chess_07_fields_only.bas (FOR loop fails)
- chess_09_while_loop.bas (WHILE loop fails)
- chess_08_no_for_loop.bas (works without loop)

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
❌ Object array access in FOR loops (BUG-085)
❌ Object array access in WHILE loops (BUG-085)
❌ Object array access in DO loops (BUG-085)
❌ Temporary variable workaround in loops (BUG-085)

---

## IMPACT ON CHESS GAME

~~**Cannot Continue** without fixing at least BUG-084 and BUG-085.~~ **UPDATE**: BUG-084 and BUG-083 now fixed! Only BUG-085 remains.

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
~~This is likely a simple type system bug in method lowering. The function return slot type is incorrectly set to i64 instead of string in class methods.~~

**FIX APPLIED (2025-11-17)**: Added `setSymbolType` call in `emitClassMethod`. One-line fix, all string method tests now pass.

### Priority 1 (Current): Fix BUG-085 (Arrays in Loops)
Critical for any realistic object array usage. Affects ALL loop types, suggesting fundamental issue with loop + array + object interaction in IL generation.

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

The stress test was HIGHLY EFFECTIVE at finding critical bugs. Attempting to build a real OOP program (chess game) immediately exposed fundamental issues that unit tests missed.

**OOP Status**: MUCH IMPROVED - 2 of 3 critical bugs fixed!
- ✅ Basic OOP works (classes, objects, simple methods)
- ✅ String methods now work (BUG-084 fixed)
- ✅ Method calls on array elements work (BUG-083 fixed)
- ✅ Arrays of objects work (outside loops)
- ❌ Object arrays in loops still broken (BUG-085)

**Next Steps**:
1. ✅ ~~Fix BUG-084 (string methods)~~ **DONE!**
2. ✅ ~~Fix BUG-083 (method calls on arrays)~~ **DONE!**
3. Fix BUG-085 (arrays in loops) ← **NEXT PRIORITY**
4. Resume chess game implementation
5. Continue stress testing with ADDFILE, ANSI graphics, AI

---

*Test conducted: 2025-11-17*
*BUG-084 fixed: 2025-11-17 (same day!)*
*BUG-083 fixed: 2025-11-17 (same day!)*
*Test duration: Incremental, stopped at blockers*
*Bugs found: 3 critical + 1 minor*
*Bugs fixed: 2 critical (BUG-084, BUG-083)*
*Bugs remaining: 1 critical (BUG-085)*
*Test files preserved in: /bugs/bug_testing/*
