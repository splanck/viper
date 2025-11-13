REM Test nested IF inside ELSEIF
x% = 2
y% = 1

IF x% = 1 THEN
    PRINT "X is 1"
ELSEIF x% = 2 THEN
    PRINT "X is 2"
    IF y% = 1 THEN
        PRINT "Y is 1"
    ELSE
        PRINT "Y is not 1"
    END IF
ELSE
    PRINT "X is something else"
END IF

PRINT "Done"
END
