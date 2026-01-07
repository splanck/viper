REM Test local arrays outside of classes

PRINT "Testing local array usage..."
PRINT

REM Test 1: Simple integer array
DIM numbers(5) AS INTEGER
DIM i AS INTEGER

PRINT "Test 1: Integer array"
FOR i = 0 TO 4
    numbers(i) = i * 10
NEXT i

FOR i = 0 TO 4
    PRINT "numbers("; i; ") = "; numbers(i)
NEXT i
PRINT

REM Test 2: String array
DIM names(3) AS STRING
names(0) = "Alice"
names(1) = "Bob"
names(2) = "Charlie"

PRINT "Test 2: String array"
FOR i = 0 TO 2
    PRINT "names("; i; ") = "; names(i)
NEXT i
PRINT

REM Test 3: Array as part of game state simulation
DIM carPositions(4) AS INTEGER
PRINT "Test 3: Car positions array"
FOR i = 0 TO 3
    carPositions(i) = i * 5 + 1
    PRINT "Car "; i; " at x="; carPositions(i)
NEXT i
PRINT

PRINT "✓ Local arrays working!"
