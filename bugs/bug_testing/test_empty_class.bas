REM Minimal test for empty class constructor bug

CLASS Empty
    REM No fields

    SUB Init()
        REM Nothing to do
    END SUB
END CLASS

PRINT "Creating empty object..."
DIM obj AS Empty
obj = NEW Empty()
obj.Init()

PRINT "Success!"
