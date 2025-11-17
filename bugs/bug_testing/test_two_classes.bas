REM Test two classes in one file

CLASS First
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Show()
        PRINT "First: "; ME.value
    END SUB
END CLASS

CLASS Second
    REM No fields

    SUB Init()
        REM Nothing
    END SUB

    SUB Greet()
        PRINT "Hello from Second"
    END SUB
END CLASS

PRINT "Creating objects..."
DIM obj1 AS First
DIM obj2 AS Second

obj1 = NEW First()
obj2 = NEW Second()

obj1.Init(42)
obj2.Init()

obj1.Show()
obj2.Greet()

PRINT "Success!"
