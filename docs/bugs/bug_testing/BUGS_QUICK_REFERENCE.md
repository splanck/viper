# Quick Reference: Outstanding Bugs (2025-11-15)

> **Historical snapshot.** Most bugs listed here have been fixed in subsequent sessions (November-December 2025).
> See `bugs/oop_bugs.md` and `bugs/basic_resolved.md` for current status.

## CRITICAL (Must Fix for OOP)

### BUG-047: IF/THEN crashes inside class methods — RESOLVED

```basic
CLASS Test
    SUB DoSomething()
        IF x = 0 THEN    ' Was: CRASH
            PRINT "Zero"
        END IF
    END SUB
END CLASS
```

### BUG-048: Cannot call module SUBs from class methods — RESOLVED

```basic
SUB Utility()
    PRINT "Helper"
END SUB

CLASS Test
    SUB DoWork()
        Utility()    ' Was: ERROR: unknown callee
    END SUB
END CLASS
```

---

## MAJOR (Fundamental Features)

### BUG-045: STRING arrays broken — RESOLVED

```basic
DIM names(5) AS STRING
names(0) = "Alice"
```

### BUG-046: Cannot call methods on array elements — RESOLVED

```basic
DIM items(3) AS Item
items(0).Init("Sword")    ' Was: ERROR: expected procedure call
```

---

## MODERATE (IL Generation Issues)

### BUG-050: Multiple CASE values fail — RESOLVED

```basic
SELECT CASE x
    CASE 1, 2, 3    ' Was: ERROR: IL generation
        PRINT "Match"
END SELECT
```

### BUG-051: DO UNTIL broken — RESOLVED

```basic
DO UNTIL i > 5    ' Was: ERROR: IL generation
    i = i + 1
LOOP
```

---

## MINOR (Missing Features)

### BUG-044: No CHR() function — RESOLVED

```basic
ESC = CHR(27)    ' Now works
```

### BUG-049: RND() takes no arguments — RESOLVED

```basic
x = RND(1)    ' Now works
x = RND()     ' Also works
```

---

## Priority Recommendations

All bugs listed in this document have been resolved. No further action required.
See `bugs/basic_resolved.md` for current open issues.

---

## What Works Well

✅ Classes with fields and methods
✅ NEW operator for object creation
✅ INTEGER arrays
✅ Object arrays
✅ All string functions (LEN, LEFT$, RIGHT$, MID$, UCASE$, LCASE$, STR$, VAL)
✅ All math functions (SQR, ABS, INT, SIN, COS, TAN, EXP, LOG, RND)
✅ FOR...NEXT loops
✅ WHILE...WEND loops
✅ DO WHILE...LOOP
✅ DO UNTIL...LOOP
✅ DO...LOOP WHILE
✅ SELECT CASE (single and multiple values)
✅ IF/THEN in class methods and at module level
✅ CONST declarations
✅ ADDFILE keyword
✅ Boolean operations (AND, OR, NOT)
✅ CHR() function
✅ STRING arrays

---

See `STRESS_TEST_SUMMARY.md` for full details.
