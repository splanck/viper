<!--
File: docs/basic-ref.md
Purpose: BASIC language reference and examples.
-->

# BASIC Language Reference

## Comments

Single-line comments begin with an apostrophe `'` or the keyword `REM`.
`REM` is case-insensitive and must appear at the start of a line or after
whitespace. Both forms consume characters until the end of the line.

```basic
' Using apostrophe
REM Using REM
10 PRINT "HI" ' trailing comment
20 REM trailing REM comment
```

## Program Structure

A BASIC source file may begin with zero or more `FUNCTION` or `SUB`
declarations followed by top-level statements that form the main program. The
procedure section and the main section are both optional, and procedures may be
invoked before or after their textual definitions.

## Array Parameters ByRef

Array parameters in `FUNCTION` and `SUB` declarations are passed by reference. The caller must supply an array variable declared with `DIM`; expressions or indexed elements are rejected.

```basic
10 SUB S(X())
20 END SUB
30 DIM A(10), B(10)
40 LET Z = S(B)       ' OK
50 LET Z = S(A(1))    ' error: argument 1 to S must be an array variable (ByRef)
60 LET Z = S(A + 0)   ' error: argument 1 to S must be an array variable (ByRef)
```

Only bare array names may be passed; expressions create temporaries and are rejected.
