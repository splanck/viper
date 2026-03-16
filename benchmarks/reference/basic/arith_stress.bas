DIM sum AS LONG
DIM i AS LONG
DIM t1 AS LONG, t2 AS LONG, t3 AS LONG, t4 AS LONG, t5 AS LONG
DIM t6 AS LONG, t7 AS LONG, t8 AS LONG, t9 AS LONG
sum = 0
FOR i = 0 TO 49999999
    t1 = i + 1
    t2 = t1 * 2
    t3 = i + 3
    t4 = t2 + t3
    t5 = t4 * 5
    t6 = t5 - i
    t7 = t6 + 7
    t8 = t7 * 3
    t9 = t8 - 11
    sum = sum + t9
NEXT i
Viper.Environment.EndProgram(sum)
END
