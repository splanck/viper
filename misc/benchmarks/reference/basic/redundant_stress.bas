DIM sum AS LONG
DIM i AS LONG
sum = 0
FOR i = 0 TO 49999999
    DIM k1 AS LONG, k2 AS LONG, k3 AS LONG
    DIM a1 AS LONG, a2 AS LONG, b1 AS LONG, b2 AS LONG
    DIM c1 AS LONG, c2 AS LONG, c3 AS LONG
    DIM d1 AS LONG, d2 AS LONG, d3 AS LONG
    DIM live AS LONG
    k1 = 10 + 20
    k2 = k1 * 3
    k3 = k2 - 40
    a1 = i + 7
    a2 = a1 * 3
    b1 = i + 7
    b2 = b1 * 3
    c1 = 100 + 200
    c2 = c1 * 2
    c3 = c2 - 100
    d1 = 5 + 10
    d2 = d1 * 5
    d3 = d2 - 5
    live = a2 + b2 + k3 + c3 + d3
    sum = (sum + live) MOD 268435456
NEXT i
Viper.Environment.EndProgram(sum)
END
