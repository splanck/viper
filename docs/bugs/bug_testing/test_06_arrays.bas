' Test 06: Arrays
DIM items(5) AS STRING
DIM i AS INTEGER

items(0) = "Sword"
items(1) = "Shield"
items(2) = "Potion"
items(3) = "Key"
items(4) = "Map"

PRINT "Inventory:"
FOR i = 0 TO 4
    PRINT "  "; items(i)
NEXT i
END
