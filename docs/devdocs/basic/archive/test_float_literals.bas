REM Test float literal handling and type inference
PRINT "=== Testing Float Literals ==="

REM Assign float literal to variable
x = 3.14159
PRINT "x = 3.14159, result: ";
PRINT x

REM Assign float literal to DIM AS INTEGER
DIM y AS INTEGER
y = 2.71828
PRINT "y AS INTEGER = 2.71828, result: ";
PRINT y

REM Assign float literal to DIM AS STRING - should this work?
DIM pi = 3.14159
PRINT "pi = 3.14159 (no type), result: ";
PRINT pi

REM Try with explicit floating point variable
radius = 5.5
PRINT "radius = 5.5, result: ";
PRINT radius

circumference = 2.0 * 3.14159 * radius
PRINT "circumference = 2.0 * 3.14159 * 5.5, result: ";
PRINT circumference

REM Test division - should produce float?
result = 10 / 3
PRINT "10 / 3 = ";
PRINT result

result2 = 10.0 / 3.0
PRINT "10.0 / 3.0 = ";
PRINT result2
