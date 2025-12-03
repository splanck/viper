REM Test floating point type system
PRINT "=== Testing AS FLOAT Declaration ==="

DIM x AS FLOAT
x = 3.14159
PRINT "x AS FLOAT = 3.14159: ";
PRINT x

DIM radius AS FLOAT
radius = 5.5
PRINT "radius AS FLOAT = 5.5: ";
PRINT radius

circumference = 2 * x * radius
PRINT "2 * PI * radius: ";
PRINT circumference

PRINT ""
PRINT "=== Testing AS DOUBLE (if it exists) ==="
DIM y AS DOUBLE
y = 2.71828
PRINT "y AS DOUBLE = 2.71828: ";
PRINT y

PRINT ""
PRINT "All float type tests completed!"
