# VIPER BASIC - Bug Tracking Database

**Last Updated:** 2024-11-23  
**Maintainer:** Development Team  
**Format:** Markdown bug database with cross-references

This file tracks bugs discovered during comprehensive BASIC program development.  
For categorized overview, see: [basic_bugs_categorization.md](basic_bugs_categorization.md)  
For specific detailed reports, see: `basic/BUG0XX_*.md` files

---

## CRITICAL BUGS (P0 - Blocking)

### BUG-030: Global Variable Isolation in SUB/FUNCTION

**Status:** CONFIRMED  
**Severity:** CRITICAL (P0)  
**Discovered:** 2024-11-XX  
**Component:** BASIC Frontend / Runtime

**Description:**  
Global variables are completely isolated from SUB/FUNCTION procedures. Each procedure sees a separate zero-initialized
copy instead of the actual global.

**Test Cases:**

- `devdocs/basic/test_bug030_scenario1.bas` through `scenario6.bas`
- `devdocs/basic/test_globals.bas`

**Detailed Reports:**

- [BUG030_INVESTIGATION_REPORT.md](basic/BUG030_INVESTIGATION_REPORT.md)
- [BUG030_QUICK_REFERENCE.md](basic/BUG030_QUICK_REFERENCE.md)

**Impact:**  
Blocks development of:

- Any modular program using shared state
- Database applications
- Games with persistent state
- Programs > 500 lines requiring structured code

**Workaround:**

- Use parameters and return values
- Use GOSUB (limited functionality)
- Avoid SUB/FUNCTION for state management

---

### BUG-040: NEW Operator for Class Instantiation Broken

**Status:** CONFIRMED  
**Severity:** CRITICAL (P0)  
**Discovered:** 2024-11-23  
**Component:** BASIC Frontend / OOP / Code Generation

**Description:**  
The NEW operator for instantiating classes causes a runtime error. The automatically-generated constructor (`__ctor`)
doesn't properly return a value, causing a "ret: missing result" error.

**Example Code (FAILS):**

```basic
CLASS TestClass
    DIM value AS INTEGER
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()  ' Runtime error here
```

**Error Message:**

```
error: TESTCLASS.__ctor:entry_TESTCLASS.__ctor: ret: missing result
```

**Test Cases:**

- `devdocs/basic/test_class_minimal.bas`
- `devdocs/basic/archive/db_oop.bas` (also fails)

**Impact:**

- **COMPLETELY BLOCKS object-oriented programming**
- Cannot instantiate any class objects
- OOP features are unusable
- Forces procedural programming approach

**Attempted Workarounds (ALL FAILED):**

- Removing explicit NEW() constructor
- Changing PUBLIC to regular visibility
- Using DIM instead of PUBLIC for fields
- Minimal class with single field

**Current Workaround:**

- Must use procedural programming with global arrays
- Cannot use OOP at all

**Related:**

- BUG-041: Constructor NEW() method causes errors (related symptom)

---

### BUG-041: Constructor Method NEW() Causes "Missing Result" Error

**Status:** CONFIRMED  
**Severity:** CRITICAL (P0)  
**Discovered:** 2024-11-23  
**Component:** BASIC Frontend / OOP / Code Generation

**Description:**  
When a CLASS defines an explicit `SUB NEW()` constructor, the compiler generates code that causes a "ret: missing
result" error at runtime.

**Example Code (FAILS):**

```basic
CLASS Record
    PUBLIC id AS INTEGER
    
    PUBLIC SUB NEW()
        id = 0
    END SUB
END CLASS
```

**Error Message:**

```
error: RECORD.__ctor:entry_RECORD.__ctor: ret: missing result
```

**Impact:**

- Cannot use constructor pattern for object initialization
- Must rely on explicit Init() methods after construction
- Reduces OOP capabilities

**Workaround:**

- Remove `SUB NEW()` constructor entirely
- Use separate `Init()` method
- Rely on default field initialization

**Related:**

- BUG-040: NEW operator broken (root cause)

---

### BUG-042: String Comparison Operators (>, <) Runtime Error

**Status:** CONFIRMED  
**Severity:** CRITICAL (P0)  
**Discovered:** 2024-11-23  
**Component:** Runtime / String Operations

**Description:**  
String comparison using `>` and `<` operators causes runtime errors in certain contexts, particularly in sorting
functions. The runtime function `@rt_str_gt` appears to be missing or not properly linked.

**Example Code (FAILS):**

```basic
IF REC_LastName$(j) > REC_LastName$(j + 1) THEN
    ' swap logic
END IF
```

**Error Message:**

```
error: DB_SORTBYLASTNAME:bc_ok1_DB_SORTBYLASTNAME: %60 = call %t43 %t59: 
unknown callee @rt_str_gt
```

**Test Case:**

- `devdocs/basic/basicdb.bas` (string sorting functions)

**Impact:**

- **BLOCKS sorting by string fields**
- Cannot implement alphabetical sorting
- String comparison with `>` and `<` operators unusable
- Equality comparison (`=`) works fine

**Context:**  
The `basic_audit.md` claims "Full lexicographic string comparison now supported for all relational operators", but this
appears to be incomplete or broken in certain contexts (arrays, complex expressions).

**Workaround:**

- None available for sorting
- Must comment out string-based sorting functions
- Can only sort by integer fields

**Related:**

- May be related to BUG-014 (String arrays) if the issue is specific to array element comparisons

---

## HIGH SEVERITY BUGS (P1)

### BUG-043: Line Continuation Character (_) Not Supported

**Status:** CONFIRMED  
**Severity:** HIGH (P1)  
**Discovered:** 2024-11-23  
**Component:** BASIC Frontend / Parser

**Description:**  
The parser does not recognize the underscore (`_`) character for line continuation in function/subroutine parameter
lists or other multi-line statements.

**Example Code (FAILS):**

```basic
PUBLIC SUB SetData(newId AS INTEGER, fname AS STRING, lname AS STRING, _
                   em AS STRING, ph AS STRING)
```

**Error Message:**

```
error[B0001]: expected ident, got ?
error[B0001]: expected ), got eol
```

**Impact:**

- Forces long parameter lists onto single lines
- Reduces code readability
- Makes functions with many parameters harder to maintain
- Not a blocking issue, just a style limitation

**Workaround:**  
Keep all parameters on a single line:

```basic
PUBLIC SUB SetData(newId AS INTEGER, fname AS STRING, lname AS STRING, em AS STRING, ph AS STRING)
```

**Notes:**

- This is a parser-level limitation
- Standard BASIC feature not implemented
- Low priority as workaround is simple

---

## MEDIUM SEVERITY BUGS (P2)

### BUG-044: CALL Keyword Not Supported

**Status:** CONFIRMED  
**Severity:** MEDIUM (P2)  
**Discovered:** 2024-11-23  
**Component:** BASIC Frontend / Parser

**Description:**  
The CALL keyword (traditional BASIC syntax for calling subroutines) is not recognized by the parser.

**Example Code (FAILS):**

```basic
SUB MySubroutine(param AS INTEGER)
    PRINT "Value: " + STR$(param)
END SUB

CALL MySubroutine(42)  ' Fails
```

**Error Message:**

```
error[B0001]: unknown statement 'CALL'; expected keyword or procedure call
```

**Impact:**

- Minor syntax incompatibility with traditional BASIC
- No functional limitation
- Easy workaround

**Workaround:**  
Simply call subroutines directly without the CALL keyword:

```basic
MySubroutine(42)  ' Works
```

**Notes:**

- This is a parser-level omission
- Not a priority since workaround is trivial
- Affects code compatibility with classic BASIC programs

---

## Cross-Reference to Categorized Bugs

The following bugs are documented in [basic_bugs_categorization.md](basic_bugs_categorization.md):

### RESOLVED ✅

- BUG-001: String concatenation $ suffix
- BUG-002: & operator
- BUG-003: FUNCTION name assignment
- BUG-005: SGN function
- BUG-006: TAN, ATN, EXP, LOG functions
- BUG-008: REDIM PRESERVE
- BUG-009: CONST keyword
- BUG-011: SWAP statement

### REQUIRES FIXES

- BUG-021: SELECT CASE negative literals
- BUG-024: CONST with type suffix assertion
- BUG-025: EXP overflow trap

### REQUIRES PLANNING

- BUG-004: Optional parentheses
- BUG-007: Multi-dimensional arrays
- BUG-010: STATIC keyword
- BUG-012: BOOLEAN type system
- BUG-013: SHARED keyword
- BUG-014: String arrays (May be related to BUG-042)
- BUG-015: String properties in classes
- BUG-016: Local strings in methods
- BUG-017: Global strings from methods
- BUG-018: FUNCTION methods in classes
- BUG-019: Float CONST truncation
- BUG-020: String constants runtime error
- BUG-022: Float literal type inference
- BUG-023: DIM with initializer

### OTHER DOCUMENTED BUGS

- BUG-030: Global variable isolation (See detailed section above)
- BUG-037: Method calls (has test file: `test_bug037_method_calls_fixed.bas`)

---

## Testing Matrix

### Test Files in `devdocs/basic/`:

- `test_bug010_static.bas`, `test_bug010_static_fixed.bas`
- `test_bug012_boolean.bas`, `test_bug012_boolean_comparisons.bas`, `test_bug012_str_bool.bas`
- `test_bug017_global_string_method.bas`
- `test_bug019_const_float.bas`, `test_bug019_const_float_fixed.bas`
- `test_bug030_scenario1.bas` through `scenario6.bas`
- `test_bug030_comprehensive.bas`
- `test_bug030_fixed.bas`
- `test_bug037_method_calls_fixed.bas`
- `test_class_minimal.bas` (BUG-040, BUG-041)
- `test_functions.bas` (General test)
- `basicdb.bas` (Comprehensive database - exposes BUG-040, BUG-041, BUG-042)

---

## Impact Summary

### CRITICAL (P0) - 4 bugs:

- **BUG-030:** Blocks modular programming
- **BUG-040:** Blocks OOP completely
- **BUG-041:** Blocks OOP constructors
- **BUG-042:** Blocks string sorting

### HIGH (P1) - 1 bug:

- **BUG-043:** Reduces code readability

### MEDIUM (P2) - 1 bug:

- **BUG-044:** Minor compatibility issue

**Total Bugs Documented in this file:** 6 new bugs  
**Total Bugs in Project:** 44+ (including categorized bugs)

---

## Recommendations

### IMMEDIATE PRIORITY:

1. **Fix BUG-030** (Global variables) - Enables modular programming
2. **Fix BUG-040/041** (OOP NEW operator) - Enables object-oriented programming
3. **Fix BUG-042** (String comparison) - Enables string sorting

### MEDIUM PRIORITY:

4. **Implement BUG-043** (Line continuation) - Improves code style
5. **Implement BUG-044** (CALL keyword) - Traditional BASIC compatibility

---

## Notes

- All bugs discovered during development of `basicdb.bas` (2000+ line database)
- Testing performed on vbasic compiler version from build on 2024-11-23
- Some bugs may be related (BUG-014 string arrays and BUG-042 string comparison)
- OOP features currently unusable due to BUG-040/041
- Procedural workarounds exist for most functionality except OOP

---

**End of Bug Database**
