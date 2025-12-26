' File: examples/basic/fib.bas
' Purpose: Compute Fibonacci numbers recursively.
' Links: docs/examples.md#fibonacci

FUNCTION FIB(N)
1 IF N <= 1 THEN RETURN N
2 RETURN FIB(N - 1) + FIB(N - 2)
END FUNCTION
10 PRINT FIB(10)
20 END
