REM baseball_complete.bas - Full 9-Inning Game (with workarounds)
ADDFILE "baseball_constants.bas"

REM Simplified for testing - focusing on game flow
DIM inning AS INTEGER
DIM outs AS INTEGER
DIM homeScore AS INTEGER
DIM awayScore AS INTEGER
DIM onFirst AS INTEGER
DIM onSecond AS INTEGER
DIM onThird AS INTEGER
DIM battingHome AS INTEGER
DIM currentBatter AS INTEGER

SUB ClearBases()
    onFirst = 0
    onSecond = 0
    onThird = 0
END SUB

SUB ShowScoreboard()
    PRINT ""
    PRINT BOLD$; YELLOW$; "╔══════════════════════╗"; RESET$
    PRINT YELLOW$; "║  INNING: "; inning; "/9        ║"; RESET$
    PRINT YELLOW$; "║  OUTS: "; outs; "            ║"; RESET$
    PRINT YELLOW$; "║ "; BOLD$; " DODGERS: "; awayScore; RESET$; YELLOW$; "       ║"; RESET$
    PRINT YELLOW$; "║ "; BOLD$; " GIANTS: "; homeScore; RESET$; YELLOW$; "        ║"; RESET$
    IF battingHome THEN
        PRINT YELLOW$; "║  🏏 GIANTS BAT      ║"; RESET$
    ELSE
        PRINT YELLOW$; "║  🏏 DODGERS BAT     ║"; RESET$
    END IF
    PRINT BOLD$; YELLOW$; "╚══════════════════════╝"; RESET$
END SUB

SUB ShowDiamond()
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
        IF battingHome THEN
            homeScore = homeScore + runsScored
        ELSE
            awayScore = awayScore + runsScored
        END IF
    END IF
END SUB

SUB PlayHalfInning()
    outs = 0
    ClearBases()
    currentBatter = 1

    IF battingHome THEN
        PRINT ""
        PRINT BLUE$; "⚾ GIANTS batting..."; RESET$
    ELSE
        PRINT ""
        PRINT YELLOW$; "⚾ DODGERS batting..."; RESET$
    END IF

    DO WHILE outs < 3
        ShowScoreboard()
        ShowDiamond()
        PRINT "🏏 Batter #"; currentBatter; " up..."

        DIM result AS INTEGER
        result = SimulateAtBat()
        ProcessHit(result)
        PRINT ""

        currentBatter = currentBatter + 1
        IF currentBatter > 9 THEN currentBatter = 1
    LOOP

    PRINT BOLD$; "✓ Side retired!"; RESET$
    PRINT ""
END SUB

REM Initialize game
homeScore = 0
awayScore = 0

PRINT BOLD$; CYAN$
PRINT "╔══════════════════════════════╗"
PRINT "║  ⚾ VIPER BASEBALL GAME ⚾   ║"
PRINT "║    DODGERS vs GIANTS        ║"
PRINT "║      9 INNING GAME          ║"
PRINT "╚══════════════════════════════╝"
PRINT RESET$

REM Play all 9 innings (BUG-078 FIX CONFIRMED!)
FOR inning = 1 TO 9
    PRINT ""
    PRINT BOLD$; CYAN$; "════════ INNING "; inning; " ════════"; RESET$

    REM Top of inning - away team
    battingHome = 0
    PlayHalfInning()

    REM Bottom of inning - home team
    battingHome = 1
    PlayHalfInning()
NEXT

PRINT ""
PRINT BOLD$; CYAN$; "════════ FINAL SCORE ════════"; RESET$
ShowScoreboard()
PRINT ""

IF homeScore > awayScore THEN
    PRINT BOLD$; GREEN$; "🏆 GIANTS WIN! 🏆"; RESET$
    PRINT BOLD$; GREEN$; "Final: GIANTS "; homeScore; ", DODGERS "; awayScore; RESET$
ELSE IF awayScore > homeScore THEN
    PRINT BOLD$; RED$; "🏆 DODGERS WIN! 🏆"; RESET$
    PRINT BOLD$; RED$; "Final: DODGERS "; awayScore; ", GIANTS "; homeScore; RESET$
ELSE
    PRINT BOLD$; YELLOW$; "⚖ TIE GAME! ⚖"; RESET$
    PRINT BOLD$; YELLOW$; "Final: "; homeScore; "-"; awayScore; RESET$
END IF
PRINT ""

PRINT BOLD$; MAGENTA$; "✓ Complete 9-inning game finished!"; RESET$
PRINT ""
