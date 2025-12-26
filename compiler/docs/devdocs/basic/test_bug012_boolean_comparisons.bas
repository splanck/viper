REM Test BUG-012 FIX: Boolean comparisons with TRUE/FALSE
DIM flag AS BOOLEAN
flag = TRUE

PRINT "Testing boolean comparisons:"

REM Test Eq
IF flag = TRUE THEN
    PRINT "1. flag = TRUE works"
END IF

IF flag = FALSE THEN
    PRINT "ERROR: flag = FALSE should be false"
ELSE
    PRINT "2. flag = FALSE works (correctly false)"
END IF

REM Test Ne
IF flag <> FALSE THEN
    PRINT "3. flag <> FALSE works"
END IF

IF flag <> TRUE THEN
    PRINT "ERROR: flag <> TRUE should be false"
ELSE
    PRINT "4. flag <> TRUE works (correctly false)"
END IF

REM Test with FALSE
DIM flag2 AS BOOLEAN
flag2 = FALSE

IF flag2 = FALSE THEN
    PRINT "5. FALSE = FALSE works"
END IF

IF flag2 <> TRUE THEN
    PRINT "6. FALSE <> TRUE works"
END IF

REM Direct TRUE/FALSE comparisons
IF TRUE = TRUE THEN
    PRINT "7. TRUE = TRUE works"
END IF

IF FALSE <> TRUE THEN
    PRINT "8. FALSE <> TRUE works"
END IF

PRINT "All boolean comparison tests passed!"
END
