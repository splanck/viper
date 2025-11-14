ON ERROR GOTO ErrorHandler
PRINT "Program running"
PRINT "End of main"
END

ErrorHandler:
PRINT "In error handler"
PRINT ERR()
RESUME NEXT
