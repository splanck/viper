REM Stress test: Large arrays (fixed overflow issue)
PRINT "=== Array and Loop Stress Test ==="

CONST SIZE% = 100
DIM arr%(SIZE%)

PRINT "Initializing array of "; SIZE%; " elements..."
FOR i% = 0 TO SIZE%
    arr%(i%) = i% - 50
NEXT i%

PRINT "Sorting array using bubble sort with SWAP..."
n% = SIZE%
FOR i% = 0 TO n% - 1
    FOR j% = 0 TO n% - i% - 1
        IF arr%(j%) > arr%(j% + 1) THEN
            SWAP arr%(j%), arr%(j% + 1)
        END IF
    NEXT j%
NEXT i%

PRINT "First 10 sorted elements:"
FOR i% = 0 TO 9
    PRINT arr%(i%);
NEXT i%
PRINT ""

PRINT "Last 10 sorted elements:"
FOR i% = 91 TO 100
    PRINT arr%(i%);
NEXT i%
PRINT ""

PRINT ""
PRINT "=== SGN Analysis of Array ==="
negative% = 0
zero% = 0
positive% = 0

FOR i% = 0 TO SIZE%
    sign% = SGN(arr%(i%))
    IF sign% < 0 THEN negative% = negative% + 1
    IF sign% = 0 THEN zero% = zero% + 1
    IF sign% > 0 THEN positive% = positive% + 1
NEXT i%

PRINT "Negative values: "; negative%
PRINT "Zero values: "; zero%
PRINT "Positive values: "; positive%

PRINT ""
PRINT "=== Math Functions on Small Values ==="
FOR i% = 1 TO 10
    val# = i% * 0.5
    PRINT "x="; val#; " EXP(x)="; EXP(val#); " LOG(EXP(x))="; LOG(EXP(val#))
NEXT i%

PRINT ""
PRINT "=== Trigonometry Table ==="
CONST PI# = 3.14159265
FOR angle% = 0 TO 90 STEP 15
    radian# = angle% * PI# / 180
    sin# = SIN(radian#)
    cos# = COS(radian#)
    tan# = TAN(radian#)
    PRINT angle%; "°: SIN="; sin#; " COS="; cos#; " TAN="; tan#
NEXT angle%

PRINT ""
PRINT "Stress test completed!"
