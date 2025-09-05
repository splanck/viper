# BASIC Array Parameters ByRef

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
