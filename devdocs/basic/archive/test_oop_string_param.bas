CLASS Test
    SUB PrintMessage(msg$ AS STRING)
        PRINT msg$
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.PrintMessage("Hello World")
