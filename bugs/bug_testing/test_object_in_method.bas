REM Test creating object of another class inside a method

CLASS Helper
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Display()
        PRINT "Value: "; ME.value
    END SUB
END CLASS

CLASS Manager
    REM No fields

    SUB Init()
        REM Nothing
    END SUB

    SUB ShowValue(num AS INTEGER)
        REM Create Helper object inside this method
        DIM h AS Helper
        h = NEW Helper()
        h.Init(num)
        h.Display()
    END SUB
END CLASS

PRINT "Testing object creation in method..."
DIM mgr AS Manager
mgr = NEW Manager()
mgr.Init()

mgr.ShowValue(100)
mgr.ShowValue(200)

PRINT "Success!"
