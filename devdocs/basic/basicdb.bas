REM ============================================================================
REM BasicDB - Database Management System in BASIC
REM Version 0.6 - Array-backed Edition
REM ============================================================================
REM
REM This version replaces fixed per-record globals with parallel arrays now that
REM string arrays (BUG-033) are resolved. Public API remains stable.
REM
REM ============================================================================

REM Global Database State
DIM DB_COUNT AS INTEGER
DIM DB_NEXT_ID AS INTEGER
DIM DB_MAXREC AS INTEGER

REM Parallel arrays storing fields for each record (0..9)
DIM REC_ID(10) AS INTEGER
DIM REC_NAME$(10)
DIM REC_EMAIL$(10)
DIM REC_AGE(10) AS INTEGER
DIM REC_ACTIVE(10) AS INTEGER

REM ============================================================================
REM Initialize Database
REM ============================================================================

SUB DB_Initialize()
    DB_MAXREC = 10
    DB_COUNT = 0
    DB_NEXT_ID = 1
    PRINT "DEBUG: Initialized DB_COUNT=" + STR$(DB_COUNT) + " MAXREC=" + STR$(DB_MAXREC)
END SUB

REM ============================================================================
REM CRUD Operations - CREATE
REM ============================================================================

FUNCTION DB_AddRecord(pName AS STRING, pEmail AS STRING, pAge AS INTEGER) AS INTEGER
    PRINT "DEBUG: DB_AddRecord called, DB_COUNT=" + STR$(DB_COUNT) + " MAXREC=" + STR$(DB_MAXREC)
    IF DB_COUNT >= DB_MAXREC THEN
        PRINT "ERROR: Database full"
        RETURN -1
    END IF

    DIM newId AS INTEGER
    newId = DB_NEXT_ID
    DB_NEXT_ID = DB_NEXT_ID + 1

    REM Next available slot = DB_COUNT (0-based)
    DIM i AS INTEGER
    i = DB_COUNT
    REC_ID(i) = newId
    REC_NAME$(i) = pName
    REC_EMAIL$(i) = pEmail
    REC_AGE(i) = pAge
    REC_ACTIVE(i) = 1
    DB_COUNT = DB_COUNT + 1
    RETURN newId
END FUNCTION

REM ============================================================================
REM CRUD Operations - READ
REM ============================================================================

SUB DB_PrintRecord(slot AS INTEGER)
    IF slot < 1 OR slot > DB_COUNT THEN
        PRINT "ERROR: Invalid slot"
        RETURN
    END IF

    DIM idx AS INTEGER
    idx = slot - 1
    PRINT "ID: " + STR$(REC_ID(idx)) + " | Name: " + REC_NAME$(idx) + " | Age: " + STR$(REC_AGE(idx))
END SUB

SUB DB_PrintAll()
    PRINT "=== Database (Count: " + STR$(DB_COUNT) + ") ==="
    IF DB_COUNT = 0 THEN
        PRINT "  (empty)"
    ELSE
        DIM i AS INTEGER
        FOR i = 1 TO DB_COUNT
            DB_PrintRecord(i)
        NEXT i
    END IF
    PRINT ""
END SUB

REM ============================================================================
REM Query helpers (NEW in v0.6)
REM ============================================================================

REM Note: String comparison in loops has issues with rt_str_eq extern
REM declaration. Simplified for now.

REM ============================================================================
REM TEST CODE
REM ============================================================================

PRINT "============================================================"
PRINT "           BASICDB - Version 0.6 (Array Edition)"
PRINT "============================================================"
PRINT ""

DB_Initialize()

PRINT "Test 1: Adding Records"
PRINT "======================"
DIM id1 AS INTEGER
DIM id2 AS INTEGER
DIM id3 AS INTEGER

id1 = DB_AddRecord("Alice Smith", "alice@example.com", 28)
PRINT "Added Alice (ID: " + STR$(id1) + ")"

id2 = DB_AddRecord("Bob Johnson", "bob@example.com", 35)
PRINT "Added Bob (ID: " + STR$(id2) + ")"

id3 = DB_AddRecord("Carol Williams", "carol@example.com", 42)
PRINT "Added Carol (ID: " + STR$(id3) + ")"
PRINT ""

PRINT "Test 2: Display All"
PRINT "==================="
DB_PrintAll()

PRINT "Test 3: Direct Access"
PRINT "====================="
PRINT "Accessing slot 2 directly:"
DB_PrintRecord(2)
PRINT ""

PRINT "============================================================"
PRINT "Tests Completed!"
PRINT "============================================================"

END
