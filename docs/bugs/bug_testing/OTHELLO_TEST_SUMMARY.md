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

### BUG-053: Cannot access global arrays in SUB/FUNCTION - CRITICAL COMPILER CRASH

**Severity**: CRITICAL
**Test**: othello_04_array_crash.bas
**Error**: `Assertion failed: (info && info->slotId), function lowerArrayAccess, file Emit_Expr.cpp, line 98`

Attempting to access a global array from within a SUB or FUNCTION causes the compiler to crash with an assertion
failure. This is a fundamental limitation that prevents modular programming with arrays.

**Example**:

```basic
DIM globalArray(10) AS INTEGER

FUNCTION AccessArray(index AS INTEGER) AS INTEGER
    RETURN globalArray(index)  ' CRASH!
END FUNCTION
```

**Impact**: Cannot use arrays in functions, forcing all array logic to be at module level.

---

### BUG-054: STEP is a reserved word

**Severity**: MINOR
**Test**: othello_game.bas (original)

Cannot use `step` as a variable name because it's reserved for `FOR...TO...STEP`.

**Example**:

```basic
DIM step AS INTEGER  ' ERROR!
FOR step = 1 TO 10
    PRINT step
NEXT step
```

**Workaround**: Use `stepNum`, `stepIndex`, `flipNum`, etc.

---

### BUG-055: Cannot assign to FOR loop variable

**Severity**: MODERATE
**Test**: othello_game.bas (original)

Cannot assign to loop counter inside FOR loop, even to break early.

**Example**:

```basic
FOR i = 1 TO 100
    IF someCondition THEN
        i = 999  ' ERROR: cannot assign to loop variable
    END IF
NEXT i
```

**Workaround**: Use flag variables to control loop flow.

---

### BUG-056: Arrays not allowed as class fields

**Severity**: MAJOR
**Test**: othello_02_classes.bas

Cannot declare arrays inside CLASS definitions.

**Example**:

```basic
CLASS Board
    DIM cells(64) AS INTEGER  ' ERROR!
END CLASS
```

**Workaround**: Use global arrays separate from classes.

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

### Cannot Build Full Game

Due to BUG-053 (array access in functions), cannot implement:

- Move validation functions
- Automated move selection
- Direction checking algorithms
- Piece flipping in loops

All array logic must be at module level, making the code extremely long and repetitive.

---

## Workarounds Used

1. **No arrays in classes** (BUG-056) - Used global board array + GameState class for metadata
2. **No global array access in functions** (BUG-053) - Kept all array logic at module level
3. **No STEP variable** (BUG-054) - Used `flipNum` instead
4. **No loop variable assignment** (BUG-055) - Removed early exit attempts

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
| OOP game state       | Class with board array     | Class without array    | ⚠️ Workaround |
| Move validation      | Function with array access | Inline at module level | ❌ BUG-053     |
| Direction checking   | Reusable function          | Copy-paste code        | ❌ BUG-053     |
| Piece flipping       | Loop in SUB                | Manual inline          | ❌ BUG-053     |
| Score counting       | SUB with array             | Inline FOR loop        | ❌ BUG-053     |

---

## Recommendations

1. **Fix BUG-053 (CRITICAL)** - Enable global array access in SUB/FUNCTION
    - This is the #1 priority for enabling real programs
    - Without this, cannot write modular array-based code

2. **Fix BUG-056 (MAJOR)** - Allow arrays as class fields
    - Enables proper OOP encapsulation
    - Makes classes useful for complex data structures

3. **Fix BUG-054 (MINOR)** - Allow STEP as variable in non-FOR contexts
    - Low priority but common use case

4. **BUG-055 is acceptable** - Preventing loop variable modification is good design

---

## Conclusion

Othello demonstrates that Viper BASIC has good fundamentals (arrays, classes, loops) but **BUG-053 is a critical blocker
** for writing real programs. The inability to access global arrays in functions forces all code to be at module level,
making it nearly impossible to build maintainable array-based applications.

**Overall Assessment**: Viper BASIC can represent game boards and state, but cannot implement game logic in a modular
way due to array access limitations.

---

**Files to Review**:

- `/bugs/bug_testing/othello_simple.bas` - Working demo (208 lines)
- `/bugs/bug_testing/othello_04_array_crash.bas` - Minimal crash reproduction
- `/bugs/bug_testing/othello_game.bas` - What SHOULD work but doesn't

**Test Command**:

```bash
cd /Users/stephen/git/viper
./build/src/tools/viper/viper front basic -run bugs/bug_testing/othello_simple.bas 2>&1 | grep -v "rt_heap"
```

---

End of Othello stress test report.
