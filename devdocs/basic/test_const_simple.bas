REM Test CONST keyword (BUG-009 fix) - no strings
PRINT "=== Testing CONST Declaration ==="
CONST PI = 3.14159
CONST MAX_SIZE = 100

PRINT "PI = ";
PRINT PI
PRINT "MAX_SIZE = ";
PRINT MAX_SIZE

PRINT ""
PRINT "=== Testing CONST in Expressions ==="
radius = 5
circumference = 2 * PI * radius
PRINT "Circumference (r=5) = ";
PRINT circumference

area = PI * radius * radius
PRINT "Area (r=5) = ";
PRINT area

PRINT ""
PRINT "CONST tests passed!"
