REM Test calling methods on ME within another method

CLASS Data
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Show()
        PRINT "Data: "; ME.value
    END SUB
END CLASS

CLASS Processor
    REM No fields

    SUB Init()
        REM Nothing
    END SUB

    FUNCTION GetValue(x AS INTEGER) AS INTEGER
        RETURN x * 2
    END FUNCTION

    SUB Process(num AS INTEGER)
        REM Call method on ME and use result
        DIM doubled AS INTEGER
        doubled = ME.GetValue(num)

        REM Create another object with the result
        DIM d AS Data
        d = NEW Data()
        d.Init(doubled)
        d.Show()
    END SUB
END CLASS

PRINT "Testing ME method calls..."
DIM proc AS Processor
proc = NEW Processor()
proc.Init()

proc.Process(5)
proc.Process(10)

PRINT "Success!"
