REM Test error handling with array bounds
DIM numbers(5) AS INTEGER
DIM idx AS INTEGER
DIM value AS INTEGER

ON ERROR GOTO ErrorHandler

PRINT "Testing array bounds error handling..."
PRINT ""

PRINT "Setting valid elements..."
numbers(1) = 10
numbers(2) = 20
numbers(3) = 30
PRINT "Elements set successfully"
PRINT ""

PRINT "Reading valid element:"
value = numbers(2)
PRINT "numbers(2) = "; value
PRINT ""

PRINT "Trying to access out-of-bounds element..."
idx = 10
value = numbers(idx)
PRINT "This should not print!"

END

ErrorHandler:
    PRINT ""
    PRINT "*** ERROR CAUGHT! ***"
    PRINT "Array index out of bounds!"
    PRINT "Index was: "; idx
    PRINT ""
    PRINT "Continuing..."
    END
