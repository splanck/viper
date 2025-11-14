REM Test empty IF blocks
x% = 5
IF x% > 0 THEN
    PRINT "Positive"
END IF

IF x% > 10 THEN
    y% = 1
ELSE
    REM Nothing here - is this allowed?
END IF

PRINT "Done"
