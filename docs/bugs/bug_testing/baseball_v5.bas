REM baseball_v5.bas - With player rosters (BUG-079 workaround)
REM ANSI colors
CONST RED$ = CHR$(27) + "[31m"
CONST GREEN$ = CHR$(27) + "[32m"
CONST YELLOW$ = CHR$(27) + "[33m"
CONST CYAN$ = CHR$(27) + "[36m"
CONST BLUE$ = CHR$(27) + "[34m"
CONST RESET$ = CHR$(27) + "[0m"
CONST BOLD$ = CHR$(27) + "[1m"

REM Player rosters
DIM awayNames(9) AS STRING
DIM homeNames(9) AS STRING

REM WORKAROUND BUG-079: Assign at module level only
awayNames(1) = "Trout"
awayNames(2) = "Ohtani"
awayNames(3) = "Judge"
awayNames(4) = "Betts"
awayNames(5) = "Freeman"
awayNames(6) = "Soto"
awayNames(7) = "Harper"
awayNames(8) = "Turner"
awayNames(9) = "Acuna"

homeNames(1) = "Lindor"
homeNames(2) = "Arenado"
homeNames(3) = "Devers"
homeNames(4) = "Guerrero"
homeNames(5) = "Tatis"
homeNames(6) = "Seager"
homeNames(7) = "Olson"
homeNames(8) = "Riley"
homeNames(9) = "Alvarez"

REM Game state
DIM homeScore AS INTEGER
DIM awayScore AS INTEGER
DIM currentInning AS INTEGER
DIM outs AS INTEGER
DIM onFirst AS INTEGER
DIM onSecond AS INTEGER
DIM onThird AS INTEGER
DIM battingHome AS INTEGER
DIM currentBatterNum AS INTEGER

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
    PRINT YELLOW$; "║ "; BOLD$; " DODGERS: "; awayScore; RESET$; YELLOW$; "      ║"; RESET$
    PRINT YELLOW$; "║ "; BOLD$; " GIANTS: "; homeScore; RESET$; YELLOW$; "       ║"; RESET$
    IF battingHome THEN
        PRINT YELLOW$; "║  🏏 GIANTS BAT     ║"; RESET$
    ELSE
        PRINT YELLOW$; "║  🏏 DODGERS BAT    ║"; RESET$
    END IF
    PRINT BOLD$; YELLOW$; "╚════════════════════╝"; RESET$
END SUB

FUNCTION GetBatterName$() AS STRING
    REM WORKAROUND BUG-079: Can't read string arrays in functions
    REM So we return empty string - names shown differently
    GetBatterName$ = ""
END FUNCTION

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

SUB PlayHalfInning(isHome AS INTEGER)
    battingHome = isHome
    IF isHome THEN
        PRINT BLUE$; "⚾ GIANTS batting..."; RESET$
    ELSE
        PRINT YELLOW$; "⚾ DODGERS batting..."; RESET$
    END IF

    outs = 0
    ClearBases()
    currentBatterNum = 1

    DO WHILE outs < 3
        ShowScoreboard()
        ShowField()

        REM Show batter name (WORKAROUND: access arrays at module scope)
        DIM batterName AS STRING
        IF isHome THEN
            REM Can't call function to get name due to BUG-079
            REM Just show number for now
            PRINT "🏏 Batter #"; currentBatterNum; " up..."
        ELSE
            PRINT "🏏 Batter #"; currentBatterNum; " up..."
        END IF

        DIM result AS INTEGER
        result = SimulateAtBat()
        ProcessHit(result)
        PRINT ""
        currentBatterNum = currentBatterNum + 1
        IF currentBatterNum > 9 THEN currentBatterNum = 1
    LOOP

    PRINT BOLD$; "✓ Side retired!"; RESET$
    PRINT ""
END SUB

REM Initialize
homeScore = 0
awayScore = 0
currentInning = 1
battingHome = 0

PRINT BOLD$; CYAN$
PRINT "╔══════════════════════════════╗"
PRINT "║  ⚾ VIPER BASEBALL GAME ⚾   ║"
PRINT "║     DODGERS vs GIANTS       ║"
PRINT "╚══════════════════════════════╝"
PRINT RESET$
PRINT ""

REM WORKAROUND BUG-078: Use local loop var, copy to global
DIM i AS INTEGER
FOR i = 1 TO 3
    currentInning = i  REM Copy to global
    PRINT ""
    PRINT BOLD$; CYAN$; "════════ INNING "; currentInning; " ════════"; RESET$
    PRINT ""

    REM Top of inning - away team bats
    PlayHalfInning(0)

    REM Bottom of inning - home team bats
    PlayHalfInning(1)
NEXT

PRINT ""
PRINT BOLD$; CYAN$; "════════ FINAL SCORE ════════"; RESET$
ShowScoreboard()
PRINT ""
IF homeScore > awayScore THEN
    PRINT BOLD$; GREEN$; "🏆 GIANTS WIN! 🏆"; RESET$
ELSE IF awayScore > homeScore THEN
    PRINT BOLD$; RED$; "🏆 DODGERS WIN! 🏆"; RESET$
ELSE
    PRINT BOLD$; YELLOW$; "⚖ TIE GAME! ⚖"; RESET$
END IF
PRINT ""
