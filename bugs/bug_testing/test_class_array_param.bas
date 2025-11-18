REM Test: Passing a class's array field as parameter

CLASS Item
    val AS INTEGER
    SUB SetVal(v AS INTEGER)
        ME.val = v
    END SUB
END CLASS

SUB InitArray(items() AS Item)
    DIM i AS INTEGER
    FOR i = 1 TO 3
        items(i) = NEW Item()
        items(i).SetVal(i * 10)
    NEXT i
END SUB

CLASS Container
    items(3) AS Item

    SUB InitItems()
        InitArray(items)  REM Pass class field array as parameter
    END SUB
END CLASS

DIM c AS Container
c = NEW Container()
c.InitItems()
PRINT "Test complete!"
