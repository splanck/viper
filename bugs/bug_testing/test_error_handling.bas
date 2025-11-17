REM Test ON ERROR GOTO
DIM divisor AS INTEGER
DIM result AS INTEGER
DIM numerator AS INTEGER

ON ERROR GOTO ErrorHandler

PRINT "Testing error handling..."
PRINT ""

numerator = 100
divisor = 10
result = numerator \ divisor
PRINT "100 / 10 = "; result

numerator = 50
divisor = 5
result = numerator \ divisor
PRINT "50 / 5 = "; result

PRINT ""
PRINT "Now trying division by zero..."
numerator = 42
divisor = 0
result = numerator \ divisor
PRINT "This should not print!"

PRINT ""
PRINT "Program end (should not reach here)"
END

ErrorHandler:
    PRINT ""
    PRINT "*** ERROR CAUGHT! ***"
    PRINT "Division by zero detected!"
    PRINT "Numerator was: "; numerator
    PRINT "Divisor was: "; divisor
    PRINT ""
    PRINT "Recovering..."
    divisor = 1
    RESUME NEXT
