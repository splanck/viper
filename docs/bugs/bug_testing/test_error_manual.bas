REM Test manual error with ERROR statement
DIM errorCode AS INTEGER

ON ERROR GOTO ErrorHandler

PRINT "Before error"
PRINT ""

errorCode = 42
ERROR errorCode

PRINT "This should not print"
END

ErrorHandler:
    PRINT "*** ERROR CAUGHT! ***"
    PRINT "Error code: "; errorCode
    END
