DIM sum AS LONG
DIM i AS LONG
sum = 0
FOR i = 0 TO 499999
    DIM result AS STRING
    result = "Hello" + " " + "World" + "!"
    sum = sum + LEN(result)
NEXT i
Viper.Environment.EndProgram(sum)
END
