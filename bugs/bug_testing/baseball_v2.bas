REM baseball_v2.bas - Building incrementally to avoid crash
REM ANSI colors
CONST RED$ = CHR$(27) + "[31m"
CONST GREEN$ = CHR$(27) + "[32m"
CONST YELLOW$ = CHR$(27) + "[33m"
CONST RESET$ = CHR$(27) + "[0m"
CONST BOLD$ = CHR$(27) + "[1m"

REM Game state
DIM homeScore AS INTEGER
DIM awayScore AS INTEGER
DIM inning AS INTEGER
DIM outs AS INTEGER

SUB ShowScoreboard()
    PRINT ""
    PRINT BOLD$; YELLOW$; "════ SCOREBOARD ════"; RESET$
    PRINT "Inning: "; inning; "/9"
    PRINT "Outs: "; outs
    PRINT "Away: "; awayScore
    PRINT "Home: "; homeScore
    PRINT YELLOW$; "════════════════════"; RESET$
END SUB

FUNCTION SimulateAtBat() AS INTEGER
    DIM roll AS INTEGER
    roll = INT(RND() * 100)
    IF roll < 70 THEN
        SimulateAtBat = 0  REM Out
    ELSE
        SimulateAtBat = 1  REM Hit
    END IF
END FUNCTION

REM Initialize
homeScore = 0
awayScore = 0
inning = 1
outs = 0

PRINT BOLD$; "VIPER BASEBALL"; RESET$
PRINT ""

REM Simple 2-inning game
FOR inning = 1 TO 2
    PRINT ""
    PRINT "═══ INNING "; inning; " ═══"

    REM Away team bats
    outs = 0
    DO WHILE outs < 3
        ShowScoreboard()
        DIM result AS INTEGER
        result = SimulateAtBat()
        IF result = 0 THEN
            PRINT RED$; "OUT!"; RESET$
            outs = outs + 1
        ELSE
            PRINT GREEN$; "HIT!"; RESET$
            awayScore = awayScore + 1
        END IF
    LOOP
    PRINT "Side retired!"
NEXT

ShowScoreboard()
PRINT ""
IF awayScore > homeScore THEN
    PRINT BOLD$; "AWAY WINS!"; RESET$
ELSE
    PRINT BOLD$; "HOME WINS!"; RESET$
END IF
