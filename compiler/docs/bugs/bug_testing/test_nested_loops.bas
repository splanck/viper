REM ╔════════════════════════════════════════════════════════╗
REM ║     NESTED LOOPS AND CONTROL FLOW TEST                ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         NESTED LOOPS STRESS TEST                       ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Test 1: Simple nested FOR loops
PRINT "Test 1: Nested FOR Loops (Multiplication Table)"
DIM row AS INTEGER
DIM col AS INTEGER

FOR row = 1 TO 5
    PRINT "Row "; row; ": ";
    FOR col = 1 TO 5
        DIM product AS INTEGER
        product = row * col
        PRINT product; " ";
    NEXT col
    PRINT
NEXT row
PRINT

REM Test 2: Triple nested loops
PRINT "Test 2: Triple Nested Loops"
DIM x AS INTEGER
DIM y AS INTEGER
DIM z AS INTEGER
DIM count AS INTEGER

count = 0
FOR x = 1 TO 3
    FOR y = 1 TO 3
        FOR z = 1 TO 3
            count = count + 1
        NEXT z
    NEXT y
NEXT x

PRINT "  Total iterations: "; count
IF count = 27 THEN
    PRINT "  ✓ Correct (3 x 3 x 3 = 27)"
ELSE
    PRINT "  ✗ FAILED: Expected 27"
END IF
PRINT

REM Test 3: Nested WHILE loops
PRINT "Test 3: Nested WHILE Loops"
DIM outer AS INTEGER
DIM inner AS INTEGER

outer = 1
WHILE outer <= 3
    PRINT "  Outer "; outer; ": ";
    inner = 1
    WHILE inner <= 4
        PRINT inner; " ";
        inner = inner + 1
    WEND
    PRINT
    outer = outer + 1
WEND
PRINT

REM Test 4: FOR inside WHILE
PRINT "Test 4: FOR Inside WHILE"
DIM stage AS INTEGER
DIM counter AS INTEGER

stage = 1
WHILE stage <= 3
    PRINT "  Stage "; stage; ": ";
    FOR counter = 1 TO stage
        PRINT "*";
    NEXT counter
    PRINT
    stage = stage + 1
WEND
PRINT

REM Test 5: Complex nested conditions
PRINT "Test 5: Nested IF Statements"
DIM a AS INTEGER
DIM b AS INTEGER
DIM c AS INTEGER

FOR a = 1 TO 3
    FOR b = 1 TO 3
        c = a + b
        IF a = 1 THEN
            IF b = 1 THEN
                PRINT "  (1,1): "; c
            ELSEIF b = 2 THEN
                PRINT "  (1,2): "; c
            END IF
        ELSEIF a = 2 THEN
            IF b = 1 THEN
                PRINT "  (2,1): "; c
            ELSEIF b = 2 THEN
                PRINT "  (2,2): "; c
            END IF
        END IF
    NEXT b
NEXT a
PRINT

REM Test 6: Pattern generation with nested loops
PRINT "Test 6: Pattern Generation"
DIM line AS INTEGER
DIM spaces AS INTEGER
DIM stars AS INTEGER

PRINT "  Triangle:"
FOR line = 1 TO 5
    PRINT "    ";
    FOR spaces = 5 TO line + 1 STEP -1
        PRINT " ";
    NEXT spaces
    FOR stars = 1 TO line
        PRINT "*";
    NEXT stars
    PRINT
NEXT line
PRINT

REM Test 7: Early exit from nested loops
PRINT "Test 7: Break Simulation with Flag"
DIM found AS INTEGER
DIM search AS INTEGER

found = 0
FOR x = 1 TO 5
    IF found = 0 THEN
        FOR y = 1 TO 5
            IF found = 0 THEN
                DIM value AS INTEGER
                value = x * y
                IF value = 12 THEN
                    PRINT "  Found 12 at ("; x; ","; y; ")"
                    found = 1
                END IF
            END IF
        NEXT y
    END IF
NEXT x
PRINT

REM Test 8: Accumulator in nested loops
PRINT "Test 8: Sum Accumulator"
DIM sum AS INTEGER
sum = 0

FOR x = 1 TO 4
    FOR y = 1 TO 4
        sum = sum + (x + y)
    NEXT y
NEXT x

PRINT "  Total sum: "; sum
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  NESTED LOOPS TEST COMPLETE!                           ║"
PRINT "╚════════════════════════════════════════════════════════╝"
