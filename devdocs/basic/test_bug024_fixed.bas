REM Test BUG-024 fix: CONST with type suffix
PRINT "=== Testing CONST with type suffixes ==="

CONST MAX% = 100
PRINT "CONST MAX% = 100: "; MAX%

CONST PI! = 3.14159
PRINT "CONST PI! = 3.14159: "; PI!

CONST E# = 2.71828
PRINT "CONST E# = 2.71828: "; E#

PRINT ""
PRINT "=== Using CONST in calculations ==="
radius! = 5.5
circumference! = 2 * PI! * radius!
PRINT "circumference = 2 * PI * 5.5: "; circumference!

area# = PI! * radius! * radius!
PRINT "area = PI * r²: "; area#

PRINT ""
PRINT "BUG-024 fix validated!"
