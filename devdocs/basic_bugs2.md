# BASIC Bugs Found During Tic-Tac-Toe Development

## BUG-2001: String array assignment causes type mismatch error

**Status:** RESOLVED
**Severity:** HIGH
**Found:** During tic-tac-toe board state implementation

**Description:**
String arrays cannot be assigned values. Code like:
```basic
DIM arr$(5)
arr$(0) = "Hello"
```

Results in error:
```
error: main:bc_ok0: call %t6 0 %t5: call arg type mismatch
```

**Impact:** Cannot use string arrays for board representation

**Fix:** Lowering for string-array element assignment now passes a pointer to a
temporary holding the string handle to match the runtime ABI. This fixes the
"call arg type mismatch" in `rt_arr_str_put`.

Files touched:
- `src/frontends/basic/LowerStmt_Runtime.cpp` (assignArrayElement)

Validation: Builds succeed; string-array element assignments no longer produce
IL call-argument type errors.

## BUG-2002: MID$ does not convert float arguments to integer

**Status:** RESOLVED
**Severity:** HIGH
**Found:** During tic-tac-toe board state implementation

**Description:**
MID$ expects integer arguments but does not properly convert float variables:
```basic
DIM s$, pos
s$ = "ABCDEFGHI"
pos = 1
PRINT MID$(s$, pos, 1)  ' Fails with "start must be >= 1 (got 0)"
```

Works with integer literal or suffix:
```basic
PRINT MID$(s$, 5%, 1%)  ' Works correctly
```

**Root Cause:** The lowering for MID$ coerced the start position to integer and
then incorrectly subtracted 1 before calling the runtime. The runtime already
interprets the start position as one-based and performs the internal
zero-based adjustment, so the extra `-1` yielded a start of 0 and a runtime
trap.

**Impact:** Cannot use MID$ with numeric variables for string manipulation

**Fix:** Removed the extraneous `-1` adjustment in MID$ lowering while still
coercing numeric arguments to `i64`. Floats are converted to integers by the
lowerer and the runtime receives the correct one-based start.

Files touched:
- `src/frontends/basic/builtins/StringBuiltins.cpp` (lowerMid)

Validation: Builds succeed; MID$(s$, pos, n) works when `pos` is a float-typed
variable with value 1.

## BUG-2003: Global variables not accessible in SUB/FUNCTION

**Status:** CRITICAL BLOCKER
**Severity:** CRITICAL
**Found:** During tic-tac-toe board state implementation

**Description:**
Global variables declared with DIM at module level cannot be accessed or modified from within SUB or FUNCTION:
```basic
DIM board$

SUB InitBoard()
    board$ = "ABCDEFGHI"  ' Assignment has no effect
END SUB

InitBoard()
PRINT board$  ' Prints empty string
```

**Impact:** Cannot use global state for game board or any shared data between procedures. Makes any non-trivial program structure impossible.

**Workaround:** None - this blocks all structured programming requiring shared state. Would need to pass all state as parameters, but BASIC doesn't support passing user-defined types or arrays properly.

## BUG-2004: String comparison in OR condition causes IL error

**Status:** BLOCKER
**Severity:** HIGH
**Found:** During tic-tac-toe menu implementation

**Description:**
String comparisons in OR conditions cause IL generation error:
```basic
IF playAgain$ = "Y" OR playAgain$ = "y" THEN
    GOTO startGame
END IF
```

Results in error:
```
error: main:L1000003: cbr %t2193 label L1000001 label or_rhs18: expected 2 branch argument bundles, or none
```

**Impact:** Cannot use OR conditions with string comparisons

**Workaround:** Use nested IF statements or ELSEIF
