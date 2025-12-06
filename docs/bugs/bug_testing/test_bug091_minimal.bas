REM Minimal test for BUG-091: Compiler crash

DIM test_array(10) AS INTEGER

SUB TestFunc(param AS INTEGER)
    DIM i AS INTEGER
    FOR i = 1 TO 10
        test_array(i) = param * i
    NEXT i
END SUB

TestFunc(5)
PRINT "Done"
