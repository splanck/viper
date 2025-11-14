REM Test CONST with numeric type suffixes
PRINT "=== Testing CONST with Type Suffixes ==="

CONST PI! = 3.14159
PRINT "CONST PI! = 3.14159: ";
PRINT PI!

CONST E# = 2.71828
PRINT "CONST E# = 2.71828: ";
PRINT E#

CONST MAX% = 100
PRINT "CONST MAX% = 100: ";
PRINT MAX%

PRINT ""
PRINT "=== Using Float CONST in Calculations ==="
radius! = 5.5
circumference! = 2 * PI! * radius!
PRINT "circumference = 2 * PI * 5.5: ";
PRINT circumference!

area# = PI! * radius! * radius!
PRINT "area = PI * r²: ";
PRINT area#

PRINT ""
PRINT "All CONST numeric type suffix tests passed!"
