# Viper BASIC Bugs Found During Poker Game Development

## BUG-POKER-001: RETURN statement inside FUNCTION causes type mismatch error

**Date:** 2025-11-18
**Severity:** Medium
**Status:** Found

**Description:**
Using RETURN statement inside a FUNCTION to early-exit causes a compilation error: "ret value type mismatch"

**Reproduction:**
```basic
Function HasThreeOfKind() AS Boolean
    DIM i AS Integer
    FOR i = 0 TO ME.count - 1
        IF ME.CountValue(ME.cards(i).value) = 3 THEN
            LET HasThreeOfKind = TRUE
            RETURN    ' <-- This line causes the error
        END IF
    NEXT i
    LET HasThreeOfKind = FALSE
End Function
```

**Error Message:**
```
poker_hand.bas:100:17: error: HAND.HASTHREEOFKIND:if_then_0_HAND.HASTHREEOFKIND: ret: ret value type mismatch
```

**Workaround:**
Remove RETURN statement and use flag variable or restructure logic to avoid early return.

**Expected Behavior:**
RETURN should exit the function early with the already-set return value.

---

## BUG-POKER-002: Array index out of bounds when accessing counted array in CountPairs

**Date:** 2025-11-18
**Severity:** High
**Status:** Found

**Description:**
When evaluating poker hands with multiple players, the `CountPairs()` function causes an array index out of bounds error. The error occurs when trying to access `counted(val)` where `val` might be the card value (2-14 for cards).

**Reproduction:**
Create multiple players, deal cards, and evaluate hands:
```basic
Function CountPairs() AS Integer
    DIM i AS Integer
    DIM pairs AS Integer
    DIM counted(15) AS Boolean  ' Length 15, but cards have values 2-14
    ...
    FOR i = 0 TO ME.count - 1
        DIM val AS Integer
        LET val = ME.cards(i).value  ' Values range from 2-14
        IF NOT counted(val) THEN     ' Accessing counted with card value!
            ...
        END IF
    NEXT i
End Function
```

**Error Message:**
```
rt_arr_i32: index 6 out of bounds (len=5)
```

**Root Cause:**
Card values range from 2-14, but we're declaring `counted(15)` which creates an array of length 15 (indices 0-14). When we try to access `counted(val)` where `val` might be 6, 7, 8, etc., we're using the card value directly as an index without adjusting for the 0-based array.

**Workaround:**
Use a larger array or adjust indices by subtracting the minimum card value (2) before indexing.

**Expected Behavior:**
Should be able to use card values directly as array indices or properly offset them.
