' Comprehensive Test 1: Data Structures and Algorithms
' Tests: arrays, nested loops, functions, sorting, searching, arithmetic
' Expected to produce deterministic output for both VM and native execution

DIM arr(10) AS INTEGER
DIM sorted(10) AS INTEGER
DIM i AS INTEGER
DIM j AS INTEGER
DIM temp AS INTEGER
DIM found AS INTEGER
DIM sum AS INTEGER
DIM product AS DOUBLE

' Initialize array with test values (deterministic pattern)
PRINT "=== Array Initialization ==="
FOR i = 0 TO 9
    arr(i) = (i * 7 + 3) MOD 20
    PRINT arr(i)
NEXT i

' Copy to sorted array
FOR i = 0 TO 9
    sorted(i) = arr(i)
NEXT i

' Bubble sort implementation
PRINT "=== Bubble Sort ==="
FOR i = 0 TO 8
    FOR j = 0 TO 8 - i
        IF sorted(j) > sorted(j + 1) THEN
            temp = sorted(j)
            sorted(j) = sorted(j + 1)
            sorted(j + 1) = temp
        END IF
    NEXT j
NEXT i

FOR i = 0 TO 9
    PRINT sorted(i)
NEXT i

' Linear search
PRINT "=== Linear Search ==="
found = LinearSearch(arr, 10, 10)
PRINT found
found = LinearSearch(arr, 10, 99)
PRINT found

' Sum and product calculations
PRINT "=== Sum and Product ==="
sum = 0
product = 1.0
FOR i = 0 TO 9
    sum = sum + arr(i)
    IF arr(i) > 0 THEN
        product = product * arr(i)
    END IF
NEXT i
PRINT sum
PRINT product

' Min and max
PRINT "=== Min and Max ==="
PRINT FindMin(arr, 10)
PRINT FindMax(arr, 10)

' Reverse array in place
PRINT "=== Reverse Array ==="
FOR i = 0 TO 4
    temp = arr(i)
    arr(i) = arr(9 - i)
    arr(9 - i) = temp
NEXT i
FOR i = 0 TO 9
    PRINT arr(i)
NEXT i

' Count occurrences
PRINT "=== Count Values ==="
PRINT CountValue(arr, 10, 10)
PRINT CountValue(arr, 10, 3)

' Two-dimensional simulation with 1D array
PRINT "=== 2D Grid Simulation ==="
DIM grid(25) AS INTEGER  ' 5x5 grid
DIM row AS INTEGER
DIM col AS INTEGER

FOR row = 0 TO 4
    FOR col = 0 TO 4
        grid(row * 5 + col) = row + col
    NEXT col
NEXT row

' Print diagonal
FOR i = 0 TO 4
    PRINT grid(i * 5 + i)
NEXT i

' Calculate sum of each row
FOR row = 0 TO 4
    sum = 0
    FOR col = 0 TO 4
        sum = sum + grid(row * 5 + col)
    NEXT col
    PRINT sum
NEXT row

' Factorial via recursion
PRINT "=== Factorial ==="
FOR i = 0 TO 7
    PRINT Factorial(i)
NEXT i

' Fibonacci via recursion
PRINT "=== Fibonacci ==="
FOR i = 0 TO 10
    PRINT Fibonacci(i)
NEXT i

PRINT "=== Test Complete ==="

FUNCTION LinearSearch(searchArr() AS INTEGER, size AS INTEGER, target AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    FOR idx = 0 TO size - 1
        IF searchArr(idx) = target THEN
            RETURN idx
        END IF
    NEXT idx
    RETURN -1
END FUNCTION

FUNCTION FindMin(searchArr() AS INTEGER, size AS INTEGER) AS INTEGER
    DIM minVal AS INTEGER
    DIM idx AS INTEGER
    minVal = searchArr(0)
    FOR idx = 1 TO size - 1
        IF searchArr(idx) < minVal THEN
            minVal = searchArr(idx)
        END IF
    NEXT idx
    RETURN minVal
END FUNCTION

FUNCTION FindMax(searchArr() AS INTEGER, size AS INTEGER) AS INTEGER
    DIM maxVal AS INTEGER
    DIM idx AS INTEGER
    maxVal = searchArr(0)
    FOR idx = 1 TO size - 1
        IF searchArr(idx) > maxVal THEN
            maxVal = searchArr(idx)
        END IF
    NEXT idx
    RETURN maxVal
END FUNCTION

FUNCTION CountValue(searchArr() AS INTEGER, size AS INTEGER, target AS INTEGER) AS INTEGER
    DIM count AS INTEGER
    DIM idx AS INTEGER
    count = 0
    FOR idx = 0 TO size - 1
        IF searchArr(idx) = target THEN
            count = count + 1
        END IF
    NEXT idx
    RETURN count
END FUNCTION

FUNCTION Factorial(n AS INTEGER) AS INTEGER
    IF n <= 1 THEN
        RETURN 1
    END IF
    RETURN n * Factorial(n - 1)
END FUNCTION

FUNCTION Fibonacci(n AS INTEGER) AS INTEGER
    IF n <= 0 THEN
        RETURN 0
    END IF
    IF n = 1 THEN
        RETURN 1
    END IF
    RETURN Fibonacci(n - 1) + Fibonacci(n - 2)
END FUNCTION
