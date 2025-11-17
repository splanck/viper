REM ═══════════════════════════════════════════════════════
REM  FROGGER V1 - Test game board rendering
REM  Testing: COLOR, LOCATE, loops, string building
REM ═══════════════════════════════════════════════════════

REM Clear screen and setup
DIM width AS INTEGER
DIM height AS INTEGER
width = 40
height = 15

REM Draw game board frame
COLOR 15, 0
PRINT "╔════════════════════════════════════════╗"
PRINT "║        🐸 VIPER FROGGER TEST          ║"
PRINT "╚════════════════════════════════════════╝"
PRINT

REM Test drawing the game lanes
COLOR 10, 0
PRINT "GOAL: "
DIM i AS INTEGER
FOR i = 1 TO width
    PRINT "~";
NEXT i
PRINT

REM Draw road lanes
COLOR 8, 0
FOR i = 1 TO 5
    PRINT "ROAD: ";
    DIM j AS INTEGER
    FOR j = 1 TO width
        PRINT "▓";
    NEXT j
    PRINT
NEXT i

REM Draw starting position
COLOR 10, 0
PRINT "START: "
FOR i = 1 TO width
    PRINT "═";
NEXT i
PRINT

REM Draw frog at starting position
COLOR 10, 0
LOCATE 10, 5
PRINT "  🐸"

PRINT
PRINT
COLOR 15, 0
PRINT "✓ Board rendering test complete!"
