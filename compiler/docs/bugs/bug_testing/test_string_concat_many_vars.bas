REM Test many string variables at module level

PRINT "Testing many string variables..."
PRINT

DIM s1 AS STRING
DIM s2 AS STRING
DIM s3 AS STRING
DIM s4 AS STRING
DIM s5 AS STRING
DIM i AS INTEGER

s1 = ""
FOR i = 1 TO 30
    s1 = s1 + "A"
NEXT i
PRINT "s1: "; LEN(s1)

s2 = ""
FOR i = 1 TO 30
    s2 = s2 + "B"
NEXT i
PRINT "s2: "; LEN(s2)

s3 = ""
FOR i = 1 TO 30
    s3 = s3 + "C"
NEXT i
PRINT "s3: "; LEN(s3)

s4 = ""
FOR i = 1 TO 30
    s4 = s4 + "D"
NEXT i
PRINT "s4: "; LEN(s4)

s5 = ""
FOR i = 1 TO 30
    s5 = s5 + "E"
NEXT i
PRINT "s5: "; LEN(s5)

PRINT "Success!"
