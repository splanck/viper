DIM count AS LONG
DIM i AS LONG
count = 0
FOR i = 0 TO 19999999
    IF i MOD 2 = 0 THEN count = count + 1
    IF i MOD 3 = 0 THEN count = count + 2
    IF i MOD 5 = 0 THEN count = count + 3
    IF i MOD 7 = 0 THEN count = count + 5
NEXT i
Viper.Environment.EndProgram(count)
END
