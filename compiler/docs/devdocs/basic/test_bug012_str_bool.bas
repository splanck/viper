REM Test BUG-012 FIX: STR$(TRUE) and STR$(FALSE)
PRINT "Testing STR$ with TRUE/FALSE:"
PRINT "STR$(TRUE) = " + STR$(TRUE)
PRINT "STR$(FALSE) = " + STR$(FALSE)

REM Test comparisons
IF TRUE = TRUE THEN
    PRINT "TRUE = TRUE works"
END IF

IF FALSE <> TRUE THEN
    PRINT "FALSE <> TRUE works"
END IF

PRINT "Tests completed!"
END
