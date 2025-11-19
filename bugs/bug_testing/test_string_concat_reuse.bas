REM Test reusing same string variable vs creating new ones

PRINT "Testing string variable reuse..."
PRINT

REM Test 1: Reuse same variable for multiple concatenations
PRINT "Test 1: Reuse same variable"
DIM s AS STRING
DIM i AS INTEGER

s = ""
FOR i = 1 TO 20
    s = s + "X"
NEXT i
PRINT "First loop: "; LEN(s)

s = ""
FOR i = 1 TO 20
    s = s + "Y"
NEXT i
PRINT "Second loop: "; LEN(s)

s = ""
FOR i = 1 TO 20
    s = s + "Z"
NEXT i
PRINT "Third loop: "; LEN(s)

PRINT "Success!"
