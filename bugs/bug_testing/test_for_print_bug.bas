REM Minimal test for FOR loop printing

PRINT "Test 1: Direct print in FOR loop"
DIM day AS INTEGER
FOR day = 1 TO 5
    PRINT "Day "; day
NEXT day
PRINT

PRINT "Test 2: Print with color"
FOR day = 1 TO 5
    COLOR 14, 0
    PRINT "Day "; day
    COLOR 15, 0
NEXT day
PRINT

PRINT "Test 3: Print in box like weather test"
FOR day = 1 TO 5
    COLOR 14, 0
    PRINT "╔════════════════════╗"
    PRINT "║ Day "; day; "            "
    PRINT "╚════════════════════╝"
    COLOR 15, 0
NEXT day
