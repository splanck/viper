' File: tests/e2e/nested_calls.bas
' Purpose: Demonstrates combining a string-returning FUNCTION and a SUB with side effects.
' Invariants: EXCL appends "!" to its input; PRINTDOUBLE prints twice its argument.
' Ownership: tests/e2e harness; invoked by CTest.
' Links: docs/examples.md#nested-calls

FUNCTION EXCL(S$)
  RETURN S$ + "!"
END FUNCTION

FUNCTION DOUBLE(N)
  RETURN N * 2
END FUNCTION

SUB PRINTDOUBLE(X)
  PRINT DOUBLE(X)
END SUB

10 PRINT EXCL("hi")
20 PRINTDOUBLE(21)
30 END
