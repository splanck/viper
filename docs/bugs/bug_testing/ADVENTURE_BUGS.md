# Bugs Found During Adventure Game Stress Test

> **Historical snapshot (2025-11-15).** All bugs listed below (BUG-057 through BUG-061) have since been resolved. See `basic_bugs.md` for current status.

## BUG-057: BOOLEAN return type in class methods causes type mismatch

**Status**: RESOLVED
**Discovered**: 2025-11-15 during adventure game stress test
**Severity**: MODERATE

**Description**:
Functions in classes that return BOOLEAN type cause IL verification error: "ret value type mismatch: expected i1 but got
i64"

**Test Case**:

```basic
CLASS Player
    DIM health AS INTEGER

    FUNCTION IsAlive() AS BOOLEAN
        RETURN health > 0
    END FUNCTION
END CLASS
```

**Error**:

```
error: PLAYER.ISALIVE:entry_PLAYER.ISALIVE: ret %t7: ret value type mismatch: expected i1 but got i64
```

**Workaround**: Use INTEGER return type with explicit 1/0 values

---

## BUG-058: String array fields in classes don't retain values

**Status**: RESOLVED
**Discovered**: 2025-11-15 during adventure game stress test
**Severity**: HIGH

**Description**:
String arrays declared as class fields can be assigned to, but the values don't persist - they read back as empty
strings.

**Test Case**:

```basic
CLASS Player
    DIM inventory(10) AS STRING
    DIM inventoryCount AS INTEGER

    SUB AddItem(item AS STRING)
        inventory(inventoryCount) = item
        inventoryCount = inventoryCount + 1
    END SUB
END CLASS

DIM p AS Player
p = NEW Player()
p.AddItem("Sword")
PRINT p.inventory(0)  ' Prints empty string instead of "Sword"
```

**Observed**:

- Assignment succeeds without error
- Reading back the value returns empty string
- Integer array fields work correctly
- Local string arrays work correctly

**Root Cause**: Possibly related to BUG-056 fix - constructor might not be initializing string arrays correctly, or
string array set/get might not be working for class fields.

**Impact**: Cannot use string arrays in classes, severely limits OOP game design

---

## BUG-059: Cannot access array fields within class methods

**Status**: RESOLVED
**Discovered**: 2025-11-15 during adventure game stress test
**Severity**: CRITICAL

**Description**:
Accessing array fields from within class methods causes IL generation error: "unknown callee @arrayname"

**Test Case**:

```basic
CLASS Room
    DIM exits(4) AS INTEGER

    FUNCTION GetExit(direction AS INTEGER) AS INTEGER
        DIM result AS INTEGER
        result = exits(direction)  ' ERROR HERE
        RETURN result
    END FUNCTION
END CLASS
```

**Error**:

```
error: ROOM.GETEXIT:entry_ROOM.GETEXIT: call %t5: unknown callee @exits
```

**Impact**: CRITICAL - Cannot use array fields in classes effectively. This breaks the OOP model for complex data
structures.

**Workaround**: None effective - cannot use array fields in class methods

---

## BUG-060: Cannot call methods on class objects passed as SUB/FUNCTION parameters

**Status**: RESOLVED
**Discovered**: 2025-11-15 during adventure game stress test
**Severity**: CRITICAL

**Description**:
When a class object is passed as a parameter to a SUB or FUNCTION, attempting to call methods on that object causes IL
generation error: "unknown callee @METHODNAME"

**Test Case**:

```basic
CLASS Foo
    DIM value AS INTEGER

    SUB SetValue(v AS INTEGER)
        value = v
    END SUB
END CLASS

SUB TestSub(f AS Foo)
    f.SetValue(42)  ' ERROR HERE
END SUB

DIM obj AS Foo
obj = NEW Foo()
TestSub(obj)
```

**Error**:

```
error: TESTSUB:entry_TESTSUB: call %t4 42: unknown callee @SETVALUE
```

**Impact**: CRITICAL - Cannot pass class objects to procedures and call their methods. This severely limits code
organization and OOP design patterns.

**Workaround**: Keep all logic in main program scope, don't use helper SUBs/FUNCTIONs with class parameters

---

## BUG-061: Cannot assign class field value to local variable

**Status**: RESOLVED
**Discovered**: 2025-11-15 during adventure game stress test
**Severity**: CRITICAL

**Description**:
Attempting to assign a class field value to a local variable causes IL generation error: "call arg type mismatch"

**Test Case**:

```basic
CLASS Foo
    DIM value AS INTEGER
END CLASS

DIM x AS INTEGER
DIM obj AS Foo
obj = NEW Foo()
obj.value = 42
x = obj.value  ' ERROR HERE
```

**Error**:

```
error: main:obj_assign_cont1: call %t9: call arg type mismatch
```

**Impact**: CRITICAL REGRESSION - Cannot read class field values into variables. This is a fundamental OOP operation
that was working before. Completely breaks ability to use class data in calculations.

**Workaround**: None - this is a fundamental operation

**Note**: This might be related to recent BUG-056 array field fixes that broke field access

---

## Notes:

- COLOR is a reserved word (cannot use as parameter name)
- PRINT semicolon syntax not supported, use string concatenation instead
