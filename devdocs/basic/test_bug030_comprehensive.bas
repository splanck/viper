REM ============================================================================
REM BUG-030 Comprehensive Investigation
REM Testing all scenarios of global variable access
REM ============================================================================

REM Global variables with different types
DIM G_INT AS INTEGER
DIM G_STR AS STRING
DIM G_ARR(5) AS INTEGER

REM Initialize globals at module level
G_INT = 100
G_STR = "INITIAL"
G_ARR(0) = 10
G_ARR(1) = 20

PRINT "============================================================"
PRINT "BUG-030: Global Variable Access Investigation"
PRINT "============================================================"
PRINT ""
PRINT "Initial values (set at module level):"
PRINT "  G_INT = " + STR$(G_INT)
PRINT "  G_STR = " + G_STR
PRINT "  G_ARR(0) = " + STR$(G_ARR(0))
PRINT "  G_ARR(1) = " + STR$(G_ARR(1))
PRINT ""

REM ============================================================================
REM Test 1: SUB reading globals
REM ============================================================================

SUB SubReadGlobals()
    PRINT "Test 1: SUB reading globals"
    PRINT "  In SUB - G_INT = " + STR$(G_INT)
    PRINT "  In SUB - G_STR = " + G_STR
    PRINT "  In SUB - G_ARR(0) = " + STR$(G_ARR(0))
END SUB

SubReadGlobals()
PRINT ""

REM ============================================================================
REM Test 2: SUB writing globals
REM ============================================================================

SUB SubWriteGlobals()
    PRINT "Test 2: SUB writing globals"
    G_INT = 200
    G_STR = "MODIFIED_BY_SUB"
    G_ARR(0) = 30
    PRINT "  SUB set G_INT = 200"
    PRINT "  SUB set G_STR = MODIFIED_BY_SUB"
    PRINT "  SUB set G_ARR(0) = 30"
END SUB

SubWriteGlobals()

PRINT "After SubWriteGlobals, in main:"
PRINT "  G_INT = " + STR$(G_INT)
PRINT "  G_STR = " + G_STR
PRINT "  G_ARR(0) = " + STR$(G_ARR(0))
PRINT ""

REM ============================================================================
REM Test 3: FUNCTION reading globals
REM ============================================================================

FUNCTION FuncReadInt() AS INTEGER
    PRINT "Test 3: FUNCTION reading G_INT"
    PRINT "  In FUNCTION - G_INT = " + STR$(G_INT)
    RETURN G_INT
END FUNCTION

DIM result AS INTEGER
result = FuncReadInt()
PRINT "FUNCTION returned: " + STR$(result)
PRINT "Actual G_INT in main: " + STR$(G_INT)
PRINT ""

REM ============================================================================
REM Test 4: FUNCTION writing globals
REM ============================================================================

FUNCTION FuncWriteInt() AS INTEGER
    PRINT "Test 4: FUNCTION writing G_INT"
    PRINT "  In FUNCTION before write - G_INT = " + STR$(G_INT)
    G_INT = 300
    PRINT "  FUNCTION set G_INT = 300"
    PRINT "  In FUNCTION after write - G_INT = " + STR$(G_INT)
    RETURN G_INT
END FUNCTION

result = FuncWriteInt()
PRINT "FUNCTION returned: " + STR$(result)
PRINT "Actual G_INT in main: " + STR$(G_INT)
PRINT ""

REM ============================================================================
REM Test 5: SUB calling SUB (does SUB's modification persist?)
REM ============================================================================

SUB SubSetValue()
    G_INT = 400
    PRINT "Test 5a: SubSetValue set G_INT = 400"
END SUB

SUB SubReadValue()
    PRINT "Test 5b: SubReadValue reads G_INT = " + STR$(G_INT)
END SUB

SubSetValue()
SubReadValue()
PRINT "In main after both SUBs: G_INT = " + STR$(G_INT)
PRINT ""

REM ============================================================================
REM Test 6: SUB calling FUNCTION (can FUNCTION see SUB's changes?)
REM ============================================================================

SUB SubSetForFunc()
    G_INT = 500
    PRINT "Test 6a: SubSetForFunc set G_INT = 500"
END SUB

FUNCTION FuncReadAfterSub() AS INTEGER
    PRINT "Test 6b: FuncReadAfterSub reads G_INT = " + STR$(G_INT)
    RETURN G_INT
END FUNCTION

SubSetForFunc()
result = FuncReadAfterSub()
PRINT "FUNCTION returned: " + STR$(result)
PRINT "In main: G_INT = " + STR$(G_INT)
PRINT ""

REM ============================================================================
REM Test 7: FUNCTION calling SUB (can SUB see FUNCTION's changes?)
REM ============================================================================

FUNCTION FuncSetForSub() AS INTEGER
    G_INT = 600
    PRINT "Test 7a: FuncSetForSub set G_INT = 600"
    RETURN G_INT
END FUNCTION

SUB SubReadAfterFunc()
    PRINT "Test 7b: SubReadAfterFunc reads G_INT = " + STR$(G_INT)
END SUB

result = FuncSetForSub()
SubReadAfterFunc()
PRINT "In main: G_INT = " + STR$(G_INT)
PRINT ""

REM ============================================================================
REM Test 8: String global access
REM ============================================================================

SUB SubModifyString()
    PRINT "Test 8a: SUB modifying G_STR"
    PRINT "  Before: G_STR = " + G_STR
    G_STR = "SET_BY_SUB"
    PRINT "  After: G_STR = " + G_STR
END SUB

FUNCTION FuncReadString() AS STRING
    PRINT "Test 8b: FUNCTION reading G_STR = " + G_STR
    RETURN G_STR
END FUNCTION

SubModifyString()
DIM strResult AS STRING
strResult = FuncReadString()
PRINT "FUNCTION returned: " + strResult
PRINT "In main: G_STR = " + G_STR
PRINT ""

REM ============================================================================
REM Test 9: Array global access
REM ============================================================================

SUB SubModifyArray()
    PRINT "Test 9a: SUB modifying G_ARR"
    PRINT "  Before: G_ARR(0) = " + STR$(G_ARR(0))
    G_ARR(0) = 99
    PRINT "  After: G_ARR(0) = " + STR$(G_ARR(0))
END SUB

FUNCTION FuncReadArray() AS INTEGER
    PRINT "Test 9b: FUNCTION reading G_ARR(0) = " + STR$(G_ARR(0))
    RETURN G_ARR(0)
END FUNCTION

SubModifyArray()
result = FuncReadArray()
PRINT "FUNCTION returned: " + STR$(result)
PRINT "In main: G_ARR(0) = " + STR$(G_ARR(0))
PRINT ""

PRINT "============================================================"
PRINT "Investigation Complete"
PRINT "============================================================"

END
