REM Test loops with SWAP, CONST, and math functions
PRINT "=== Bubble Sort using SWAP ==="

DIM arr(5)
arr(0) = 64
arr(1) = 34
arr(2) = 25
arr(3) = 12
arr(4) = 22

PRINT "Original array:"
FOR i = 0 TO 4
    PRINT arr(i);
NEXT i
PRINT ""

REM Bubble sort
n = 5
FOR i = 0 TO n - 2
    FOR j = 0 TO n - i - 2
        IF arr(j) > arr(j + 1) THEN
            SWAP arr(j), arr(j + 1)
        END IF
    NEXT j
NEXT i

PRINT "Sorted array:"
FOR i = 0 TO 4
    PRINT arr(i);
NEXT i
PRINT ""

PRINT ""
PRINT "=== Sieve of Eratosthenes using arrays ==="
CONST MAX_NUM = 30
DIM isPrime(MAX_NUM)

REM Initialize all as prime
FOR i = 0 TO MAX_NUM
    isPrime(i) = 1
NEXT i

isPrime(0) = 0
isPrime(1) = 0

REM Sieve algorithm
FOR i = 2 TO MAX_NUM
    IF isPrime(i) = 1 THEN
        REM Mark multiples as not prime
        j = i * 2
        DO WHILE j <= MAX_NUM
            isPrime(j) = 0
            j = j + i
        LOOP
    END IF
NEXT i

PRINT "Prime numbers up to 30:"
FOR i = 2 TO MAX_NUM
    IF isPrime(i) = 1 THEN
        PRINT i;
    END IF
NEXT i
PRINT ""

PRINT ""
PRINT "=== Computing series using math functions ==="
REM Compute sum of SGN(n) * 1/n for n from -5 to 5
sum = 0
FOR n = -5 TO 5
    IF n <> 0 THEN
        term = SGN(n)
        sum = sum + term
    END IF
NEXT n
PRINT "Sum of SGN(n) for n=-5 to 5 (skip 0): "; sum

PRINT ""
PRINT "All loop tests passed!"
