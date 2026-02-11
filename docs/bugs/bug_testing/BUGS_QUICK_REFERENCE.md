# Quick Reference: Outstanding Bugs (2025-11-15)

> **Historical snapshot.** Most bugs listed here have been fixed in subsequent sessions (November-December 2025).
> See `bugs/oop_bugs.md` and `bugs/basic_resolved.md` for current status.

## CRITICAL (Must Fix for OOP)

### BUG-047: IF/THEN crashes inside class methods

```basic
CLASS Test
    SUB DoSomething()
        IF x = 0 THEN    ' CRASH!
            PRINT "Zero"
        END IF
    END SUB
END CLASS
```

**Fix Required**: Enable IF/THEN/END IF in class methods

### BUG-048: Cannot call module SUBs from class methods

```basic
SUB Utility()
    PRINT "Helper"
END SUB

CLASS Test
    SUB DoWork()
        Utility()    ' ERROR: unknown callee
    END SUB
END CLASS
```

**Fix Required**: Enable cross-scope function calls

---

## MAJOR (Fundamental Features)

### BUG-045: STRING arrays broken

```basic
DIM names(5) AS STRING    ' Declares as INT instead
names(0) = "Alice"        ' ERROR: type mismatch
```

**Workaround**: Use object arrays with string fields

### BUG-046: Cannot call methods on array elements

```basic
DIM items(3) AS Item
items(0).Init("Sword")    ' ERROR: expected procedure call
```

**Workaround**:

```basic
DIM temp AS Item
temp = items(0)
temp.Init("Sword")
items(0) = temp
```

---

## MODERATE (IL Generation Issues)

### BUG-050: Multiple CASE values fail

```basic
SELECT CASE x
    CASE 1, 2, 3    ' ERROR: IL generation
        PRINT "Match"
END SELECT
```

**Workaround**: Separate CASE statements

### BUG-051: DO UNTIL broken

```basic
DO UNTIL i > 5    ' ERROR: IL generation
    i = i + 1
LOOP
```

**Workaround**: `DO WHILE NOT (i > 5)`

---

## MINOR (Missing Features)

### BUG-044: No CHR() function

```basic
ESC = CHR(27)    ' ERROR: unknown procedure
```

**Impact**: Cannot generate ANSI codes or control characters

### BUG-049: RND() takes no arguments

```basic
x = RND(1)    ' ERROR: expected )
x = RND()     ' OK
```

**Impact**: Cannot control RNG behavior

---

## Priority Recommendations

1. **BUG-047** - IF in methods (CRITICAL)
2. **BUG-048** - Module function calls (CRITICAL)
3. **BUG-045** - STRING arrays (MAJOR)
4. **BUG-046** - Array method calls (MAJOR)
5. **BUG-051** - DO UNTIL (MODERATE)
6. **BUG-050** - Multiple CASE values (MODERATE)
7. **BUG-044** - CHR() (MINOR)
8. **BUG-049** - RND() signature (MINOR)

---

## What Works Well

✅ Classes with fields and methods
✅ NEW operator for object creation
✅ INTEGER arrays
✅ Object arrays (with workarounds)
✅ All string functions (LEN, LEFT$, RIGHT$, MID$, UCASE$, LCASE$, STR$, VAL)
✅ All math functions (SQR, ABS, INT, SIN, COS, TAN, EXP, LOG, RND)
✅ FOR...NEXT loops
✅ WHILE...WEND loops
✅ DO WHILE...LOOP
✅ DO...LOOP WHILE
✅ SELECT CASE (single values)
✅ IF/THEN at module level
✅ CONST declarations
✅ ADDFILE keyword
✅ Boolean operations (AND, OR, NOT)

---

See `STRESS_TEST_SUMMARY.md` for full details.
