REM Test file I/O with new math functions
PRINT "=== Testing File I/O with Math Functions ==="

DIM filename$ AS STRING
filename$ = "/tmp/math_results.txt"

REM Write math results to file
OPEN filename$ FOR OUTPUT AS #1
PRINT #1, "Mathematical Analysis Results"
PRINT #1, ""

REM Test various numbers
DIM testVals(5)
testVals(0) = -100
testVals(1) = -10
testVals(2) = 0
testVals(3) = 10
testVals(4) = 100

FOR i = 0 TO 4
    val = testVals(i)
    PRINT #1, "Value: " + STR$(val)
    PRINT #1, "  SGN: " + STR$(SGN(val))
    PRINT #1, "  SIN: " + STR$(SIN(val))
    PRINT #1, "  COS: " + STR$(COS(val))
    PRINT #1, ""
NEXT i

CLOSE #1
PRINT "Results written to file"

PRINT ""
PRINT "=== Reading results back ==="
OPEN filename$ FOR INPUT AS #1
DIM line$ AS STRING
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    PRINT line$
LOOP
CLOSE #1

PRINT ""
PRINT "File I/O with math functions works!"
