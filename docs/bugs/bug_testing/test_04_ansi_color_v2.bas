' Test 04: ANSI color codes (workaround - hardcoded ESC)
' Note: CHR() function not available, using workaround

' Using literal escape character in string (may not work)
DIM RED AS STRING
DIM GREEN AS STRING
DIM YELLOW AS STRING
DIM BLUE AS STRING
DIM MAGENTA AS STRING
DIM CYAN AS STRING
DIM RESET AS STRING

RED = "[31m"
GREEN = "[32m"
YELLOW = "[33m"
BLUE = "[34m"
MAGENTA = "[35m"
CYAN = "[36m"
RESET = "[0m"

PRINT "Testing colors (no ESC prefix):"
PRINT RED + "Red text" + RESET
PRINT GREEN + "Green text" + RESET
PRINT YELLOW + "Yellow text" + RESET
PRINT BLUE + "Blue text" + RESET
END
