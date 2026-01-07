REM Test 4: STRING global variable
DIM MESSAGE AS STRING
MESSAGE = "Hello"

PRINT "=== Test 4: STRING global variable ==="
PRINT "Main initial: MESSAGE = " + MESSAGE

SUB ModifyString()
    PRINT "SUB before: MESSAGE = " + MESSAGE
    MESSAGE = "World"
    PRINT "SUB after: MESSAGE = " + MESSAGE
END SUB

FUNCTION GetString() AS STRING
    PRINT "FUNCTION reads: MESSAGE = " + MESSAGE
    RETURN MESSAGE
END FUNCTION

ModifyString()
PRINT "Main after SUB: MESSAGE = " + MESSAGE

DIM result AS STRING
result = GetString()
PRINT "FUNCTION returned: " + result
PRINT "Main final: MESSAGE = " + MESSAGE
PRINT ""

END
