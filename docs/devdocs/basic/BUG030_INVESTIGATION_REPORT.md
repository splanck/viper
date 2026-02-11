# BUG-030: Global Variable Isolation - Detailed Investigation Report

> **Historical (2025-11-14).** This bug has been FIXED. Module-level variables are now accessible from SUB/FUNCTION
> via `rt_modvar_addr_*` runtime helpers. See `bugs/basic_resolved.md` for resolution details.

**Date**: 2025-11-14
**Severity**: CRITICAL (when reported)
**Status**: RESOLVED
**Original Discovery**: 2025-11-12 (dungeon_quest_v4.bas)

---

## Executive Summary

**Global variables declared at module level are completely isolated from SUB and FUNCTION procedures.** Each procedure
sees a separate, zero-initialized copy of global variables. Modifications made inside procedures have NO effect on the
actual global variables in main code or other procedures.

This is not a scope visibility issue - it's a **complete isolation bug** where procedures operate on different memory
locations than the main program.

---

## What SHOULD Work (Standard BASIC Behavior)

In standard BASIC (QBASIC, GW-BASIC, Visual Basic, etc.):

1. **Global variables are shared** - All code (main, SUB, FUNCTION) accesses the same memory location
2. **SUBs can read and modify globals** - Changes persist after SUB returns
3. **FUNCTIONs can read and modify globals** - Same as SUBs
4. **Procedures communicate via globals** - SUB can modify, FUNCTION can read the modification

### Expected Example:

```basic
DIM COUNTER AS INTEGER
COUNTER = 100

SUB Increment()
    COUNTER = COUNTER + 1  ' Should modify the actual global
END SUB

Increment()
PRINT COUNTER  ' Should print 101
```

**Expected output**: `101`

---

## What ACTUALLY Happens in VIPER BASIC

### Test Results Summary

| Scenario                | Main Value | SUB Sees   | FUNCTION Sees | Main After | Status   |
|-------------------------|------------|------------|---------------|------------|----------|
| 1. Main→SUB→Main        | 100        | 0          | -             | 100        | ❌ BROKEN |
| 2. Main→FUNC→Main       | 100        | -          | 0             | 100        | ❌ BROKEN |
| 3. SUB sets, FUNC reads | 50         | 0→200      | 0             | 50         | ❌ BROKEN |
| 4. STRING globals       | "Hello"    | ""→"World" | ""            | "Hello"    | ❌ BROKEN |
| 5. SUB→SUB              | 10         | 0→20       | - (0)         | 10         | ❌ BROKEN |
| 6. FUNC→FUNC            | 15         | -          | 0→30, 0       | 15         | ❌ BROKEN |

---

## Detailed Test Case Analysis

### Test 1: Main → SUB → Main (INTEGER)

**Code**:

```basic
DIM COUNTER AS INTEGER
COUNTER = 100

SUB IncrementCounter()
    PRINT "In SUB: COUNTER = " + STR$(COUNTER)
    COUNTER = COUNTER + 50
END SUB

IncrementCounter()
PRINT "After SUB: COUNTER = " + STR$(COUNTER)
```

**Actual Output**:

```
Main before SUB: COUNTER = 100
In SUB before increment: COUNTER = 0    ← SUB sees 0, not 100!
In SUB after increment: COUNTER = 50    ← SUB's local copy
Main after SUB: COUNTER = 100           ← Main still sees 100!
```

**Analysis**:

- Main code: `COUNTER` is at memory location A, value = 100
- Inside SUB: `COUNTER` is at memory location B, value = 0 (zero-initialized)
- SUB modifies location B to 50
- Main still sees location A = 100

**Conclusion**: SUB operates on a completely separate copy of the variable.

---

### Test 2: Main → FUNCTION → Main (INTEGER)

**Code**:

```basic
DIM VALUE AS INTEGER
VALUE = 100

FUNCTION GetDoubled() AS INTEGER
    PRINT "In FUNCTION: VALUE = " + STR$(VALUE)
    RETURN VALUE * 2
END FUNCTION

result = GetDoubled()
PRINT "FUNCTION returned: " + STR$(result)
```

**Actual Output**:

```
Main before FUNCTION: VALUE = 100
In FUNCTION: VALUE = 0            ← FUNCTION sees 0!
FUNCTION returned: 0              ← Returns 0 * 2 = 0
Main after FUNCTION: VALUE = 100
```

**Analysis**: Same isolation as SUB. FUNCTION sees its own zero-initialized copy.

---

### Test 3: SUB Modifies, Then FUNCTION Reads

**Code**:

```basic
DIM SHARED_VAR AS INTEGER
SHARED_VAR = 50

SUB SetValue()
    SHARED_VAR = 200
END SUB

FUNCTION GetValue() AS INTEGER
    RETURN SHARED_VAR
END FUNCTION

SetValue()
result = GetValue()
```

**Actual Output**:

```
Main initial: SHARED_VAR = 50
SUB before: SHARED_VAR = 0        ← SUB sees 0
SUB after: SHARED_VAR = 200       ← SUB sets its copy to 200
Main after SUB: SHARED_VAR = 50   ← Main unchanged
FUNCTION reads: SHARED_VAR = 0    ← FUNCTION sees 0, not SUB's 200!
FUNCTION returned: 0
Main final: SHARED_VAR = 50
```

**Analysis**:

- SUB's modification is invisible to FUNCTION
- FUNCTION's copy is also separate from SUB's copy
- Three separate memory locations exist!

**Critical Finding**: Even procedures cannot communicate with each other via globals.

---

### Test 4: STRING Global Variables

**Code**:

```basic
DIM MESSAGE AS STRING
MESSAGE = "Hello"

SUB ModifyString()
    PRINT "SUB before: MESSAGE = " + MESSAGE
    MESSAGE = "World"
END SUB

ModifyString()
PRINT "Main after SUB: MESSAGE = " + MESSAGE
```

**Actual Output**:

```
Main initial: MESSAGE = Hello
SUB before: MESSAGE =              ← Empty string (zero-init for strings)
SUB after: MESSAGE = World         ← SUB's local copy
Main after SUB: MESSAGE = Hello    ← Main unchanged
```

**Analysis**: Bug affects STRING variables exactly the same way as INTEGER.

---

### Test 5: SUB → SUB Communication

**Code**:

```basic
DIM STATE AS INTEGER
STATE = 10

SUB FirstSub()
    STATE = 20
END SUB

SUB SecondSub()
    PRINT "SecondSub reads: STATE = " + STR$(STATE)
END SUB

FirstSub()
SecondSub()
```

**Actual Output**:

```
Main initial: STATE = 10
FirstSub before: STATE = 0
FirstSub after: STATE = 20
Main after FirstSub: STATE = 10    ← Main unchanged
SecondSub reads: STATE = 0         ← SecondSub sees 0, not FirstSub's 20!
```

**Analysis**: Even two SUBs cannot share data via globals. Each SUB has its own isolated copy.

---

### Test 6: FUNCTION → FUNCTION Communication

**Code**:

```basic
DIM DATA AS INTEGER
DATA = 15

FUNCTION FirstFunc() AS INTEGER
    DATA = 30
    RETURN DATA
END FUNCTION

FUNCTION SecondFunc() AS INTEGER
    RETURN DATA
END FUNCTION

r1 = FirstFunc()
r2 = SecondFunc()
```

**Actual Output**:

```
Main initial: DATA = 15
FirstFunc before: DATA = 0
FirstFunc after: DATA = 30
FirstFunc returned: 30             ← Returns its local copy
Main after FirstFunc: DATA = 15    ← Main unchanged
SecondFunc reads: DATA = 0         ← SecondFunc sees 0, not FirstFunc's 30!
SecondFunc returned: 0
```

**Analysis**: FUNCTIONs are also completely isolated from each other.

---

## Isolation Pattern Analysis

### Memory Model Hypothesis

Based on test results, VIPER BASIC appears to create **separate variable instances** for each scope:

```
┌─────────────┬───────────┬───────────────┬───────────────┐
│ Scope       │ Variable  │ Initial Value │ Memory Loc    │
├─────────────┼───────────┼───────────────┼───────────────┤
│ Main        │ COUNTER   │ 100           │ Location A    │
│ SUB Foo     │ COUNTER   │ 0             │ Location B    │
│ SUB Bar     │ COUNTER   │ 0             │ Location C    │
│ FUNCTION Baz│ COUNTER   │ 0             │ Location D    │
└─────────────┴───────────┴───────────────┴───────────────┘
```

Each procedure gets a **shadow copy** that:

1. Is zero-initialized (0 for INTEGER, "" for STRING)
2. Can be read and modified locally
3. Has NO connection to the actual global variable
4. Is discarded when the procedure returns

---

## What Actually Works

✅ **Parameters** - SUB/FUNCTION parameters work correctly:

```basic
SUB PrintValue(x AS INTEGER)
    PRINT x  ' Works - prints the passed value
END SUB
```

✅ **Return values** - FUNCTIONs can return values:

```basic
FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a + b  ' Works correctly
END FUNCTION
```

✅ **Local variables** - Variables declared inside SUB/FUNCTION work:

```basic
SUB Test()
    DIM local AS INTEGER
    local = 42  ' Works fine
END SUB
```

---

## What Is Completely Broken

❌ **Global variable reading** - Procedures see zero-initialized copies, not actual values
❌ **Global variable writing** - Modifications have no effect on the actual global
❌ **Procedure-to-procedure communication via globals** - Each procedure is isolated
❌ **Shared state** - Impossible to maintain state across procedure calls
❌ **Module-level constants** - CONST variables show as 0 in procedures (separate issue)
❌ **Global arrays** - Arrays have zero length in procedures (causes bounds errors)

---

## Real-World Impact

### Programs That Cannot Be Written

1. **Database systems** - No way to maintain shared record count, arrays, etc.
2. **Games** - Cannot maintain player health, score, inventory across procedures
3. **State machines** - Cannot track current state
4. **Counters/accumulators** - Cannot accumulate values across procedure calls
5. **Configuration** - Cannot store global settings
6. **Any modular program** - Procedures cannot access shared data structures

### Workarounds and Limitations

**Workaround 1**: Pass everything as parameters

```basic
' Instead of using globals, pass all state:
FUNCTION Add1(counter AS INTEGER) AS INTEGER
    RETURN counter + 1
END FUNCTION

counter = Add1(counter)  ' Awkward and verbose
```

**Limitation**:

- Becomes extremely verbose for >2-3 variables
- Cannot handle complex data structures
- Defeats the purpose of procedures

**Workaround 2**: Use GOSUB instead of SUB/FUNCTION

```basic
counter = 100
GOSUB Increment
PRINT counter  ' Works - prints 101

Increment:
    counter = counter + 1
    RETURN
```

**Limitation**:

- No parameters or return values
- No local variables
- No scoping or modularity
- Label namespace pollution

**Workaround 3**: Inline all code (no procedures)

**Limitation**:

- Code duplication
- No code reuse
- Unmaintainable for programs >500 lines

---

## Comparison with Basic DB Development

This bug is **the reason BasicDB v0.6 refactoring failed**:

```basic
DIM DB_COUNT AS INTEGER
DIM DB_MAXREC AS INTEGER

SUB DB_Initialize()
    DB_MAXREC = 10    ' Sets local copy
    DB_COUNT = 0      ' Sets local copy
END SUB

FUNCTION DB_AddRecord(...) AS INTEGER
    ' Sees DB_MAXREC = 0, DB_COUNT = 0 (zero-initialized copies)
    IF DB_COUNT >= DB_MAXREC THEN  ' 0 >= 0 is TRUE!
        PRINT "ERROR: Database full"
        RETURN -1
    END IF
    ' ...never actually adds records
END FUNCTION
```

The database reports "full" immediately because the FUNCTION cannot see the SUB's initialization.

---

## Expected Behavior (Standard BASIC)

In QBasic, GW-BASIC, VB, and all standard BASIC implementations:

```basic
' Microsoft QBasic 1.1
DIM SHARED counter AS INTEGER  ' SHARED keyword optional at module level
counter = 100

SUB Increment
    counter = counter + 1  ' Modifies the actual global
END SUB

Increment
PRINT counter  ' Prints: 101
```

**Note**: Some BASIC dialects require `DIM SHARED` for globals, but VIPER BASIC doesn't support `SHARED` keyword, and
regular `DIM` at module level should create a global.

---

## Root Cause Analysis (Hypothesis)

Based on behavior, likely causes:

1. **Symbol table scoping bug** - Procedure scopes don't search parent (module) scope for variables
2. **Variable lowering issue** - Global variables are incorrectly lowered as procedure-local
3. **IL generation bug** - Module-level variables get separate allocations per procedure
4. **Missing SHARED semantics** - System expects `DIM SHARED` but doesn't support the keyword

Recommendation: Examine `SemanticAnalyzer` variable lookup for procedures and IL lowering for module-level variables.

---

## Test Files Provided

All test cases are in `/devdocs/basic/`:

1. `test_bug030_scenario1.bas` - Main → SUB → Main (INTEGER)
2. `test_bug030_scenario2.bas` - Main → FUNCTION → Main (INTEGER)
3. `test_bug030_scenario3.bas` - SUB modifies, FUNCTION reads
4. `test_bug030_scenario4.bas` - STRING global variables
5. `test_bug030_scenario5.bas` - SUB → SUB communication
6. `test_bug030_scenario6.bas` - FUNCTION → FUNCTION communication
7. `test_bug030_comprehensive.bas` - All scenarios in one file
8. `test_globals.bas` - Minimal reproduction case

All tests compile successfully but produce incorrect runtime behavior.

---

## Priority and Severity

**Priority**: 🔴 **P0 - CRITICAL BLOCKER**

**Severity**: This is a **language-breaking bug** that makes SUB/FUNCTION essentially useless.

**Blocks**:

- BasicDB v0.6 refactoring (confirmed)
- Any database or CRUD application
- Any game with shared state
- Any program using modular design
- Any program >500 lines (per DUNGEON_QUEST_TEST_SUMMARY.md)

**Impact**: Combined with BUG-026 (DO WHILE + GOSUB), there is **NO viable way to write modular, complex programs** in
current VIPER BASIC.

---

## Recommendations

1. **Immediate**: Fix SUB/FUNCTION scope to search module-level scope for variables
2. **Verify**: Test that modifications inside procedures affect the actual global
3. **Test**: Run all 8 test cases after fix to confirm resolution
4. **Document**: Add test cases to regression suite
5. **Consider**: Whether to support `SHARED` keyword for explicit global marking (optional)

---

## Related Bugs

- **BUG-035**: Duplicate report of BUG-030 (found during tic-tac-toe development)
- **CONST scoping**: Module-level CONST also shows as 0 in procedures (likely same root cause)
- **Array bounds**: Global arrays have zero length in procedures (likely same root cause)

---

**Report End**
*Generated from comprehensive test suite execution*
