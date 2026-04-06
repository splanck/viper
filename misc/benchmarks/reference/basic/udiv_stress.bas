DIM sum AS LONG
DIM i AS LONG
DIM d1 AS LONG, d2 AS LONG, d3 AS LONG, d4 AS LONG
DIM d5 AS LONG, d6 AS LONG, d7 AS LONG, d8 AS LONG
DIM s AS LONG
sum = 0
i = 1
DO WHILE i < 50000001
    d1 = i \ 2
    d2 = i \ 4
    d3 = i \ 8
    d4 = i \ 16
    d5 = i \ 32
    d6 = i \ 64
    d7 = i \ 128
    d8 = i \ 256
    s = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8
    sum = (sum + s) MOD 268435456
    i = i + 1
LOOP
Viper.Environment.EndProgram(sum)
END
