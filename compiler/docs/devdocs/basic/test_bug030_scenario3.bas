REM Test 3: SUB modifies, FUNCTION reads
DIM SHARED_VAR AS INTEGER
SHARED_VAR = 50

PRINT "=== Test 3: SUB modifies, then FUNCTION reads ==="
PRINT "Main initial: SHARED_VAR = " + STR$(SHARED_VAR)

SUB SetValue()
    PRINT "SUB before: SHARED_VAR = " + STR$(SHARED_VAR)
    SHARED_VAR = 200
    PRINT "SUB after: SHARED_VAR = " + STR$(SHARED_VAR)
END SUB

FUNCTION GetValue() AS INTEGER
    PRINT "FUNCTION reads: SHARED_VAR = " + STR$(SHARED_VAR)
    RETURN SHARED_VAR
END FUNCTION

SetValue()
PRINT "Main after SUB: SHARED_VAR = " + STR$(SHARED_VAR)

DIM result AS INTEGER
result = GetValue()
PRINT "FUNCTION returned: " + STR$(result)
PRINT "Main final: SHARED_VAR = " + STR$(SHARED_VAR)
PRINT ""

END
