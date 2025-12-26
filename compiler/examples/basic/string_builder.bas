' File: examples/basic/string_builder.bas
' Purpose: Demonstrate a string-returning FUNCTION.
' Links: docs/examples.md#string-builder

FUNCTION EXCL(S$)
  RETURN S$ + "!"
END FUNCTION
10 PRINT EXCL("hi")
20 END
