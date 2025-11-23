REM Test basic functions

FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
    RETURN a + b
END FUNCTION

SUB PrintMessage(msg AS STRING)
    PRINT msg
END SUB

REM Main
PRINT "Testing functions..."

DIM result AS INTEGER
result = Add(5, 3)
PRINT "5 + 3 = " + STR$(result)

PrintMessage("Hello from SUB!")

PRINT "Done!"
END
