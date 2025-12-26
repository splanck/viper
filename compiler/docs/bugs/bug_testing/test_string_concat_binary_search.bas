REM Binary search for exact threshold

PRINT "Finding exact stack overflow threshold..."
PRINT

REM Test 25 iterations
PRINT "Test: 25 iterations"
DIM s AS STRING
DIM i AS INTEGER
s = ""
FOR i = 1 TO 25
    s = s + "X"
NEXT i
PRINT "Success: length = "; LEN(s)
PRINT

REM Test 30 iterations
PRINT "Test: 30 iterations"
s = ""
FOR i = 1 TO 30
    s = s + "X"
NEXT i
PRINT "Success: length = "; LEN(s)
PRINT

REM Test 35 iterations
PRINT "Test: 35 iterations"
s = ""
FOR i = 1 TO 35
    s = s + "X"
NEXT i
PRINT "Success: length = "; LEN(s)
PRINT

REM Test 40 iterations
PRINT "Test: 40 iterations"
s = ""
FOR i = 1 TO 40
    s = s + "X"
NEXT i
PRINT "Success: length = "; LEN(s)
PRINT

REM Test 45 iterations
PRINT "Test: 45 iterations"
s = ""
FOR i = 1 TO 45
    s = s + "X"
NEXT i
PRINT "Success: length = "; LEN(s)
