' Test: Control Flow
' Tests: IF, SELECT CASE, FOR, WHILE, DO, EXIT

' IF statement
PRINT "=== IF Statement ==="
DIM x AS INTEGER
x = 5

IF x > 10 THEN
    PRINT "x > 10"
ELSEIF x > 3 THEN
    PRINT "x > 3 but <= 10"
ELSE
    PRINT "x <= 3"
END IF

' Single-line IF
IF x = 5 THEN PRINT "x is 5"

' SELECT CASE
PRINT ""
PRINT "=== SELECT CASE ==="
DIM day AS INTEGER
day = 3

SELECT CASE day
    CASE 1
        PRINT "Monday"
    CASE 2
        PRINT "Tuesday"
    CASE 3
        PRINT "Wednesday"
    CASE 4, 5
        PRINT "Thursday or Friday"
    CASE 6 TO 7
        PRINT "Weekend"
    CASE ELSE
        PRINT "Invalid day"
END SELECT

' FOR loop
PRINT ""
PRINT "=== FOR Loop ==="
DIM i AS INTEGER
FOR i = 1 TO 5
    PRINT "  i = "; i
NEXT i

' FOR with STEP
PRINT "FOR with STEP -2:"
FOR i = 10 TO 2 STEP -2
    PRINT "  i = "; i
NEXT i

' WHILE loop
PRINT ""
PRINT "=== WHILE Loop ==="
DIM n AS INTEGER
n = 1
WHILE n <= 3
    PRINT "  n = "; n
    n = n + 1
WEND

' DO WHILE loop
PRINT ""
PRINT "=== DO WHILE Loop ==="
n = 1
DO WHILE n <= 3
    PRINT "  n = "; n
    n = n + 1
LOOP

' DO UNTIL loop
PRINT ""
PRINT "=== DO UNTIL Loop ==="
n = 1
DO UNTIL n > 3
    PRINT "  n = "; n
    n = n + 1
LOOP

' DO LOOP WHILE (post-test)
PRINT ""
PRINT "=== DO LOOP WHILE (post-test) ==="
n = 1
DO
    PRINT "  n = "; n
    n = n + 1
LOOP WHILE n <= 3

' EXIT FOR
PRINT ""
PRINT "=== EXIT FOR ==="
FOR i = 1 TO 10
    IF i = 5 THEN EXIT FOR
    PRINT "  i = "; i
NEXT i
PRINT "Exited at i = "; i

' EXIT DO
PRINT ""
PRINT "=== EXIT DO ==="
n = 1
DO
    IF n = 4 THEN EXIT DO
    PRINT "  n = "; n
    n = n + 1
LOOP
PRINT "Exited at n = "; n

' Nested loops
PRINT ""
PRINT "=== Nested Loops ==="
DIM j AS INTEGER
FOR i = 1 TO 2
    FOR j = 1 TO 2
        PRINT "  ("; i; ","; j; ")"
    NEXT j
NEXT i

PRINT "=== Control flow test complete ==="
