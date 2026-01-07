REM Test passing loop variables to methods

CLASS Counter
    value AS INTEGER

    SUB SetValue(v AS INTEGER)
        ME.value = v
    END SUB

    SUB ShowValue()
        PRINT "Counter value: "; ME.value
    END SUB
END CLASS

PRINT "Test: Passing loop variable to method"
PRINT

DIM counter AS Counter
counter = NEW Counter()

DIM i AS INTEGER
FOR i = 1 TO 5
    PRINT "Loop iteration "; i
    counter.SetValue(i)
    counter.ShowValue()
    PRINT
NEXT i

PRINT "Test complete"
