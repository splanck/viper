REM Test BUG-089: Calling methods on object arrays from within class methods

CLASS Item
    val AS INTEGER

    SUB SetVal(v AS INTEGER)
        ME.val = v
    END SUB

    FUNCTION GetVal() AS INTEGER
        GetVal = ME.val
    END FUNCTION
END CLASS

CLASS Container
    items(3) AS Item

    SUB InitItems()
        DIM i AS INTEGER
        FOR i = 1 TO 3
            items(i) = NEW Item()
            items(i).SetVal(i * 10)  REM BUG: This fails!
        NEXT i
    END SUB

    SUB PrintItems()
        DIM i AS INTEGER
        FOR i = 1 TO 3
            PRINT "Item "; i; ": "; items(i).GetVal()
        NEXT i
    END SUB
END CLASS

DIM c AS Container
c = NEW Container()
c.InitItems()
c.PrintItems()
