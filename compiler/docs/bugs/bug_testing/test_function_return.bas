REM Test function return detection

CLASS Test
    value AS INTEGER

    FUNCTION Simple() AS INTEGER
        Simple = 42
    END FUNCTION

    FUNCTION WithIf() AS INTEGER
        IF ME.value > 0 THEN
            WithIf = ME.value
        ELSE
            WithIf = 0
        END IF
    END FUNCTION

    FUNCTION WithIfNoElse() AS INTEGER
        IF ME.value > 0 THEN
            WithIfNoElse = ME.value
        END IF
        WithIfNoElse = 0
    END FUNCTION
END CLASS

DIM t AS Test
t.value = 10
PRINT t.Simple()
PRINT t.WithIf()
PRINT t.WithIfNoElse()
