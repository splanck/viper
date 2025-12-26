REM Test: Nested IF inside SELECT CASE in class method
REM Testing if this causes empty block errors

CLASS TestClass
    pieceColor AS INTEGER

    FUNCTION TestNested(pieceType AS INTEGER, toRow AS INTEGER) AS INTEGER
        TestNested = 0

        SELECT CASE pieceType
            CASE 1
                IF ME.pieceColor = 0 THEN
                    IF toRow > 5 THEN
                        TestNested = 1
                    ELSE
                        TestNested = 0
                    END IF
                ELSE
                    IF toRow < 5 THEN
                        TestNested = 1
                    ELSE
                        TestNested = 0
                    END IF
                END IF

            CASE 2
                TestNested = 2

            CASE ELSE
                TestNested = 99
        END SELECT
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
obj.pieceColor = 0

PRINT "Testing nested IF in SELECT CASE"
PRINT "TestNested(1, 6) = "; obj.TestNested(1, 6)
PRINT "TestNested(1, 3) = "; obj.TestNested(1, 3)
PRINT "TestNested(2, 5) = "; obj.TestNested(2, 5)
