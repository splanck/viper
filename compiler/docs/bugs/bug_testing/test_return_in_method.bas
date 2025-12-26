REM Test: RETURN vs assignment in class methods

CLASS TestClass
    val AS INTEGER

    SUB SetVal(v AS INTEGER)
        ME.val = v
    END SUB

    FUNCTION GetValWithReturn() AS INTEGER
        RETURN ME.val
    END FUNCTION

    FUNCTION GetValWithAssignment() AS INTEGER
        GetValWithAssignment = ME.val
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
obj.SetVal(42)
PRINT "With RETURN: "; obj.GetValWithReturn()
PRINT "With assignment: "; obj.GetValWithAssignment()
