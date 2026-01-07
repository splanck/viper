' BUG-OOP-003: Virtual method inheritance without override
' Verifies that a child class can call a virtual FUNCTION/SUB inherited
' from the parent without providing an OVERRIDE - should use parent's impl.
CLASS Parent
    VIRTUAL FUNCTION GetValue() AS INTEGER
        RETURN 42
    END FUNCTION

    VIRTUAL FUNCTION GetMessage() AS STRING
        RETURN "Parent message"
    END FUNCTION

    VIRTUAL SUB Speak()
        PRINT "Parent speaks"
    END SUB
END CLASS

CLASS Child : Parent
    ' No OVERRIDE - should inherit all methods from Parent
END CLASS

DIM c AS Child
c = NEW Child()
PRINT c.GetValue()
PRINT c.GetMessage()
c.Speak()
END
