REM Test string array assignment
DIM names(3) AS STRING

PRINT "Test 1: Direct assignment with literals"
names(1) = "Alice"
names(2) = "Bob"
names(3) = "Charlie"

PRINT "Test 2: Reading back"
PRINT "1: "; names(1)
PRINT "2: "; names(2)
PRINT "3: "; names(3)

PRINT "Test 3: Assignment from variable"
DIM newName AS STRING
newName = "David"
names(1) = newName

PRINT "Updated 1: "; names(1)

PRINT "Test complete!"
