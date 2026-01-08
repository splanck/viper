' Test: I/O & Console
' Tests: PRINT, INPUT

PRINT "=== I/O Console Test ==="

' Test PRINT with newline
PRINT ""
PRINT "--- PRINT (with newline) ---"
PRINT "Hello, World!"

' Test PRINT with semicolon (no newline)
PRINT ""
PRINT "--- PRINT with semicolon ---"
PRINT "Part 1... ";
PRINT "Part 2... ";
PRINT "Done!"

' Test numeric output
PRINT ""
PRINT "--- Numeric Output ---"
PRINT "Integer: "; 42
PRINT "Float: "; 3.14159

' Test formatted output
PRINT ""
PRINT "--- Formatted Output ---"
PRINT "STR$(42) = "; STR$(42)
PRINT "TRUE = "; TRUE
PRINT "FALSE = "; FALSE

PRINT ""
PRINT "=== I/O Console test complete ==="
