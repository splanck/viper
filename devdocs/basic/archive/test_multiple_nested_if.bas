REM Test multiple ELSEIF blocks with nested IFs
x% = 5
y% = 1
z% = 0

IF x% = 1 THEN
    PRINT "X is 1"
ELSEIF x% = 2 THEN
    PRINT "X is 2"
    IF y% = 1 THEN
        PRINT "Y is 1 in block 2"
    END IF
ELSEIF x% = 3 THEN
    PRINT "X is 3"
ELSEIF x% = 4 THEN
    PRINT "X is 4"
ELSEIF x% = 5 THEN
    PRINT "X is 5"
    IF z% = 0 THEN
        PRINT "Z is 0 in block 5"
    ELSE
        PRINT "Z is not 0"
    END IF
ELSEIF x% = 6 THEN
    PRINT "X is 6"
    IF y% = 0 THEN
        PRINT "Y is 0 in block 6"
    ELSE
        PRINT "Y is not 0"
    END IF
ELSE
    PRINT "X is something else"
END IF

PRINT "Done"
END
