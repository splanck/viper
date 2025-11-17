REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXT ADVENTURE - Room Class Test                  ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Testing: OOP classes, strings, methods

CLASS Room
    name AS STRING
    description AS STRING
    visited AS INTEGER

    SUB Init(roomName AS STRING, roomDesc AS STRING)
        ME.name = roomName
        ME.description = roomDesc
        ME.visited = 0
    END SUB

    SUB Describe()
        COLOR 14, 0
        PRINT "═══════════════════════════════════════"
        PRINT ME.name
        PRINT "═══════════════════════════════════════"
        COLOR 15, 0
        PRINT ME.description
        PRINT

        IF ME.visited = 0 THEN
            COLOR 10, 0
            PRINT "(First time here!)"
            COLOR 15, 0
            ME.visited = 1
        ELSE
            COLOR 8, 0
            PRINT "(Visited ", ME.visited, " times)"
            COLOR 15, 0
            ME.visited = ME.visited + 1
        END IF
        PRINT
    END SUB

    FUNCTION HasBeenVisited() AS INTEGER
        IF ME.visited > 0 THEN
            RETURN 1
        END IF
        RETURN 0
    END FUNCTION
END CLASS

REM ═══ TEST ROOM CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         ROOM CLASS STRESS TEST                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

DIM entrance AS Room
entrance = NEW Room()
entrance.Init("Grand Entrance", "A magnificent hall with marble floors and towering columns.")

DIM library AS Room
library = NEW Room()
library.Init("Ancient Library", "Dusty bookshelves line the walls, filled with forgotten knowledge.")

REM Visit entrance multiple times
PRINT "First visit to entrance:"
entrance.Describe()

PRINT "Second visit to entrance:"
entrance.Describe()

PRINT "First visit to library:"
library.Describe()

PRINT "Third visit to entrance:"
entrance.Describe()

REM Test visited status
COLOR 11, 0
PRINT "═══ VISIT STATUS ═══"
COLOR 15, 0
IF entrance.HasBeenVisited() THEN
    PRINT "✓ Entrance has been visited"
ELSE
    PRINT "✗ Entrance has NOT been visited"
END IF

IF library.HasBeenVisited() THEN
    PRINT "✓ Library has been visited"
ELSE
    PRINT "✗ Library has NOT been visited"
END IF

PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  ROOM CLASS TEST COMPLETE!                             ║"
PRINT "╚════════════════════════════════════════════════════════╝"
