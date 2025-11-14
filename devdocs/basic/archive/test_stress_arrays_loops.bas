REM Stress test: Large arrays and intensive loops
PRINT "=== Array and Loop Stress Test ==="

CONST SIZE% = 100
DIM arr%(SIZE%)
DIM results#(SIZE%)

PRINT "Initializing array of "; SIZE%; " elements..."
FOR i% = 0 TO SIZE%
    arr%(i%) = i% - 50
NEXT i%

PRINT "Computing SGN, EXP, and LOG for all elements..."
FOR i% = 0 TO SIZE%
    val% = arr%(i%)
    sign% = SGN(val%)
    
    IF sign% <> 0 THEN
        results#(i%) = EXP(ABS(val%)) * sign%
    ELSE
        results#(i%) = 0
    END IF
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
PRINT "Computing trigonometric values..."
FOR angle% = 0 TO 90 STEP 15
    radian# = angle% * 3.14159 / 180
    PRINT "Angle: "; angle%; "° - SIN: "; SIN(radian#); " COS: "; COS(radian#); " TAN: "; TAN(radian#)
NEXT angle%

PRINT ""
PRINT "Stress test completed successfully!"
PRINT "Operations performed: array init, math functions, sorting, trig"
