' BUG-OOP-006: Test multiple object-returning functions in same file
CLASS Point
    PUBLIC x AS INTEGER
    PUBLIC y AS INTEGER
END CLASS

FUNCTION CreatePoint(px AS INTEGER, py AS INTEGER) AS Point
    DIM p AS Point
    p = NEW Point()
    p.x = px
    p.y = py
    RETURN p
END FUNCTION

FUNCTION MidPoint(p1 AS Point, p2 AS Point) AS Point
    DIM mid AS Point
    mid = NEW Point()
    mid.x = (p1.x + p2.x) / 2
    mid.y = (p1.y + p2.y) / 2
    RETURN mid
END FUNCTION

DIM a AS Point
a = CreatePoint(0, 0)
DIM b AS Point
b = CreatePoint(10, 20)
DIM m AS Point
m = MidPoint(a, b)
PRINT m.x
PRINT m.y
END
