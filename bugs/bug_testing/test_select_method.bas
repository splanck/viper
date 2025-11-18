REM Test SELECT CASE with string in class method

CLASS TestClass
    FUNCTION GetName(n AS INTEGER) AS STRING
        DIM s AS STRING
        SELECT CASE n
            CASE 1
                s = "Alice"
            CASE 2
                s = "Bob"
            CASE ELSE
                s = "Unknown"
        END SELECT
        GetName = s
    END FUNCTION
END CLASS

DIM obj AS TestClass
obj = NEW TestClass()
PRINT "Test 1: "; obj.GetName(1)
PRINT "Test 2: "; obj.GetName(2)
PRINT "Test 3: "; obj.GetName(99)
