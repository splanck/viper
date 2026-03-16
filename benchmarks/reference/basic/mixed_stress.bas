FUNCTION Helper(x AS LONG) AS LONG
    Helper = x * 3 + 7
END FUNCTION

DIM sum AS LONG
DIM i AS LONG
sum = 0
FOR i = 0 TO 9999999
    DIM t1 AS LONG, t2 AS LONG, t3 AS LONG, tmp AS LONG
    t1 = i + 1
    t2 = t1 * 2
    t3 = t2 - i
    IF i MOD 4 = 0 THEN
        tmp = Helper(t3) * 2
    ELSE
        tmp = (t3 + 100) * 3
    END IF
    IF i MOD 7 = 0 THEN
        tmp = tmp + Helper(tmp)
    END IF
    sum = sum + tmp
NEXT i
Viper.Environment.EndProgram(sum)
END
