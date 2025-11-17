REM Simplest possible array test

PRINT "Test 1: Declare array"
DIM arr(3) AS INTEGER
PRINT "✓ Array declared"

PRINT "Test 2: Assign value"
arr(0) = 42
PRINT "✓ arr(0) = 42"

PRINT "Test 3: Read value"
DIM val AS INTEGER
val = arr(0)
PRINT "✓ val = "; val

PRINT "Done!"
