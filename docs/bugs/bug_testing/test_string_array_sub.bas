REM Test string array assignment in SUB
DIM names(3) AS STRING

SUB TestAssign()
    PRINT "Assigning in SUB..."
    names(1) = "Alice"
    names(2) = "Bob"
    PRINT "Done!"
END SUB

PRINT "Before SUB:"
TestAssign()

PRINT "After SUB:"
PRINT "1: "; names(1)
PRINT "2: "; names(2)
