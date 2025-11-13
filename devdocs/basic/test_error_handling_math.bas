REM Test ON ERROR with math functions
PRINT "=== Testing Error Handling with Math Functions ==="

ON ERROR GOTO ErrorHandler

PRINT "Computing LOG of positive numbers..."
FOR i% = 1 TO 5
    result# = LOG(i%)
    PRINT "LOG("; i%; ") = "; result#
NEXT i%

PRINT ""
PRINT "Computing LOG of negative (causes domain error in strict math)..."
x% = -1
result# = LOG(x%)
PRINT "LOG(-1) = "; result#; " (NaN expected)"

PRINT ""
PRINT "Program completed without triggering error handler"
PRINT "(LOG returns NaN instead of error)"
END

ErrorHandler:
PRINT "Error handler triggered!"
PRINT "Error code: "; ERR()
RESUME NEXT
