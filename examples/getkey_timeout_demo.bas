REM Demo of GETKEY$ with timeout
REM This demonstrates timeout mode

PRINT "GETKEY$ Timeout Demo"
PRINT "===================="
PRINT ""

PRINT "Test 1: Timeout mode (1000ms = 1 second)"
PRINT "You have 1 second to press a key..."
DIM key1$ AS STRING
key1$ = GETKEY$(1000)
IF LEN(key1$) = 0 THEN
    PRINT "Timeout! No key pressed."
ELSE
    PRINT "You pressed: "; key1$
END IF
PRINT ""

PRINT "Test 2: Short timeout (500ms)"
PRINT "You have 0.5 seconds to press a key..."
DIM key2$ AS STRING
key2$ = GETKEY$(500)
IF LEN(key2$) = 0 THEN
    PRINT "Timeout! Too slow."
ELSE
    PRINT "Quick! You pressed: "; key2$
END IF
PRINT ""

PRINT "Demo complete!"
