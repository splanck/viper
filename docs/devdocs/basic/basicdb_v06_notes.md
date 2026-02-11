# BasicDB v0.6 Refactoring - Blocked by BUG-030

> **Historical (2025-11-14).** BUG-030 has since been FIXED. Module-level variables now work correctly in
> SUB/FUNCTION procedures. The blocker described here no longer applies.

## Goal

Refactor BasicDB from v0.5 (490 lines with 50 individual variables) to v0.6 using parallel arrays after STRING array
support was added (BUG-033 RESOLVED).

## Status: BLOCKED (at time of writing; BUG-030 has since been resolved)

## Findings

### STRING Arrays Now Work ✅

BUG-033 was resolved in commit a4b0c793. STRING arrays now work with the `$` suffix syntax:

```basic
DIM names$(10)
names$(0) = "Alice"
names$(1) = "Bob"
PRINT names$(0)  ' Outputs: Alice
```

### CONST Scoping Issue ❌

CONST variables declared at module level show as 0 when accessed from within SUB/FUNCTION:

```basic
CONST MAXREC = 10   ' Module level

SUB Test()
    PRINT MAXREC    ' Outputs: 0 (should be 10)
END SUB
```

Workaround: Use DIM variable initialized in SUB.

### Critical: BUG-030 - Global Variables Not Shared Between SUB and FUNCTION ❌

**This is the blocking issue.**

Global variables modified by SUBs are NOT visible to FUNCTIONs:

```basic
DIM GLOBAL_VAR AS INTEGER

SUB SetGlobal()
    GLOBAL_VAR = 42
    PRINT "In SUB: " + STR$(GLOBAL_VAR)      ' Outputs: 42
END SUB

FUNCTION GetGlobal() AS INTEGER
    PRINT "In FUNCTION: " + STR$(GLOBAL_VAR) ' Outputs: 0
    RETURN GLOBAL_VAR
END FUNCTION

SetGlobal()
GetGlobal()  ' Returns 0, not 42!
```

**Impact on BasicDB**:

- DB_Initialize() (SUB) sets DB_COUNT = 0, DB_MAXREC = 10
- DB_AddRecord() (FUNCTION) sees DB_COUNT = 0, DB_MAXREC = 0
- Database immediately reports "full" because 0 >= 0 is true

**Why This Blocks v0.6**:

- Cannot use shared state across SUBs and FUNCTIONs
- Would need to pass ALL state as parameters (DB_COUNT, DB_NEXT_ID, DB_MAXREC, plus 5 arrays)
- This defeats the purpose of the refactoring

## Test Files Created

1. `/devdocs/basic/test_string_arrays.bas` - Verifies STRING arrays work with `$` suffix
2. `/devdocs/basic/test_arrays_simple.bas` - Simple array functionality test
3. `/devdocs/basic/test_const.bas` - CONST scoping test (shows CONST issue)
4. `/devdocs/basic/test_globals.bas` - Global variable visibility test (proves BUG-030)

## Attempted v0.6 Code

Created `/devdocs/basic/basicdb.bas` v0.6 (135 lines) with:

- Parallel arrays instead of 50 individual variables
- Same public API (DB_Initialize, DB_AddRecord, DB_PrintRecord, DB_PrintAll)
- Attempted DB_FindByName (blocked by rt_str_eq extern issue)

Code compiles but does not run correctly due to BUG-030.

## Recommendation

**Cannot proceed with v0.6 until BUG-030 is fixed.**

Alternative approaches:

1. Convert all procedures to SUBs (no FUNCTIONs) - loses return values
2. Pass all state as parameters - extremely verbose and error-prone
3. Wait for BUG-030 fix

## Related Bugs

- ✅ **BUG-033 RESOLVED**: STRING arrays now work (commit a4b0c793)
- ❌ **BUG-030 OUTSTANDING**: Global variables not shared between SUB and FUNCTION
- ❌ **CONST scoping**: Module-level CONST shows as 0 in procedures
- ❌ **rt_str_eq**: String comparison in loops fails with "unknown callee @rt_str_eq"

## Date

2025-11-14
