REM Test BUG-012: BOOLEAN type compatibility
DIM flag AS BOOLEAN
flag = TRUE
PRINT "flag = " + STR$(flag)
IF flag = FALSE THEN
    PRINT "Flag is false"
ELSE
    PRINT "Flag is true"
END IF
END
