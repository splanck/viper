# Othello Game - OOP Stress Test Summary

**Date**: 2025-11-15
**Objective**: Build an OOP Othello/Reversi game to stress test arrays, game logic, and complex algorithms

---

## What Was Built

**Othello/Reversi** - Classic board game implementation with:

- 8x8 board representation using integer array
- OOP classes for game state and moves
- Move validation in all 8 directions
- Piece flipping mechanics
- Score tracking (black vs white)
- Automated move selection (simple AI)

---

## Test Files Created

### Incremental Tests

1. **othello_01_board.bas** - Board array and display ✅
2. **othello_02_classes.bas** - OOP classes with array fields ❌ (BUG-056)
3. **othello_02_classes_v2.bas** - OOP classes without arrays ✅
4. **othello_03_move_validation.bas** - Direction-based validation ✅
5. **othello_04_array_crash.bas** - Global array access test ❌ (BUG-053 CRASH)

### Complete Programs

1. **othello_game.bas** - Full game with functions ❌ (BUG-053, BUG-054, BUG-055)
2. **othello_simple.bas** - Working simplified version ✅ (208 lines)

---

## Bugs Discovered

### BUG-053: Cannot access global arrays in SUB/FUNCTION - RESOLVED (2025-11-15)

**Severity**: CRITICAL (historical)
**Test**: othello_04_array_crash.bas
**Error (historical)**: `Assertion failed: (info && info->slotId), function lowerArrayAccess, file Emit_Expr.cpp, line 98`

Global arrays can now be accessed from within SUB and FUNCTION procedures.

**Example (now works)**:

```basic
DIM globalArray(10) AS INTEGER

FUNCTION AccessArray(index AS INTEGER) AS INTEGER
    RETURN globalArray(index)  ' Now works
END FUNCTION
```

---

### BUG-054: STEP is a reserved word

**Severity**: MINOR
**Test**: othello_game.bas (original)
**Status**: Open — STEP remains a reserved keyword

Cannot use `step` as a variable name because it's reserved for `FOR...TO...STEP`.

**Workaround**: Use `stepNum`, `stepIndex`, `flipNum`, etc.

---

### BUG-055: Cannot assign to FOR loop variable

**Severity**: MODERATE
**Test**: othello_game.bas (original)
**Status**: Open — by design; assigning to a loop counter inside a FOR loop is not supported

**Workaround**: Use flag variables to control loop flow.

---

### BUG-056: Arrays not allowed as class fields - RESOLVED (2025-11-15)

**Severity**: MAJOR (historical)
**Test**: othello_02_classes.bas

Arrays can now be declared inside CLASS definitions.

**Example (now works)**:

```basic
CLASS Board
    DIM cells(64) AS INTEGER  ' Now works
END CLASS
```

---

## Features Successfully Tested

### Arrays ✅

- Integer arrays up to 64 elements
- Array initialization with FOR loops
- Array indexing with calculated indices (row * 8 + col)
- Array access at module level

### OOP Classes ✅

- Multiple class definitions
- INTEGER fields
- Object creation with NEW
- Field access and modification

### Game Logic ✅

- 8x8 board representation
- Starting position setup (4 center pieces)
- Coordinate system (row, col) to array index conversion
- Piece placement
- Manual piece flipping
- Score counting

### Control Flow ✅

- Nested FOR loops (board display)
- SELECT CASE for piece types
- IF/THEN at module level
- Complex conditionals

---

## What Works

The **othello_simple.bas** program successfully demonstrates:

```
================================================================
                    OTHELLO SIMPLE
================================================================

Initial position:

  0 1 2 3 4 5 6 7
0 . . . . . . . .
1 . . . . . . . .
2 . . . . . . . .
3 . . . W B . . .
4 . . . B W . . .
5 . . . . . . . .
6 . . . . . . . .
7 . . . . . . . .

Making test moves...

Move 1: Black plays at (2,3)

  0 1 2 3 4 5 6 7
0 . . . . . . . .
1 . . . . . . . .
2 . . . B . . . .
3 . . . B B . . .
4 . . . B W . . .
5 . . . . . . . .
6 . . . . . . . .
7 . . . . . . . .

Score: Black = 4, White = 1
```

---

## What Doesn't Work

BUG-053 (array access in functions) has been resolved. The Othello game can now be implemented with full modular design
using SUB/FUNCTION procedures that access global arrays.

---

## Workarounds Used (at time of test)

1. **No arrays in classes** (BUG-056) — RESOLVED; workaround used global board array + GameState class for metadata
2. **No global array access in functions** (BUG-053) — RESOLVED; workaround kept all array logic at module level
3. **No STEP variable** (BUG-054) — still reserved; workaround: use `flipNum` instead
4. **No loop variable assignment** (BUG-055) — still restricted; workaround: use flag variables

---

## Statistics

- **Test files created**: 7 (.bas files)
- **Total lines of code**: ~600 lines
- **Bugs discovered**: 4 (1 CRITICAL, 1 MAJOR, 2 MINOR)
- **Working programs**: 2 (othello_simple.bas, othello_03_move_validation.bas)

---

## Key Findings

### CRITICAL Issue: BUG-053

The inability to access global arrays from within SUB/FUNCTION procedures is a **critical limitation** that makes it
impossible to write clean, modular code for array-based algorithms.

**Impact on Othello**:

- Cannot create `IsValidMove(row, col)` function
- Cannot create `CheckDirection(row, col, dRow, dCol)` function
- Cannot create `MakeMove(row, col)` SUB
- Cannot create `CountPieces()` SUB with array access
- All logic must be inlined at module level

This turns what should be ~400 lines of clean, modular code into repetitive spaghetti code.

### Arrays vs OOP

Cannot combine arrays with OOP effectively:

- Arrays cannot be class fields (BUG-056)
- Functions cannot access global arrays (BUG-053)
- Must choose between "OOP with simple data" or "arrays with no functions"

---

## Comparison with Real Othello Implementation

| Feature              | Ideal Implementation       | Viper BASIC            | Status        |
|----------------------|----------------------------|------------------------|---------------|
| Board representation | 2D array or 1D array       | 1D array (global)      | ✅ Works       |
| OOP game state       | Class with board array     | Class with array       | ✅ BUG-056 fixed |
| Move validation      | Function with array access | Function works         | ✅ BUG-053 fixed |
| Direction checking   | Reusable function          | Reusable function      | ✅ BUG-053 fixed |
| Piece flipping       | Loop in SUB                | Loop in SUB            | ✅ BUG-053 fixed |
| Score counting       | SUB with array             | SUB with array         | ✅ BUG-053 fixed |

---

## Recommendations

1. **BUG-053** — RESOLVED. Global array access in SUB/FUNCTION now works.
2. **BUG-056** — RESOLVED. Arrays as class fields now supported.
3. **BUG-054** — Open (minor). STEP remains reserved; use alternate names.
4. **BUG-055** — Open (by design). Loop variable modification is intentionally restricted.

---

## Conclusion

Othello demonstrates that Viper BASIC has good fundamentals (arrays, classes, loops). The critical blocker identified
during this test (BUG-053: global array access in functions) has since been resolved, enabling fully modular
array-based game logic.

**Overall Assessment**: Viper BASIC can represent game boards, state, and implement game logic in a modular way.
The remaining limitations (BUG-054: STEP keyword reserved, BUG-055: loop variable assignment restricted) have viable
workarounds.

---

**Files to Review**:

- `docs/bugs/bug_testing/othello_04_array_crash.bas` - Minimal crash reproduction (historical)
- `docs/bugs/bug_testing/othello_game.bas` - Full game (BUG-053 blocking workarounds no longer needed)
- `docs/bugs/bug_testing/othello_simple.bas` - Working demo (208 lines)

**Test Command**:

```bash
# From viper repo root:
./build/src/tools/viper/viper front basic -run docs/bugs/bug_testing/othello_simple.bas
```

---

End of Othello stress test report.
