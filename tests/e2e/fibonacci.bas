' File: tests/e2e/fibonacci.bas
' Purpose: Compute Fibonacci numbers via a recursive function.
' Invariants: FIB returns N when N <= 1 and prints FIB(10) = 55.
' Ownership: tests/e2e harness; invoked by CTest.
' Links: docs/examples.md#fibonacci

FUNCTION FIB(N)
1 IF N <= 1 THEN RETURN N
2 RETURN FIB(N - 1) + FIB(N - 2)
END FUNCTION
10 PRINT FIB(10)
20 END
