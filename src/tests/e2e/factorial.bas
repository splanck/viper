' File: tests/e2e/factorial.bas
' Purpose: Exercise recursion via a factorial function.
' Invariants: FACT returns 1 when N <= 1 and prints 10! = 3628800.
' Ownership: tests/e2e harness; invoked by CTest.
' Links: docs/examples.md#factorial

' recursive factorial
FUNCTION FACT(N)
1 IF N = 0 THEN RETURN 1
2 RETURN N * FACT(N - 1)
END FUNCTION
10 PRINT FACT(10)
20 END
