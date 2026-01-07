' File: examples/basic/fact.bas
' Purpose: Compute factorial recursively.
' Links: docs/examples.md#factorial

' recursive factorial
FUNCTION FACT(N)
1 IF N = 0 THEN RETURN 1
2 RETURN N * FACT(N - 1)
END FUNCTION
10 PRINT FACT(10)
20 END
