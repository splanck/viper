REM baseball_full_v1.bas - Complete OOP Baseball Game
REM Using workarounds for remaining bugs

ADDFILE "baseball_constants.bas"

REM Player class with full stats
CLASS Player
    name AS STRING
    position AS STRING
    battingAvg AS INTEGER    REM batting average * 1000 (e.g. 300 = .300)
    hits AS INTEGER
    atBats AS INTEGER
    runs AS INTEGER
    rbis AS INTEGER
    homeruns AS INTEGER

    SUB Init(playerName AS STRING, pos AS STRING, avg AS INTEGER)
        ME.name = playerName
        ME.position = pos
        ME.battingAvg = avg
        ME.hits = 0
        ME.atBats = 0
        ME.runs = 0
        ME.rbis = 0
        ME.homeruns = 0
    END SUB

    SUB RecordAtBat(result AS INTEGER, runsScored AS INTEGER)
        ME.atBats = ME.atBats + 1
        IF result > 0 THEN
            ME.hits = ME.hits + 1
        END IF
        IF result = 4 THEN
            ME.homeruns = ME.homeruns + 1
        END IF
        ME.rbis = ME.rbis + runsScored
        IF ME.atBats > 0 THEN
            ME.battingAvg = (ME.hits * 1000) \ ME.atBats
        END IF
    END SUB

    SUB ShowStats()
        PRINT ME.name; " ("; ME.position; ")"
        PRINT "  BA: .";
        IF ME.battingAvg < 100 THEN PRINT "0";
        IF ME.battingAvg < 10 THEN PRINT "0";
        PRINT ME.battingAvg
        PRINT "  H: "; ME.hits; " AB: "; ME.atBats
        PRINT "  HR: "; ME.homeruns; " RBI: "; ME.rbis; " R: "; ME.runs
    END SUB
END CLASS

REM Game state manager
CLASS GameState
    inning AS INTEGER
    outs AS INTEGER
    homeScore AS INTEGER
    awayScore AS INTEGER
    onFirst AS INTEGER
    onSecond AS INTEGER
    onThird AS INTEGER
    battingHome AS INTEGER

    SUB Init()
        ME.inning = 1
        ME.outs = 0
        ME.homeScore = 0
        ME.awayScore = 0
        ME.onFirst = 0
        ME.onSecond = 0
        ME.onThird = 0
        ME.battingHome = 0
    END SUB

    SUB ClearBases()
        ME.onFirst = 0
        ME.onSecond = 0
        ME.onThird = 0
    END SUB

    SUB ShowDiamond()
        PRINT ""
        PRINT BOLD$; CYAN$; "    ⚾ DIAMOND ⚾"; RESET$
        PRINT "         ◆"
        IF ME.onSecond THEN
            PRINT "      "; GREEN$; "[2B]"; RESET$
        ELSE
            PRINT "       2B"
        END IF
        PRINT "     ◆   ◆"
        IF ME.onThird THEN
            PRINT "    "; GREEN$; "[3B]"; RESET$
        ELSE
            PRINT "     3B"
        END IF
        PRINT "         ";
        IF ME.onFirst THEN
            PRINT GREEN$; "[1B]"; RESET$
        ELSE
            PRINT "1B"
        END IF
        PRINT "       ⌂"
        PRINT ""
    END SUB

    SUB ShowScoreboard()
        PRINT ""
        PRINT BOLD$; YELLOW$; "╔════════════════════╗"; RESET$
        PRINT YELLOW$; "║  INNING: "; ME.inning; "/9      ║"; RESET$
        PRINT YELLOW$; "║  OUTS: "; ME.outs; "          ║"; RESET$
        PRINT YELLOW$; "║ "; BOLD$; " DODGERS: "; ME.awayScore; RESET$; YELLOW$; "      ║"; RESET$
        PRINT YELLOW$; "║ "; BOLD$; " GIANTS: "; ME.homeScore; RESET$; YELLOW$; "       ║"; RESET$
        IF ME.battingHome THEN
            PRINT YELLOW$; "║  🏏 GIANTS BAT     ║"; RESET$
        ELSE
            PRINT YELLOW$; "║  🏏 DODGERS BAT    ║"; RESET$
        END IF
        PRINT BOLD$; YELLOW$; "╚════════════════════╝"; RESET$
    END SUB
END CLASS

REM Test the classes
DIM game AS GameState
game = NEW GameState()
game.Init()

PRINT BOLD$; CYAN$
PRINT "╔══════════════════════════════╗"
PRINT "║  ⚾ VIPER BASEBALL GAME ⚾   ║"
PRINT "║   CLASS SYSTEM TEST v1.0    ║"
PRINT "╚══════════════════════════════╝"
PRINT RESET$
PRINT ""

PRINT "Testing GameState class..."
PRINT ""

game.ShowScoreboard()
game.ShowDiamond()

PRINT ""
PRINT "Adding runs..."
game.awayScore = 2
game.homeScore = 1
game.onFirst = 1
game.onThird = 1
game.outs = 2

game.ShowScoreboard()
game.ShowDiamond()

PRINT ""
PRINT "Testing Player class..."
PRINT ""

DIM player1 AS Player
player1 = NEW Player()
player1.Init("Mike Trout", "CF", 305)

DIM player2 AS Player
player2 = NEW Player()
player2.Init("Shohei Ohtani", "DH", 304)

player1.RecordAtBat(1, 1)  REM Single, 1 RBI
player1.RecordAtBat(4, 1)  REM Homer, 1 RBI
player1.RecordAtBat(0, 0)  REM Out

player2.RecordAtBat(2, 2)  REM Double, 2 RBI
player2.RecordAtBat(1, 0)  REM Single
player2.RecordAtBat(1, 1)  REM Single, 1 RBI

PRINT "Player Statistics:"
PRINT ""
player1.ShowStats()
PRINT ""
player2.ShowStats()
PRINT ""

PRINT BOLD$; GREEN$; "✓ Class system test complete!"; RESET$
PRINT ""
