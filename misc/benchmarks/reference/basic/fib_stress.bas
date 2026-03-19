FUNCTION Fib(n AS LONG) AS LONG
    IF n <= 1 THEN
        Fib = n
    ELSE
        Fib = Fib(n - 1) + Fib(n - 2)
    END IF
END FUNCTION

DIM result AS LONG
result = Fib(40)
Viper.Environment.EndProgram(result)
END
