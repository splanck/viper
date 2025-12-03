# BUG-030: Quick Reference Guide

## The Problem in One Sentence

**Global variables are completely isolated from SUB/FUNCTION procedures - each procedure sees a separate zero-initialized copy.**

---

## Visual Example

```basic
DIM COUNTER AS INTEGER
COUNTER = 100

SUB Increment()
    COUNTER = COUNTER + 1
END SUB

Increment()
PRINT COUNTER
```

### What SHOULD Happen:
```
Prints: 101
```

### What ACTUALLY Happens:
```
Prints: 100
```

**Why**: The SUB sees `COUNTER = 0`, adds 1 to get 1, but main still has `COUNTER = 100`.

---

## Quick Test Results Table

| What You Try | What You See |
|--------------|--------------|
| Main sets global to 100 | ✅ Works |
| SUB reads global | ❌ Sees 0 instead of 100 |
| SUB modifies global to 200 | ❌ Main still sees 100 |
| FUNCTION reads global | ❌ Sees 0 instead of 100 |
| FUNCTION returns global * 2 | ❌ Returns 0 (because sees 0) |
| SUB sets 200, FUNCTION reads | ❌ FUNCTION sees 0, not 200 |

---

## What Works (Pass Data Instead)

✅ **Parameters work:**
```basic
SUB PrintValue(x AS INTEGER)
    PRINT x  ' Correctly prints passed value
END SUB

PrintValue(42)  ' Prints: 42
```

✅ **Return values work:**
```basic
FUNCTION Double(x AS INTEGER) AS INTEGER
    RETURN x * 2
END FUNCTION

result = Double(21)  ' Returns: 42
```

✅ **GOSUB works (but limited):**
```basic
counter = 100
GOSUB Increment
PRINT counter  ' Prints: 101

Increment:
    counter = counter + 1
    RETURN
```

---

## Real Example: Database Bug

```basic
DIM DB_COUNT AS INTEGER
DIM DB_MAX AS INTEGER

SUB DB_Init()
    DB_MAX = 10
    DB_COUNT = 0
END SUB

FUNCTION DB_Add() AS INTEGER
    IF DB_COUNT >= DB_MAX THEN  ' Sees 0 >= 0 = TRUE!
        RETURN -1  ' "Database full" immediately
    END IF
    ' ...
END FUNCTION
```

**Result**: Database always reports "full" because FUNCTION sees `DB_COUNT=0, DB_MAX=0`.

---

## How To Test It Yourself

Run this simple test:

```basic
DIM TEST AS INTEGER
TEST = 999

SUB ShowBug()
    PRINT "In SUB: TEST = " + STR$(TEST)
END SUB

PRINT "In Main: TEST = " + STR$(TEST)
ShowBug()
```

**Output:**
```
In Main: TEST = 999
In SUB: TEST = 0
```

---

## Impact

🔴 **CRITICAL**: Cannot write any program that needs:
- Shared state
- Database operations
- Game state (health, score, inventory)
- Counters
- Configuration
- Modular design with >500 lines

---

## Files

- **Full Report**: `/devdocs/basic/BUG030_INVESTIGATION_REPORT.md` (detailed analysis)
- **Test Cases**: `/devdocs/basic/test_bug030_scenario*.bas` (6 scenarios)
- **Minimal Test**: `/devdocs/basic/test_globals.bas`

---

## Status

**Confirmed**: Multiple test cases prove complete isolation
**Severity**: P0 CRITICAL BLOCKER
**Blocks**: BasicDB v0.6, all modular programs, games, databases

---

*Run any test_bug030_scenario*.bas file to see the bug in action.*
