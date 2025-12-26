REM Test string concatenation limits to find stack overflow threshold

PRINT "Testing string concatenation in loops..."
PRINT

REM Test 1: 10 iterations
PRINT "Test 1: 10 iterations"
DIM s1 AS STRING
DIM i AS INTEGER
s1 = ""
FOR i = 1 TO 10
    s1 = s1 + "X"
NEXT i
PRINT "Success: length = "; LEN(s1)
PRINT

REM Test 2: 20 iterations
PRINT "Test 2: 20 iterations"
DIM s2 AS STRING
s2 = ""
FOR i = 1 TO 20
    s2 = s2 + "X"
NEXT i
PRINT "Success: length = "; LEN(s2)
PRINT

REM Test 3: 50 iterations
PRINT "Test 3: 50 iterations"
DIM s3 AS STRING
s3 = ""
FOR i = 1 TO 50
    s3 = s3 + "X"
NEXT i
PRINT "Success: length = "; LEN(s3)
PRINT

REM Test 4: 100 iterations - This should fail
PRINT "Test 4: 100 iterations (expect failure)"
DIM s4 AS STRING
s4 = ""
FOR i = 1 TO 100
    s4 = s4 + "X"
NEXT i
PRINT "Success: length = "; LEN(s4)
