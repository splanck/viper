REM ============================================================================
REM BasicDB - Comprehensive Database Management System in BASIC
REM Version 0.5 - Fixed Variables Edition (Array Workaround)
REM ============================================================================
REM
REM Due to critical bugs in BASIC compiler:
REM - BUG-NEW-005: Cannot use arrays of objects
REM - BUG-NEW-006: "LINE" is a reserved keyword
REM - BUG-NEW-007: STRING arrays do not work at all
REM
REM This version uses individual variables for each record field.
REM Maximum capacity: 10 records
REM
REM ============================================================================

REM Global Database State
DIM DB_COUNT AS INTEGER
DIM DB_NEXT_ID AS INTEGER

REM Record 1 Fields
DIM REC1_ID AS INTEGER
DIM REC1_NAME AS STRING
DIM REC1_EMAIL AS STRING
DIM REC1_AGE AS INTEGER
DIM REC1_ACTIVE AS INTEGER

REM Record 2 Fields
DIM REC2_ID AS INTEGER
DIM REC2_NAME AS STRING
DIM REC2_EMAIL AS STRING
DIM REC2_AGE AS INTEGER
DIM REC2_ACTIVE AS INTEGER

REM Record 3 Fields
DIM REC3_ID AS INTEGER
DIM REC3_NAME AS STRING
DIM REC3_EMAIL AS STRING
DIM REC3_AGE AS INTEGER
DIM REC3_ACTIVE AS INTEGER

REM Record 4 Fields
DIM REC4_ID AS INTEGER
DIM REC4_NAME AS STRING
DIM REC4_EMAIL AS STRING
DIM REC4_AGE AS INTEGER
DIM REC4_ACTIVE AS INTEGER

REM Record 5 Fields
DIM REC5_ID AS INTEGER
DIM REC5_NAME AS STRING
DIM REC5_EMAIL AS STRING
DIM REC5_AGE AS INTEGER
DIM REC5_ACTIVE AS INTEGER

REM Record 6 Fields
DIM REC6_ID AS INTEGER
DIM REC6_NAME AS STRING
DIM REC6_EMAIL AS STRING
DIM REC6_AGE AS INTEGER
DIM REC6_ACTIVE AS INTEGER

REM Record 7 Fields
DIM REC7_ID AS INTEGER
DIM REC7_NAME AS STRING
DIM REC7_EMAIL AS STRING
DIM REC7_AGE AS INTEGER
DIM REC7_ACTIVE AS INTEGER

REM Record 8 Fields
DIM REC8_ID AS INTEGER
DIM REC8_NAME AS STRING
DIM REC8_EMAIL AS STRING
DIM REC8_AGE AS INTEGER
DIM REC8_ACTIVE AS INTEGER

REM Record 9 Fields
DIM REC9_ID AS INTEGER
DIM REC9_NAME AS STRING
DIM REC9_EMAIL AS STRING
DIM REC9_AGE AS INTEGER
DIM REC9_ACTIVE AS INTEGER

REM Record 10 Fields
DIM REC10_ID AS INTEGER
DIM REC10_NAME AS STRING
DIM REC10_EMAIL AS STRING
DIM REC10_AGE AS INTEGER
DIM REC10_ACTIVE AS INTEGER

REM ============================================================================
REM Initialize Database
REM ============================================================================

SUB DB_Initialize()
    DB_COUNT = 0
    DB_NEXT_ID = 1

    REM Initialize all records to empty/inactive
    REC1_ID = 0
    REC1_NAME = ""
    REC1_EMAIL = ""
    REC1_AGE = 0
    REC1_ACTIVE = 0

    REC2_ID = 0
    REC2_NAME = ""
    REC2_EMAIL = ""
    REC2_AGE = 0
    REC2_ACTIVE = 0

    REC3_ID = 0
    REC3_NAME = ""
    REC3_EMAIL = ""
    REC3_AGE = 0
    REC3_ACTIVE = 0

    REC4_ID = 0
    REC4_NAME = ""
    REC4_EMAIL = ""
    REC4_AGE = 0
    REC4_ACTIVE = 0

    REC5_ID = 0
    REC5_NAME = ""
    REC5_EMAIL = ""
    REC5_AGE = 0
    REC5_ACTIVE = 0

    REC6_ID = 0
    REC6_NAME = ""
    REC6_EMAIL = ""
    REC6_AGE = 0
    REC6_ACTIVE = 0

    REC7_ID = 0
    REC7_NAME = ""
    REC7_EMAIL = ""
    REC7_AGE = 0
    REC7_ACTIVE = 0

    REC8_ID = 0
    REC8_NAME = ""
    REC8_EMAIL = ""
    REC8_AGE = 0
    REC8_ACTIVE = 0

    REC9_ID = 0
    REC9_NAME = ""
    REC9_EMAIL = ""
    REC9_AGE = 0
    REC9_ACTIVE = 0

    REC10_ID = 0
    REC10_NAME = ""
    REC10_EMAIL = ""
    REC10_AGE = 0
    REC10_ACTIVE = 0
END SUB

REM ============================================================================
REM CRUD Operations - CREATE
REM ============================================================================

FUNCTION DB_AddRecord(pName AS STRING, pEmail AS STRING, pAge AS INTEGER) AS INTEGER
    IF DB_COUNT >= 10 THEN
        PRINT "ERROR: Database full (maximum 10 records)"
        RETURN -1
    END IF

    DIM newId AS INTEGER
    newId = DB_NEXT_ID
    DB_NEXT_ID = DB_NEXT_ID + 1

    REM Find next available slot and populate it
    IF DB_COUNT = 0 THEN
        REC1_ID = newId
        REC1_NAME = pName
        REC1_EMAIL = pEmail
        REC1_AGE = pAge
        REC1_ACTIVE = 1
        DB_COUNT = 1
        RETURN newId
    END IF

    IF DB_COUNT = 1 THEN
        REC2_ID = newId
        REC2_NAME = pName
        REC2_EMAIL = pEmail
        REC2_AGE = pAge
        REC2_ACTIVE = 1
        DB_COUNT = 2
        RETURN newId
    END IF

    IF DB_COUNT = 2 THEN
        REC3_ID = newId
        REC3_NAME = pName
        REC3_EMAIL = pEmail
        REC3_AGE = pAge
        REC3_ACTIVE = 1
        DB_COUNT = 3
        RETURN newId
    END IF

    IF DB_COUNT = 3 THEN
        REC4_ID = newId
        REC4_NAME = pName
        REC4_EMAIL = pEmail
        REC4_AGE = pAge
        REC4_ACTIVE = 1
        DB_COUNT = 4
        RETURN newId
    END IF

    IF DB_COUNT = 4 THEN
        REC5_ID = newId
        REC5_NAME = pName
        REC5_EMAIL = pEmail
        REC5_AGE = pAge
        REC5_ACTIVE = 1
        DB_COUNT = 5
        RETURN newId
    END IF

    IF DB_COUNT = 5 THEN
        REC6_ID = newId
        REC6_NAME = pName
        REC6_EMAIL = pEmail
        REC6_AGE = pAge
        REC6_ACTIVE = 1
        DB_COUNT = 6
        RETURN newId
    END IF

    IF DB_COUNT = 6 THEN
        REC7_ID = newId
        REC7_NAME = pName
        REC7_EMAIL = pEmail
        REC7_AGE = pAge
        REC7_ACTIVE = 1
        DB_COUNT = 7
        RETURN newId
    END IF

    IF DB_COUNT = 7 THEN
        REC8_ID = newId
        REC8_NAME = pName
        REC8_EMAIL = pEmail
        REC8_AGE = pAge
        REC8_ACTIVE = 1
        DB_COUNT = 8
        RETURN newId
    END IF

    IF DB_COUNT = 8 THEN
        REC9_ID = newId
        REC9_NAME = pName
        REC9_EMAIL = pEmail
        REC9_AGE = pAge
        REC9_ACTIVE = 1
        DB_COUNT = 9
        RETURN newId
    END IF

    IF DB_COUNT = 9 THEN
        REC10_ID = newId
        REC10_NAME = pName
        REC10_EMAIL = pEmail
        REC10_AGE = pAge
        REC10_ACTIVE = 1
        DB_COUNT = 10
        RETURN newId
    END IF

    RETURN -1
END FUNCTION

REM ============================================================================
REM CRUD Operations - READ
REM ============================================================================

SUB DB_PrintRecord(slot AS INTEGER)
    IF slot < 1 OR slot > DB_COUNT THEN
        PRINT "ERROR: Invalid record slot"
        RETURN
    END IF

    DIM output AS STRING

    IF slot = 1 THEN
        output = "ID: " + STR$(REC1_ID)
        output = output + " | Name: " + REC1_NAME
        output = output + " | Email: " + REC1_EMAIL
        output = output + " | Age: " + STR$(REC1_AGE)
        IF REC1_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 2 THEN
        output = "ID: " + STR$(REC2_ID)
        output = output + " | Name: " + REC2_NAME
        output = output + " | Email: " + REC2_EMAIL
        output = output + " | Age: " + STR$(REC2_AGE)
        IF REC2_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 3 THEN
        output = "ID: " + STR$(REC3_ID)
        output = output + " | Name: " + REC3_NAME
        output = output + " | Email: " + REC3_EMAIL
        output = output + " | Age: " + STR$(REC3_AGE)
        IF REC3_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 4 THEN
        output = "ID: " + STR$(REC4_ID)
        output = output + " | Name: " + REC4_NAME
        output = output + " | Email: " + REC4_EMAIL
        output = output + " | Age: " + STR$(REC4_AGE)
        IF REC4_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 5 THEN
        output = "ID: " + STR$(REC5_ID)
        output = output + " | Name: " + REC5_NAME
        output = output + " | Email: " + REC5_EMAIL
        output = output + " | Age: " + STR$(REC5_AGE)
        IF REC5_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 6 THEN
        output = "ID: " + STR$(REC6_ID)
        output = output + " | Name: " + REC6_NAME
        output = output + " | Email: " + REC6_EMAIL
        output = output + " | Age: " + STR$(REC6_AGE)
        IF REC6_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 7 THEN
        output = "ID: " + STR$(REC7_ID)
        output = output + " | Name: " + REC7_NAME
        output = output + " | Email: " + REC7_EMAIL
        output = output + " | Age: " + STR$(REC7_AGE)
        IF REC7_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 8 THEN
        output = "ID: " + STR$(REC8_ID)
        output = output + " | Name: " + REC8_NAME
        output = output + " | Email: " + REC8_EMAIL
        output = output + " | Age: " + STR$(REC8_AGE)
        IF REC8_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 9 THEN
        output = "ID: " + STR$(REC9_ID)
        output = output + " | Name: " + REC9_NAME
        output = output + " | Email: " + REC9_EMAIL
        output = output + " | Age: " + STR$(REC9_AGE)
        IF REC9_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF

    IF slot = 10 THEN
        output = "ID: " + STR$(REC10_ID)
        output = output + " | Name: " + REC10_NAME
        output = output + " | Email: " + REC10_EMAIL
        output = output + " | Age: " + STR$(REC10_AGE)
        IF REC10_ACTIVE = 1 THEN
            output = output + " | Status: Active"
        ELSE
            output = output + " | Status: Inactive"
        END IF
        PRINT output
        RETURN
    END IF
END SUB

SUB DB_PrintAll()
    DIM header AS STRING
    header = "=== Database Contents (Count: " + STR$(DB_COUNT) + " / Max: 10) ==="
    PRINT header
    PRINT ""

    IF DB_COUNT = 0 THEN
        PRINT "  (database is empty)"
    ELSE
        DIM i AS INTEGER
        FOR i = 1 TO DB_COUNT
            DB_PrintRecord(i)
        NEXT i
    END IF

    PRINT ""
END SUB

REM ============================================================================
REM TEST CODE
REM ============================================================================

PRINT "============================================================"
PRINT "           BASICDB - Database Management System"
PRINT "                    Version 0.5"
PRINT "         Fixed Variables Edition (Bug Workaround)"
PRINT "============================================================"
PRINT ""

REM Initialize
DB_Initialize()

REM Test 1: Add records
PRINT "Test 1: Adding Records"
PRINT "======================"
DIM id1 AS INTEGER
DIM id2 AS INTEGER
DIM id3 AS INTEGER

id1 = DB_AddRecord("Alice Smith", "alice@example.com", 28)
PRINT "Added: Alice Smith (ID: " + STR$(id1) + ")"

id2 = DB_AddRecord("Bob Johnson", "bob@example.com", 35)
PRINT "Added: Bob Johnson (ID: " + STR$(id2) + ")"

id3 = DB_AddRecord("Carol Williams", "carol@example.com", 42)
PRINT "Added: Carol Williams (ID: " + STR$(id3) + ")"

PRINT ""

REM Test 2: Display all
PRINT "Test 2: Display All Records"
PRINT "============================"
DB_PrintAll()

PRINT "============================================================"
PRINT "                Tests Completed!"
PRINT "============================================================"

END
