REM Test CONST with type suffixes
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

CONST MSG$ = "Hello World"
PRINT "CONST MSG$ = Hello World: ";
PRINT MSG$

PRINT ""
PRINT "=== Using Float CONST in Calculations ==="
radius! = 5.5
circumference! = 2 * PI! * radius!
PRINT "circumference = 2 * PI * 5.5: ";
PRINT circumference!

PRINT ""
PRINT "All CONST type suffix tests completed!"
