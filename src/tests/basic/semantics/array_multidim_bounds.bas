' Test multi-dimensional array bounds checking
' Expects: errors for too many indices, warnings for out-of-bounds constants

DIM A(3, 4) AS INTEGER

' Valid accesses (no diagnostics)
LET A(0, 0) = 1
LET A(2, 3) = 2

' Too many indices (error B3002)
LET Y = A(1, 2, 3)

' Out of bounds on first dimension (warning B3001)
LET Z = A(5, 0)

' Out of bounds on second dimension (warning B3001)
LET W = A(0, 10)

END
