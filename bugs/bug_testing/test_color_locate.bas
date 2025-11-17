REM Test COLOR and LOCATE features

PRINT "Testing COLOR and LOCATE commands..."
PRINT

REM Test LOCATE
PRINT "Testing LOCATE:"
LOCATE 5, 10
PRINT "This should be at row 5, column 10"
LOCATE 7, 5
PRINT "This should be at row 7, column 5"
PRINT

REM Test COLOR
PRINT "Testing COLOR:"
COLOR 12, 0
PRINT "Red text on black background"
COLOR 10, 0
PRINT "Green text on black background"
COLOR 14, 0
PRINT "Yellow text on black background"
COLOR 9, 0
PRINT "Blue text on black background"
COLOR 15, 0
PRINT "White text (reset)"
PRINT

PRINT "If colors and positioning worked, test passed!"
