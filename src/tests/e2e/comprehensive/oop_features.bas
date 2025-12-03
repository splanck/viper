' Comprehensive Test 2: Object-Oriented Programming Features
' Tests: classes, constructors, methods, properties, inheritance simulation, arrays of objects
' Expected to produce deterministic output for both VM and native execution

' Simple Point class
CLASS Point
    PUBLIC x AS INTEGER
    PUBLIC y AS INTEGER

    SUB Init(px AS INTEGER, py AS INTEGER)
        x = px
        y = py
    END SUB

    FUNCTION DistanceFromOrigin() AS DOUBLE
        RETURN SQR(x * x + y * y)
    END FUNCTION

    SUB Move(dx AS INTEGER, dy AS INTEGER)
        x = x + dx
        y = y + dy
    END SUB

    FUNCTION ToString() AS STRING
        RETURN "(" + STR$(x) + "," + STR$(y) + ")"
    END FUNCTION
END CLASS

' Rectangle class using Point
CLASS Rectangle
    PUBLIC topLeft AS Point
    PUBLIC width AS INTEGER
    PUBLIC height AS INTEGER

    SUB Init(tlx AS INTEGER, tly AS INTEGER, w AS INTEGER, h AS INTEGER)
        topLeft = NEW Point()
        topLeft.Init(tlx, tly)
        width = w
        height = h
    END SUB

    FUNCTION Area() AS INTEGER
        RETURN width * height
    END FUNCTION

    FUNCTION Perimeter() AS INTEGER
        RETURN 2 * (width + height)
    END FUNCTION

    SUB Move(dx AS INTEGER, dy AS INTEGER)
        topLeft.Move(dx, dy)
    END SUB
END CLASS

' Counter class for testing state
CLASS Counter
    PUBLIC value AS INTEGER
    PUBLIC name AS STRING

    SUB Init(n AS STRING, initial AS INTEGER)
        name = n
        value = initial
    END SUB

    SUB Increment()
        value = value + 1
    END SUB

    SUB Decrement()
        value = value - 1
    END SUB

    SUB Add(amount AS INTEGER)
        value = value + amount
    END SUB

    FUNCTION GetValue() AS INTEGER
        RETURN value
    END FUNCTION
END CLASS

' Stack implementation using array
CLASS IntStack
    PUBLIC items(20) AS INTEGER
    PUBLIC top AS INTEGER

    SUB Init()
        top = -1
    END SUB

    SUB Push(val AS INTEGER)
        top = top + 1
        items(top) = val
    END SUB

    FUNCTION Pop() AS INTEGER
        DIM val AS INTEGER
        val = items(top)
        top = top - 1
        RETURN val
    END FUNCTION

    FUNCTION Peek() AS INTEGER
        RETURN items(top)
    END FUNCTION

    FUNCTION IsEmpty() AS INTEGER
        IF top < 0 THEN
            RETURN 1
        ELSE
            RETURN 0
        END IF
    END FUNCTION

    FUNCTION Size() AS INTEGER
        RETURN top + 1
    END FUNCTION
END CLASS

' Main test code
DIM p1 AS Point
DIM p2 AS Point
DIM rect AS Rectangle
DIM c1 AS Counter
DIM c2 AS Counter
DIM stack AS IntStack
DIM i AS INTEGER

PRINT "=== Point Class Tests ==="
p1 = NEW Point()
p1.Init(3, 4)
PRINT p1.x
PRINT p1.y
PRINT p1.DistanceFromOrigin()
PRINT p1.ToString()

p1.Move(2, 3)
PRINT p1.x
PRINT p1.y

p2 = NEW Point()
p2.Init(0, 0)
PRINT p2.DistanceFromOrigin()

PRINT "=== Rectangle Class Tests ==="
rect = NEW Rectangle()
rect.Init(10, 20, 5, 3)
PRINT rect.topLeft.x
PRINT rect.topLeft.y
PRINT rect.width
PRINT rect.height
PRINT rect.Area()
PRINT rect.Perimeter()

rect.Move(5, 5)
PRINT rect.topLeft.x
PRINT rect.topLeft.y

PRINT "=== Counter Class Tests ==="
c1 = NEW Counter()
c1.Init("Counter1", 0)
PRINT c1.name
PRINT c1.GetValue()

FOR i = 1 TO 5
    c1.Increment()
NEXT i
PRINT c1.GetValue()

c1.Decrement()
c1.Decrement()
PRINT c1.GetValue()

c1.Add(10)
PRINT c1.GetValue()

c2 = NEW Counter()
c2.Init("Counter2", 100)
PRINT c2.name
PRINT c2.GetValue()

PRINT "=== Stack Class Tests ==="
stack = NEW IntStack()
stack.Init()
PRINT stack.IsEmpty()
PRINT stack.Size()

' Push values
FOR i = 1 TO 5
    stack.Push(i * 10)
NEXT i
PRINT stack.IsEmpty()
PRINT stack.Size()
PRINT stack.Peek()

' Pop values
FOR i = 1 TO 3
    PRINT stack.Pop()
NEXT i
PRINT stack.Size()

PRINT "=== Array of Objects ==="
DIM points(5) AS Point
FOR i = 0 TO 4
    points(i) = NEW Point()
    points(i).Init(i, i * 2)
NEXT i

FOR i = 0 TO 4
    PRINT points(i).x
    PRINT points(i).y
NEXT i

' Move all points
FOR i = 0 TO 4
    points(i).Move(10, 10)
NEXT i

PRINT "After moving:"
FOR i = 0 TO 4
    PRINT points(i).x
    PRINT points(i).y
NEXT i

PRINT "=== Multiple Object Interactions ==="
DIM counters(3) AS Counter
counters(0) = NEW Counter()
counters(0).Init("A", 0)
counters(1) = NEW Counter()
counters(1).Init("B", 10)
counters(2) = NEW Counter()
counters(2).Init("C", 100)

FOR i = 0 TO 2
    counters(i).Add(i * 5)
NEXT i

DIM total AS INTEGER
total = 0
FOR i = 0 TO 2
    PRINT counters(i).name
    PRINT counters(i).GetValue()
    total = total + counters(i).GetValue()
NEXT i
PRINT "Total:"
PRINT total

PRINT "=== Test Complete ==="
