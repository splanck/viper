REM Test explicit RETURN vs function name assignment

CLASS Test
    FUNCTION GetValueExplicit() AS INTEGER
        RETURN 42
    END FUNCTION

    FUNCTION GetValueImplicit() AS INTEGER
        GetValueImplicit = 42
    END FUNCTION
END CLASS

DIM t AS Test
t = NEW Test()

DIM result AS INTEGER
result = t.GetValueExplicit()
PRINT "Explicit RETURN: "; result

result = t.GetValueImplicit()
PRINT "Implicit (name assignment): "; result
