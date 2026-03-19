FUNCTION AddTriple(a AS LONG, b AS LONG, c AS LONG) AS LONG
    AddTriple = a + b + c
END FUNCTION

FUNCTION MulPair(x AS LONG, y AS LONG) AS LONG
    MulPair = x * y
END FUNCTION

FUNCTION Compute(n AS LONG) AS LONG
    DIM s AS LONG
    s = AddTriple(n, n + 1, n + 2)
    Compute = MulPair(s, 3)
END FUNCTION

DIM sum AS LONG
DIM i AS LONG
sum = 0
FOR i = 0 TO 9999999
    DIM r1 AS LONG, r2 AS LONG, r3 AS LONG
    r1 = Compute(i)
    r2 = AddTriple(i, r1, 1)
    r3 = MulPair(r2, 2)
    sum = sum + r3
NEXT i
Viper.Environment.EndProgram(sum)
END
