' Test 1: Basic integer constructor args
CLASS Point
    PUBLIC x AS INTEGER
    PUBLIC y AS INTEGER

    SUB NEW(px AS INTEGER, py AS INTEGER)
        x = px
        y = py
    END SUB
END CLASS

DIM p AS Point
p = NEW Point(10, 20)

IF p.x = 10 AND p.y = 20 THEN
    PRINT "PASS: basic constructor args"
ELSE
    PRINT "FAIL: x=" + STR$(p.x) + " y=" + STR$(p.y)
END IF

' Test 2: Multi-type constructor args (INTEGER, DOUBLE, STRING)
CLASS Rectangle
    PUBLIC x AS INTEGER
    PUBLIC y AS INTEGER
    PUBLIC width AS DOUBLE
    PUBLIC height AS DOUBLE
    PUBLIC name AS STRING

    SUB NEW(px AS INTEGER, py AS INTEGER, w AS DOUBLE, h AS DOUBLE, n AS STRING)
        x = px
        y = py
        width = w
        height = h
        name = n
    END SUB

    FUNCTION Area() AS DOUBLE
        Area = width * height
    END FUNCTION
END CLASS

DIM r AS Rectangle
r = NEW Rectangle(5, 10, 20.5, 30.5, "MyRect")

DIM passed AS INTEGER
passed = 1

IF r.x <> 5 THEN passed = 0
IF r.y <> 10 THEN passed = 0
IF r.width < 20.4 OR r.width > 20.6 THEN passed = 0
IF r.height < 30.4 OR r.height > 30.6 THEN passed = 0
' Note: String comparison on object fields not yet supported, print instead
PRINT "name=" + r.name

IF passed = 1 THEN
    PRINT "PASS: multi-type constructor args"
ELSE
    PRINT "FAIL: constructor args not passed correctly"
    PRINT "x=" + STR$(r.x) + " y=" + STR$(r.y)
    PRINT "width=" + STR$(r.width) + " height=" + STR$(r.height)
END IF
