REM Test class with methods but no fields

CLASS Calculator
    REM No fields

    SUB Init()
        REM Nothing
    END SUB

    FUNCTION Add(a AS INTEGER, b AS INTEGER) AS INTEGER
        RETURN a + b
    END FUNCTION

    FUNCTION Multiply(x AS INTEGER, y AS INTEGER) AS INTEGER
        RETURN x * y
    END FUNCTION
END CLASS

PRINT "Creating calculator..."
DIM calc AS Calculator
calc = NEW Calculator()
calc.Init()

DIM result AS INTEGER
result = calc.Add(5, 3)
PRINT "5 + 3 = "; result

result = calc.Multiply(4, 7)
PRINT "4 * 7 = "; result

PRINT "Success!"
