REM Test ME in Init methods

CLASS Simple
    value AS INTEGER

    SUB Init(v AS INTEGER)
        PRINT "Init called with: "; v
        ME.value = v
        PRINT "Set ME.value to: "; ME.value
    END SUB

    SUB Display()
        PRINT "Value: "; ME.value
    END SUB
END CLASS

PRINT "Creating object..."
DIM obj AS Simple
PRINT "Calling Init..."
obj.Init(42)
PRINT "Calling Display..."
obj.Display()
PRINT "Done!"
