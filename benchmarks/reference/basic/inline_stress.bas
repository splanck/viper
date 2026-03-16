FUNCTION DoubleVal(x AS LONG) AS LONG
    DoubleVal = x + x
END FUNCTION

FUNCTION Square(x AS LONG) AS LONG
    Square = x * x
END FUNCTION

FUNCTION Add3(a AS LONG, b AS LONG, c AS LONG) AS LONG
    Add3 = a + b + c
END FUNCTION

FUNCTION Inc(x AS LONG) AS LONG
    Inc = x + 1
END FUNCTION

FUNCTION Combine(x AS LONG) AS LONG
    DIM d AS LONG, s AS LONG, i AS LONG
    d = DoubleVal(x)
    s = Square(x)
    i = Inc(x)
    Combine = Add3(d, s, i)
END FUNCTION

DIM sum AS LONG
DIM i AS LONG
sum = 0
FOR i = 0 TO 49999999
    DIM r AS LONG
    r = Combine(i)
    sum = sum + r
NEXT i
Viper.Environment.EndProgram(sum)
END
