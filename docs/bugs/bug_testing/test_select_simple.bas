REM Simple SELECT CASE test

DIM x AS INTEGER
x = 1

SELECT CASE x
    CASE 1
        PRINT "One"
    CASE 2
        PRINT "Two"
    CASE ELSE
        PRINT "Other"
END SELECT

PRINT "Done"
