REM Test REDIM PRESERVE syntax (BUG-008 fix)
PRINT "=== Testing REDIM PRESERVE ==="
DIM arr(5)
arr(0) = 100
arr(1) = 200
arr(2) = 300

PRINT "Original size: ";
PRINT UBOUND(arr)
PRINT "arr(0) = ";
PRINT arr(0)
PRINT "arr(1) = ";
PRINT arr(1)
PRINT "arr(2) = ";
PRINT arr(2)

PRINT ""
PRINT "Resizing with REDIM PRESERVE..."
REDIM PRESERVE arr(10)

PRINT "New size: ";
PRINT UBOUND(arr)
PRINT "arr(0) = ";
PRINT arr(0)
PRINT "arr(1) = ";
PRINT arr(1)
PRINT "arr(2) = ";
PRINT arr(2)

PRINT ""
PRINT "REDIM PRESERVE test passed!"
