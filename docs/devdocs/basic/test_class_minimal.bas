REM Minimal class test

CLASS TestClass
    DIM value AS INTEGER

    SUB SetValue(v AS INTEGER)
        value = v
    END SUB

    SUB ShowValue()
        PRINT "Value: " + STR$(value)
    END SUB
END CLASS

PRINT "Creating object..."
DIM obj AS TestClass
obj = NEW TestClass()

PRINT "Setting value..."
obj.SetValue(42)

PRINT "Showing value..."
obj.ShowValue()

PRINT "Done!"

END
