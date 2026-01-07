REM Test FOR loop variable behavior

PRINT "Test 1: Simple FOR loop"
DIM i AS INTEGER
FOR i = 1 TO 5
    PRINT "i = "; i
NEXT i
PRINT

PRINT "Test 2: FOR loop with variable in calculations"
DIM sum AS INTEGER
sum = 0
FOR i = 1 TO 5
    sum = sum + i
    PRINT "i = "; i; ", sum = "; sum
NEXT i
PRINT "Final sum: "; sum
PRINT

PRINT "Test 3: FOR loop variable passed to expression"
FOR i = 1 TO 5
    DIM doubled AS INTEGER
    doubled = i * 2
    PRINT "i = "; i; ", doubled = "; doubled
NEXT i
PRINT

PRINT "Test 4: Nested loops"
DIM j AS INTEGER
FOR i = 1 TO 3
    PRINT "Outer i = "; i
    FOR j = 1 TO 3
        PRINT "  Inner j = "; j; ", i = "; i
    NEXT j
NEXT i
