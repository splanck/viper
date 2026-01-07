REM Progressive test to find FOR loop bug

CLASS SimpleClass
    value AS INTEGER

    SUB Generate(d AS INTEGER)
        ME.value = d * 10
    END SUB

    SUB Show()
        PRINT "Value: "; ME.value
    END SUB
END CLASS

PRINT "Test 1: FOR loop with object, no method calls"
DIM obj AS SimpleClass
obj = NEW SimpleClass()

DIM day AS INTEGER
FOR day = 1 TO 3
    PRINT "Day "; day
NEXT day
PRINT

PRINT "Test 2: FOR loop with method call"
FOR day = 1 TO 3
    PRINT "Day "; day
    obj.Generate(day)
    obj.Show()
NEXT day
PRINT

PRINT "Test 3: Two objects like weather/market"
DIM obj2 AS SimpleClass
obj2 = NEW SimpleClass()

FOR day = 1 TO 3
    PRINT "Iteration "; day
    obj.Generate(day)
    obj2.Generate(day)
    obj.Show()
    obj2.Show()
NEXT day
