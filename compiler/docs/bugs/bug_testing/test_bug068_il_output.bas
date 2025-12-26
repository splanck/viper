REM Check IL output for function name assignment

CLASS Test
    FUNCTION GetValue() AS INTEGER
        GetValue = 42
    END FUNCTION
END CLASS

DIM t AS Test
t = NEW Test()
DIM result AS INTEGER
result = t.GetValue()
PRINT result
