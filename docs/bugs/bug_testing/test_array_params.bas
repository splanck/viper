REM Test: Array parameters in SUBs and FUNCTIONs
REM Testing if arrays can be passed as parameters

REM Test 1: Simple integer array parameter
SUB PrintIntArray(arr() AS INTEGER, count AS INTEGER)
    DIM i AS INTEGER
    PRINT "Integer array:"
    FOR i = 1 TO count
        PRINT arr(i); " ";
    NEXT i
    PRINT ""
END SUB

REM Test 2: Object array parameter
CLASS SimpleClass
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    FUNCTION GetValue() AS INTEGER
        GetValue = ME.value
    END FUNCTION
END CLASS

SUB PrintObjectArray(objs() AS SimpleClass, count AS INTEGER)
    DIM i AS INTEGER
    PRINT "Object array:"
    FOR i = 1 TO count
        PRINT objs(i).GetValue(); " ";
    NEXT i
    PRINT ""
END SUB

REM Main test
PRINT "=== Array Parameter Test ==="
PRINT ""

REM Test integer array
DIM nums(5) AS INTEGER
nums(1) = 10
nums(2) = 20
nums(3) = 30
nums(4) = 40
nums(5) = 50

PrintIntArray(nums, 5)

REM Test object array
DIM objects(3) AS SimpleClass
DIM i AS INTEGER
FOR i = 1 TO 3
    objects(i) = NEW SimpleClass()
    objects(i).Init(i * 100)
NEXT i

PrintObjectArray(objects, 3)

PRINT "Test complete!"
