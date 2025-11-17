REM Test boolean parameter passing

CLASS Test
    flag AS BOOLEAN

    SUB SetFlag(value AS BOOLEAN)
        ME.flag = value
    END SUB

    SUB Display()
        PRINT "Flag: "; ME.flag
    END SUB
END CLASS

DIM t AS Test

PRINT "Test 1: TRUE constant"
t.SetFlag(TRUE)
t.Display()

PRINT "Test 2: FALSE constant"
t.SetFlag(FALSE)
t.Display()

PRINT "Test 3: Integer 1"
t.SetFlag(1)
t.Display()

PRINT "Test 4: Integer 0"
t.SetFlag(0)
t.Display()
