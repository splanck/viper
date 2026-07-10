' Test: OOP - Classes
' Tests: CLASS, fields, methods, constructor, destructor, ME

' Simple class with fields and methods
CLASS Point
    PUBLIC x AS DOUBLE
    PUBLIC y AS DOUBLE

    PUBLIC SUB NEW(px AS DOUBLE, py AS DOUBLE)
        x = px
        y = py
    END SUB

    PUBLIC FUNCTION Distance() AS DOUBLE
        Distance = SQR(x * x + y * y)
    END FUNCTION

    PUBLIC SUB Move(dx AS DOUBLE, dy AS DOUBLE)
        x = x + dx
        y = y + dy
    END SUB

    PUBLIC FUNCTION ToString$() AS STRING
        ToString$ = "(" + STR$(x) + ", " + STR$(y) + ")"
    END FUNCTION
END CLASS

' Class with private fields
CLASS Counter
    PRIVATE count AS INTEGER

    PUBLIC SUB NEW()
        count = 0
    END SUB

    PUBLIC SUB Increment()
        count = count + 1
    END SUB

    PUBLIC SUB Decrement()
        count = count - 1
    END SUB

    PUBLIC FUNCTION GetCount() AS INTEGER
        GetCount = count
    END FUNCTION

    PUBLIC SUB Reset()
        count = 0
    END SUB
END CLASS

' Class with ME reference
CLASS Circle
    PUBLIC radius AS DOUBLE

    PUBLIC SUB NEW(r AS DOUBLE)
        ME.radius = r
    END SUB

    PUBLIC FUNCTION Area() AS DOUBLE
        Area = 3.14159 * ME.radius * ME.radius
    END FUNCTION

    PUBLIC FUNCTION Circumference() AS DOUBLE
        Circumference = 2 * 3.14159 * ME.radius
    END FUNCTION
END CLASS

' Class with destructor
CLASS Resource
    PRIVATE name AS STRING

    PUBLIC SUB NEW(n AS STRING)
        name = n
        PRINT "Resource created: "; name
    END SUB

    PUBLIC SUB DESTROY()
        PRINT "Resource destroyed: "; name
    END SUB

    PUBLIC FUNCTION GetName$() AS STRING
        GetName$ = name
    END FUNCTION
END CLASS

' Main program
PRINT "=== OOP Classes Test ==="

' Test Point class
PRINT ""
PRINT "--- Point class ---"
DIM p AS Point
p = NEW Point(3, 4)
PRINT "Point: "; p.ToString$()
PRINT "Distance from origin: "; p.Distance()
p.Move(1, 1)
PRINT "After Move(1,1): "; p.ToString$()

' Test Counter class
PRINT ""
PRINT "--- Counter class ---"
DIM c AS Counter
c = NEW Counter()
PRINT "Initial count: "; c.GetCount()
c.Increment()
c.Increment()
c.Increment()
PRINT "After 3 increments: "; c.GetCount()
c.Decrement()
PRINT "After decrement: "; c.GetCount()

' Test Circle class
PRINT ""
PRINT "--- Circle class ---"
DIM circle AS Circle
circle = NEW Circle(5)
PRINT "Circle radius: "; circle.radius
PRINT "Area: "; circle.Area()
PRINT "Circumference: "; circle.Circumference()

' Test Resource class (destructor)
PRINT ""
PRINT "--- Resource class (destructor test) ---"
DIM res AS Resource
res = NEW Resource("TestFile")
PRINT "Resource name: "; res.GetName$()
DELETE res
PRINT "After DELETE"

PRINT ""
PRINT "=== OOP Classes test complete ==="
