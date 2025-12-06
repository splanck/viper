REM Test BUG-068: Function Name Assignment for Return Value

CLASS Test
    FUNCTION GetValue() AS INTEGER
        GetValue = 42
    END FUNCTION

    FUNCTION Calculate(x AS INTEGER, y AS INTEGER) AS INTEGER
        Calculate = x + y
    END FUNCTION
END CLASS

DIM t AS Test
t = NEW Test()

DIM result AS INTEGER
result = t.GetValue()
PRINT "GetValue returned: "; result

result = t.Calculate(10, 20)
PRINT "Calculate returned: "; result

PRINT "SUCCESS: Function name assignment works!"
