' Test 07: Arrays of objects
CLASS Item
    DIM name AS STRING
    DIM value AS INTEGER

    SUB Init(n AS STRING, v AS INTEGER)
        name = n
        value = v
    END SUB
END CLASS

DIM inventory(3) AS Item
DIM i AS INTEGER

' Create items
inventory(0) = NEW Item()
inventory(0).Init("Gold Coin", 10)

inventory(1) = NEW Item()
inventory(1).Init("Silver Ring", 50)

inventory(2) = NEW Item()
inventory(2).Init("Magic Gem", 100)

PRINT "Inventory:"
FOR i = 0 TO 2
    PRINT "  "; inventory(i).name; " ("; inventory(i).value; ")"
NEXT i
END
