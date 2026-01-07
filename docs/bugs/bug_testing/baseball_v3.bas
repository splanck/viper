REM baseball_v3.bas - With BUG-078 workaround
REM ANSI colors
CONST RED$ = CHR$(27) + "[31m"
CONST GREEN$ = CHR$(27) + "[32m"
CONST YELLOW$ = CHR$(27) + "[33m"
CONST CYAN$ = CHR$(27) + "[36m"
CONST RESET$ = CHR$(27) + "[0m"
CONST BOLD$ = CHR$(27) + "[1m"

REM Game state
DIM homeScore AS INTEGER
DIM awayScore AS INTEGER
DIM currentInning AS INTEGER
DIM outs AS INTEGER
DIM onFirst AS INTEGER
DIM onSecond AS INTEGER
DIM onThird AS INTEGER

SUB ClearBases()
    onFirst = 0
    onSecond = 0
    onThird = 0
END SUB

SUB ShowField()
    PRINT ""
    PRINT BOLD$; CYAN$; "    ⚾ DIAMOND ⚾"; RESET$
    PRINT "         ◆"
    IF onSecond THEN
        PRINT "      "; GREEN$; "[2B]"; RESET$
    ELSE
        PRINT "       2B"
    END IF
    PRINT "     ◆   ◆"
    IF onThird THEN
        PRINT "    "; GREEN$; "[3B]"; RESET$
    ELSE
        PRINT "     3B"
    END IF
    PRINT "         ";
    IF onFirst THEN
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
    PRINT YELLOW$; "║  INNING: "; currentInning; "/9      ║"; RESET$
    PRINT YELLOW$; "║  OUTS: "; outs; "          ║"; RESET$
    PRINT YELLOW$; "║  AWAY: "; awayScore; "          ║"; RESET$
    PRINT YELLOW$; "║  HOME: "; homeScore; "          ║"; RESET$
    PRINT BOLD$; YELLOW$; "╚════════════════════╝"; RESET$
END SUB

FUNCTION SimulateAtBat() AS INTEGER
    DIM roll AS INTEGER
    roll = INT(RND() * 100)
    IF roll < 60 THEN
        SimulateAtBat = 0  REM Out
    ELSE IF roll < 80 THEN
        SimulateAtBat = 1  REM Single
    ELSE IF roll < 92 THEN
        SimulateAtBat = 2  REM Double
    ELSE IF roll < 98 THEN
        SimulateAtBat = 3  REM Triple
    ELSE
        SimulateAtBat = 4  REM Homer
    END IF
END FUNCTION

SUB ProcessHit(hitType AS INTEGER)
    DIM runsScored AS INTEGER
    runsScored = 0

    IF hitType = 0 THEN
        PRINT RED$; "  ⚠ OUT!"; RESET$
        outs = outs + 1
    ELSE IF hitType = 1 THEN
        PRINT GREEN$; "  ✓ SINGLE!"; RESET$
        IF onThird THEN runsScored = runsScored + 1
        onThird = onSecond
        onSecond = onFirst
        onFirst = 1
    ELSE IF hitType = 2 THEN
        PRINT GREEN$; BOLD$; "  ✓✓ DOUBLE!"; RESET$
        IF onThird THEN runsScored = runsScored + 1
        IF onSecond THEN runsScored = runsScored + 1
        onThird = onFirst
        onSecond = 1
        onFirst = 0
    ELSE IF hitType = 3 THEN
        PRINT GREEN$; BOLD$; "  ✓✓✓ TRIPLE!"; RESET$
        IF onThird THEN runsScored = runsScored + 1
        IF onSecond THEN runsScored = runsScored + 1
        IF onFirst THEN runsScored = runsScored + 1
        onThird = 1
        onSecond = 0
        onFirst = 0
    ELSE IF hitType = 4 THEN
        PRINT YELLOW$; BOLD$; "  ⚡ HOME RUN! ⚡"; RESET$
        runsScored = 1
        IF onFirst THEN runsScored = runsScored + 1
        IF onSecond THEN runsScored = runsScored + 1
        IF onThird THEN runsScored = runsScored + 1
        ClearBases()
    END IF

    IF runsScored > 0 THEN
        PRINT BOLD$; GREEN$; "  🎉 +"; runsScored; " RUN";
        IF runsScored > 1 THEN PRINT "S";
        PRINT "!"; RESET$
        awayScore = awayScore + runsScored
    END IF
END SUB

REM Initialize
homeScore = 0
awayScore = 0
currentInning = 1

PRINT BOLD$; CYAN$
PRINT "╔══════════════════════════════╗"
PRINT "║  ⚾ VIPER BASEBALL GAME ⚾   ║"
PRINT "╚══════════════════════════════╝"
PRINT RESET$
PRINT ""

REM WORKAROUND BUG-078: Use local loop var, copy to global
DIM i AS INTEGER
FOR i = 1 TO 3
    currentInning = i  REM Copy to global
    PRINT ""
    PRINT BOLD$; CYAN$; "════════ INNING "; currentInning; " ════════"; RESET$

    REM Away team bats
    PRINT YELLOW$; "⚾ Away team batting..."; RESET$
    outs = 0
    ClearBases()

    DO WHILE outs < 3
        ShowScoreboard()
        ShowField()
        PRINT "🏏 Batter up..."
        DIM result AS INTEGER
        result = SimulateAtBat()
        ProcessHit(result)
        PRINT ""
    LOOP

    PRINT BOLD$; "✓ Side retired!"; RESET$
NEXT

ShowScoreboard()
PRINT ""
IF homeScore > awayScore THEN
    PRINT BOLD$; GREEN$; "🏆 HOME TEAM WINS! 🏆"; RESET$
ELSE IF awayScore > homeScore THEN
    PRINT BOLD$; RED$; "🏆 AWAY TEAM WINS! 🏆"; RESET$
ELSE
    PRINT BOLD$; YELLOW$; "⚖ TIE GAME! ⚖"; RESET$
END IF
PRINT ""
