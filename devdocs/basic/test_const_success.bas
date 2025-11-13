CONST PI = 3.14159
PRINT "PI ="; PI

CONST MESSAGE$ = "Hello, World!"
PRINT MESSAGE$

CONST MAX_COUNT = 100
PRINT "Max count:"; MAX_COUNT

' This should work - constants can be used in expressions
CONST RADIUS = 5.0
CONST AREA = PI * RADIUS * RADIUS
PRINT "Area of circle with radius"; RADIUS; "is"; AREA

' Test using a constant in an expression
LET DOUBLE_PI = PI * 2
PRINT "2 * PI ="; DOUBLE_PI
