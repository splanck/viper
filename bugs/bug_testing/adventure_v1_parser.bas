REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXT ADVENTURE - Command Parser Test              ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Testing: SELECT CASE, string operations, INPUT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         COMMAND PARSER STRESS TEST                     ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Test SELECT CASE with string commands
DIM command AS STRING
DIM testNum AS INTEGER

testNum = 1
PRINT "Test "; testNum; ": SELECT CASE with 'north'"
command = "north"

SELECT CASE command
    CASE "north"
        PRINT "✓ Matched 'north' - moving north"
    CASE "south"
        PRINT "✗ Should not match 'south'"
    CASE "east"
        PRINT "✗ Should not match 'east'"
    CASE "west"
        PRINT "✗ Should not match 'west'"
    CASE ELSE
        PRINT "✗ Should not reach ELSE"
END SELECT
PRINT

testNum = testNum + 1
PRINT "Test "; testNum; ": SELECT CASE with 'look'"
command = "look"

SELECT CASE command
    CASE "look"
        PRINT "✓ Matched 'look' - describing room"
    CASE "inventory"
        PRINT "✗ Should not match 'inventory'"
    CASE "quit"
        PRINT "✗ Should not match 'quit'"
    CASE ELSE
        PRINT "✗ Should not reach ELSE"
END SELECT
PRINT

testNum = testNum + 1
PRINT "Test "; testNum; ": SELECT CASE with unknown command"
command = "dance"

SELECT CASE command
    CASE "north"
        PRINT "✗ Should not match direction"
    CASE "look"
        PRINT "✗ Should not match 'look'"
    CASE ELSE
        PRINT "✓ Correctly fell through to ELSE - unknown command"
END SELECT
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  SELECT CASE TEST COMPLETE!                            ║"
PRINT "╚════════════════════════════════════════════════════════╝"
