' File: tests/basic/lex/ident_with_underscore.bas
' Purpose: Ensure identifiers with underscores retain suffixes.

10 FUNCTION FOO_BAR%(N%) : RETURN N%*2 : END FUNCTION
20 PRINT FOO_BAR%(7)
30 END
