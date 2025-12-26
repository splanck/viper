REM Test BUG-017: Global strings from methods
DIM globalString AS STRING
globalString = "Hello World"

CLASS Test
    SUB UseGlobal()
        PRINT globalString
    END SUB
END CLASS

DIM t AS Test
t = NEW Test()
t.UseGlobal()
END
