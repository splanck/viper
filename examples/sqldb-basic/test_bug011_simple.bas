' Minimal test case for Bug #011
PRINT "=== Bug #011 Minimal Test ==="

CLASS Container
    PUBLIC name AS STRING
END CLASS

' Test: Direct member access in SUB with object parameter
SUB TestParam(c AS Container)
    PRINT "In SUB, c.name = "; c.name
END SUB

' Create and test
DIM obj AS Container
LET obj = NEW Container()
obj.name = "TestValue"

TestParam(obj)

PRINT "=== Done ==="
