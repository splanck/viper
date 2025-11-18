REM Test: SELECT CASE with EXIT FUNCTION
REM Minimal reproduction of IL error

CLASS TestClass
    value AS INTEGER

    FUNCTION TestMethod(x AS INTEGER) AS INTEGER
        TestMethod = 0  REM Default

        SELECT CASE x
            CASE 0
                TestMethod = 100
                EXIT FUNCTION
            CASE 1
                TestMethod = 200
            CASE ELSE
                TestMethod = 300
        END SELECT
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()

PRINT "Testing SELECT CASE with EXIT FUNCTION"
PRINT "TestMethod(0) = "; obj.TestMethod(0)
PRINT "TestMethod(1) = "; obj.TestMethod(1)
PRINT "TestMethod(2) = "; obj.TestMethod(2)
