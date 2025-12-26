' Test valid multi-dimensional array access (no errors expected)

DIM A(3, 4) AS INTEGER
DIM B(2, 3, 5) AS INTEGER

' Valid 2D accesses
LET A(0, 0) = 1
LET A(2, 3) = 2
LET A(1, 2) = 3

' Valid 3D accesses
LET B(0, 0, 0) = 10
LET B(1, 2, 4) = 20

' Access with variables (no static check, but valid syntax)
DIM i AS INTEGER
DIM j AS INTEGER
LET i = 1
LET j = 2
LET A(i, j) = 100

END
