# VIPER BASIC - New Bugs Discovered During BasicDB Development

*Last Updated: 2025-11-13*
*Source: Comprehensive OOP testing during BasicDB database system development*

---

## SUMMARY

**Bug Statistics**: 6 new bugs discovered (5 critical, 1 medium severity)

These bugs were discovered while attempting to build a comprehensive database management system using BASIC's OOP features. All bugs significantly limit the practical use of object-oriented programming in VIPER BASIC.

---

## BUG REPORTS

### BUG-NEW-001: SUB methods on class instances cannot be called
**Status**: 🔴 CRITICAL - Blocks OOP mutation methods
**Severity**: HIGH
**Discovered**: 2025-11-13

**Description**:
When calling SUB methods on class instances, the compiler reports "unknown procedure" errors. FUNCTION methods work correctly, but SUB methods fail completely.

**Example**:
```basic
CLASS Database
  SUB IncrementCount()
    LET Me.count = Me.count + 1
  END SUB
END CLASS

DIM db AS Database
db = NEW Database()
db.IncrementCount()   ' ERROR: unknown procedure 'db.incrementcount'
```

**Error Message**:
```
error[B1006]: unknown procedure 'db.incrementcount'
```

**Workaround**:
Convert all SUB methods to FUNCTION methods that return a dummy value (e.g., INTEGER returning 0).

**Impact**:
- Cannot use SUB methods on class instances
- All mutation methods must be FUNCTIONs
- Reduces code clarity (mutation methods shouldn't return values)

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.2

---

### BUG-NEW-002: String concatenation with method results fails in certain contexts
**Status**: 🟡 MEDIUM - Has workaround
**Severity**: MEDIUM
**Discovered**: 2025-11-13

**Description**:
When concatenating strings with the result of a method call inline, sometimes get operand type mismatch errors even though the method returns STRING.

**Example**:
```basic
CLASS Record
  FUNCTION ToString() AS STRING
    RETURN "Record data"
  END FUNCTION
END CLASS

DIM rec AS Record
rec = NEW Record(1, "Test", "test@example.com", 25)
PRINT "After updates: " + rec.ToString()  ' ERROR
```

**Error Message**:
```
error[B2001]: operand type mismatch
```

**Workaround**:
Store method result in a temporary variable first, then concatenate:
```basic
DIM tempStr AS STRING
tempStr = rec.ToString()
PRINT "After updates: " + tempStr  ' Works
```

**Impact**:
- Cannot inline method calls in string concatenation in all contexts
- Requires extra variables and lines of code
- Reduces code readability

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.2, line 138

---

### BUG-NEW-003: Methods that mutate object state cause IL generation errors
**Status**: 🔴 CRITICAL - Blocks OOP state management
**Severity**: HIGH
**Discovered**: 2025-11-13

**Description**:
Methods that both modify object state (Me.field = ...) and return a value cause "call arg type mismatch" errors during IL generation, even after workaround for BUG-NEW-001.

**Example**:
```basic
CLASS Database
  FUNCTION IncrementCount() AS INTEGER
    LET Me.recordCount = Me.recordCount + 1
    RETURN 0
  END FUNCTION
END CLASS

DIM db AS Database
db = NEW Database()
DIM dummy AS INTEGER
dummy = db.IncrementCount()  ' ERROR during IL generation
```

**Error Message**:
```
error: main:obj_assign_cont1: call %t43: call arg type mismatch
```

**Workaround**:
None found. Cannot use methods for state mutations on objects.

**Impact**:
- **CRITICAL**: Cannot implement proper OOP encapsulation
- Must use direct field access or external functions
- Defeats the purpose of object-oriented design
- Makes classes essentially read-only after construction

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.2

---

### BUG-NEW-004: Cannot use custom class types as function return types
**Status**: 🔴 CRITICAL - Blocks factory patterns
**Severity**: HIGH
**Discovered**: 2025-11-13

**Description**:
Functions can only return built-in types (INTEGER, STRING, etc.). Cannot declare a function that returns a custom CLASS type.

**Example**:
```basic
CLASS Record
  ' ...
END CLASS

FUNCTION DB_GetRecordById(id AS INTEGER) AS Record  ' ERROR
  ' ...
END FUNCTION
```

**Error Message**:
```
error[B0001]: unknown statement 'RECORD'; expected keyword or procedure call
```

**Workaround**:
Return an index into an array instead of the object itself:
```basic
FUNCTION DB_FindRecordIndexById(id AS INTEGER) AS INTEGER
  ' Return index, then caller uses: DB_RECORDS(index)
  RETURN index
END FUNCTION
```

**Impact**:
- Cannot create factory functions or getters that return objects
- Must use array indexing directly
- Limits abstraction capabilities
- Makes APIs less clean and more error-prone

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.3

---

### BUG-NEW-005: Cannot create arrays of custom class types
**Status**: 🔴 CRITICAL - Blocks collections
**Severity**: CRITICAL
**Discovered**: 2025-11-13

**Description**:
Cannot declare or use arrays of custom CLASS types. Attempting to assign class instances to array elements causes IL generation errors.

**Example**:
```basic
CLASS Record
  id AS INTEGER
  name AS STRING
END CLASS

DIM DB_RECORDS(100) AS Record
DB_RECORDS(0) = NEW Record(1, "Test", "test@example.com", 25)  ' ERROR
```

**Error Message**:
```
error: DB_ADDRECORD:bc_ok0_DB_ADDRECORD: call %t26 %t27 %t21:
       @rt_arr_i32_set value operand must be i64
```

**Workaround**:
Use parallel arrays - one array per field:
```basic
DIM DB_IDS(100) AS INTEGER
DIM DB_NAMES(100) AS STRING
DIM DB_EMAILS(100) AS STRING
DIM DB_AGES(100) AS INTEGER
```

**Impact**:
- **CRITICAL**: Cannot store collections of objects
- Fundamentally limits OOP capabilities
- Must use primitive type arrays with parallel indexing
- Error-prone (easy to get indices out of sync)
- Reduces code maintainability dramatically

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.3

**Notes**: This is perhaps the most severe limitation - without object arrays, you cannot build meaningful data structures or collections in an OOP style.

---

### BUG-NEW-006: Reserved keyword 'LINE' cannot be used as variable name
**Status**: 🔴 CRITICAL - Parser confusion
**Severity**: HIGH
**Discovered**: 2025-11-13

**Description**:
The identifier `line` (or `LINE`) cannot be used as a variable name because the parser thinks it's part of `LINE INPUT` statement, even when used as `DIM line AS STRING`.

**Example**:
```basic
DIM line AS STRING  ' ERROR
line = "Hello"      ' Parser expects LINE INPUT syntax
```

**Error Message**:
```
error[B0001]: expected ident, got LINE
error[B0001]: expected INPUT, got =
```

**Workaround**:
Use a different variable name (e.g., `lineStr`, `output`, `result`).

**Impact**:
- Common variable name is reserved
- Parser error messages are confusing
- Violates principle of least surprise

**Test Case**: See `/devdocs/basic/basicdb.bas` version 0.4, line 156

**Notes**: This suggests the parser may have issues with other statement-prefix keywords when used as identifiers. May affect: `INPUT`, `DATA`, `FOR`, `IF`, etc.

---

## ARCHITECTURAL IMPLICATIONS

These bugs reveal fundamental limitations in the current BASIC OOP implementation:

1. **Objects are effectively immutable after construction** - Cannot modify state via methods
2. **No proper collections** - Cannot store arrays of objects
3. **Limited abstraction** - Cannot return objects from functions
4. **Forced procedural style** - Even with classes, must use module-level functions and parallel arrays

**Recommendation**: Until these are fixed, OOP features should be considered experimental/limited. For production code, stick to procedural style with functions and parallel arrays.

---

## WORKAROUNDS SUMMARY

For building database-style applications (see `basicdb.bas`):

1. Use **parallel arrays** instead of object arrays (fields as separate arrays)
2. Use **module-level functions** instead of class methods for mutations
3. Use **array indices** instead of object references for lookups
4. Avoid **inline method calls** in expressions; use temp variables
5. Reserve **SUBs for top-level** procedures only, not class methods
6. Use different **variable names** to avoid reserved keywords

---

## TEST FILE

All bugs documented here were discovered during development of:
- **File**: `/devdocs/basic/basicdb.bas`
- **Purpose**: Comprehensive database management system
- **Features Attempted**: CRUD operations, search, filtering, sorting, statistics
- **Current Version**: 0.4 (661 lines)
- **Status**: Functional using workarounds (parallel arrays, procedural style)

---

*End of Bug Report*

### BUG-NEW-007: STRING arrays do not work
**Status**: 🔴 CRITICAL - Contradicts BUG-014 resolution
**Severity**: CRITICAL
**Discovered**: 2025-11-13

**Description**:
Cannot create or use arrays of STRING type. Despite BUG-014 being marked as RESOLVED in basic_bugs.md, STRING arrays fail with "array element type mismatch" errors.

**Example**:
```basic
DIM names(5) AS STRING
names(0) = "Alice"  ' ERROR
names(1) = "Bob"    ' ERROR
```

**Error Message**:
```
error[B2001]: array element type mismatch: expected INT, got STRING
```

**Workaround**:
None practical. Cannot store collections of strings.

**Impact**:
- **CRITICAL**: Cannot store lists of names, emails, or any text data
- Makes database applications nearly impossible
- Arrays only work for INTEGER types
- Contradicts documentation claiming BUG-014 is resolved

**Test Case**: See `/devdocs/basic/test_string_arrays.bas`

**Notes**: This is a regression or the resolution was incomplete. Combined with BUG-NEW-005 (no object arrays), this makes VIPER BASIC arrays only useful for numeric data.

---
