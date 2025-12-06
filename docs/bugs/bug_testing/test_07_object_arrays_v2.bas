' Test 07b: Arrays of objects (workaround)
CLASS Item
    DIM name AS STRING
    DIM value AS INTEGER
END CLASS

DIM inventory(3) AS Item
DIM temp AS Item
DIM i AS INTEGER

' Create items using temp variable
temp = NEW Item()
temp.name = "Gold Coin"
temp.value = 10
inventory(0) = temp

temp = NEW Item()
temp.name = "Silver Ring"
temp.value = 50
inventory(1) = temp

temp = NEW Item()
temp.name = "Magic Gem"
temp.value = 100
inventory(2) = temp

PRINT "Inventory:"
FOR i = 0 TO 2
    temp = inventory(i)
    PRINT "  "; temp.name; " ("; temp.value; ")"
NEXT i
END
