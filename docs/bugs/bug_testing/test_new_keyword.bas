REM Test NEW keyword for object creation

CLASS Simple
    value AS INTEGER

    SUB New(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Display()
        PRINT "Value: "; ME.value
    END SUB
END CLASS

PRINT "Creating with NEW..."
DIM obj AS NEW Simple(42)
obj.Display()
PRINT "Done!"
