' Test: Variables and Constants
' Tests: DIM, CONST, STATIC, type inference

' Regular variable declaration
DIM x AS INTEGER
x = 10
PRINT "Variable x: "; x

' Constant declaration
CONST PI = 3.14159
CONST MAX_SIZE = 100
CONST GREETING$ = "Hello"
PRINT "Constant PI: "; PI
PRINT "Constant MAX_SIZE: "; MAX_SIZE
PRINT "Constant GREETING: "; GREETING$

' Multiple declarations on one line
DIM a AS INTEGER, b AS INTEGER, c AS INTEGER
a = 1 : b = 2 : c = 3
PRINT "a, b, c: "; a; ", "; b; ", "; c

' Variable reassignment
x = 20
PRINT "Reassigned x: "; x

' Type suffix inference
result% = 42
result# = 3.14
result$ = "test"
PRINT "Suffix %: "; result%
PRINT "Suffix #: "; result#
PRINT "Suffix $: "; result$

' Static variable test
TestStatic()
TestStatic()
TestStatic()

PRINT "=== Variables test complete ==="
END

SUB TestStatic()
    STATIC counter AS INTEGER
    counter = counter + 1
    PRINT "Static counter: "; counter
END SUB
