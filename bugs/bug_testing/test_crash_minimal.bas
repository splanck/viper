REM Minimal test to reproduce crash

CLASS TestClass
    val AS INTEGER

    SUB SetVal(v AS INTEGER)
        ME.val = v
    END SUB

    FUNCTION GetVal() AS INTEGER
        RETURN ME.val
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
obj.SetVal(42)
PRINT "Value: "; obj.GetVal()
