REM BUG-110: Stack overflow in string concatenation loops
REM Minimal reproduction

PRINT "Demonstrating stack overflow bug..."
PRINT

REM This crashes after ~60-90 total concatenations
REM Each concatenation generates 2 allocas inside the loop
REM Stack never gets reclaimed until function returns

DIM s1 AS STRING
DIM s2 AS STRING
DIM s3 AS STRING
DIM i AS INTEGER

PRINT "Building s1 (30 iterations)..."
s1 = ""
FOR i = 1 TO 30
    s1 = s1 + "A"
NEXT i
PRINT "s1 done: "; LEN(s1)

PRINT "Building s2 (30 iterations)..."
s2 = ""
FOR i = 1 TO 30
    s2 = s2 + "B"
NEXT i
PRINT "s2 done: "; LEN(s2)

PRINT "Building s3 (30 iterations)..."
s3 = ""
FOR i = 1 TO 30
    s3 = s3 + "C"  ' Crashes here on iteration ~1-5
NEXT i
PRINT "s3 done: "; LEN(s3)

PRINT "If you see this, the bug is fixed!"
