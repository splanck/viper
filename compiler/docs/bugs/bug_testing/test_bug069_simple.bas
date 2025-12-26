REM Test BUG-069: Objects Not Initialized by DIM

CLASS Simple
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Show()
        PRINT "Value: "; ME.value
    END SUB
END CLASS

PRINT "Test 1: Using NEW (works)"
DIM obj1 AS Simple
obj1 = NEW Simple()
obj1.Init(42)
obj1.Show()

PRINT
PRINT "Test 2: Calling method on null object (should trap)"
DIM obj2 AS Simple
REM obj2 is null here - calling Init should trap
obj2.Init(99)
obj2.Show()
