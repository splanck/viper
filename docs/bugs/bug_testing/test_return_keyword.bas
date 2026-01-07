REM Test if RETURN keyword is required

CLASS Test
    value AS INTEGER

    FUNCTION WithReturn() AS INTEGER
        RETURN 42
    END FUNCTION

    FUNCTION WithAssignment() AS INTEGER
        WithAssignment = 42
    END FUNCTION
END CLASS

DIM t AS Test
t.value = 10
PRINT "With RETURN: "; t.WithReturn()
PRINT "With assignment: "; t.WithAssignment()
