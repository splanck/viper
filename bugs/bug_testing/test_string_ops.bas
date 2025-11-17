REM ╔════════════════════════════════════════════════════════╗
REM ║     STRING OPERATIONS STRESS TEST                      ║
REM ╚════════════════════════════════════════════════════════╝

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         STRING OPERATIONS TEST                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Test string assignment and comparison
DIM name1 AS STRING
DIM name2 AS STRING
DIM name3 AS STRING

name1 = "Alice"
name2 = "Bob"
name3 = "Alice"

PRINT "Test 1: String Assignment"
PRINT "  name1 = "; name1
PRINT "  name2 = "; name2
PRINT "  name3 = "; name3
PRINT

PRINT "Test 2: String Equality"
IF name1 = name3 THEN
    PRINT "  ✓ name1 = name3 (both 'Alice')"
ELSE
    PRINT "  ✗ FAILED: name1 should equal name3"
END IF

IF name1 = name2 THEN
    PRINT "  ✗ FAILED: name1 should NOT equal name2"
ELSE
    PRINT "  ✓ name1 <> name2"
END IF
PRINT

PRINT "Test 3: String Inequality"
IF name1 <> name2 THEN
    PRINT "  ✓ name1 <> name2 works"
ELSE
    PRINT "  ✗ FAILED"
END IF
PRINT

REM Test empty string
DIM empty AS STRING
empty = ""

PRINT "Test 4: Empty String"
PRINT "  empty = '"; empty; "'"
IF empty = "" THEN
    PRINT "  ✓ Empty string comparison works"
END IF
PRINT

REM Test string with numbers
DIM mixed AS STRING
mixed = "Player123"

PRINT "Test 5: String with Numbers"
PRINT "  mixed = "; mixed
PRINT

REM Test string literals in PRINT
PRINT "Test 6: String Literals"
PRINT "  'Hello, World!'"
PRINT "  'The quick brown fox jumps over the lazy dog.'"
PRINT

REM Test string comparison in loop
PRINT "Test 7: String Comparison in Loop"
DIM i AS INTEGER
DIM testNames AS STRING
DIM found AS INTEGER

found = 0
FOR i = 1 TO 5
    IF i = 1 THEN
        testNames = "Alice"
    ELSEIF i = 2 THEN
        testNames = "Bob"
    ELSEIF i = 3 THEN
        testNames = "Charlie"
    ELSEIF i = 4 THEN
        testNames = "David"
    ELSE
        testNames = "Eve"
    END IF

    PRINT "  Checking: "; testNames
    IF testNames = "Charlie" THEN
        PRINT "    ✓ Found Charlie at position "; i
        found = 1
    END IF
NEXT i

IF found = 0 THEN
    PRINT "  ✗ FAILED: Should have found Charlie"
END IF
PRINT

REM Test multiple string comparisons
PRINT "Test 8: Multiple String Comparisons"
DIM cmd AS STRING
cmd = "north"

IF cmd = "north" OR cmd = "n" THEN
    PRINT "  ✓ OR comparison works"
END IF

IF cmd = "north" AND name1 = "Alice" THEN
    PRINT "  ✓ AND comparison works"
END IF
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  STRING OPERATIONS TEST COMPLETE!                      ║"
PRINT "╚════════════════════════════════════════════════════════╝"
