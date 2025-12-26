REM baseball_simple.bas - Simplified baseball without arrays of objects
REM Testing: ANSI colors, game logic, random numbers

REM ANSI color codes
CONST ESC$ = CHR$(27)
CONST RESET$ = ESC$ + "[0m"
CONST RED$ = ESC$ + "[31m"
CONST GREEN$ = ESC$ + "[32m"
CONST YELLOW$ = ESC$ + "[33m"
CONST BLUE$ = ESC$ + "[34m"
CONST CYAN$ = ESC$ + "[36m"
CONST WHITE$ = ESC$ + "[37m"
CONST BOLD$ = ESC$ + "[1m"

REM Game state
DIM homeScore AS INTEGER
DIM awayScore AS INTEGER
DIM inning AS INTEGER
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
    PRINT BOLD$; CYAN$; "    BASEBALL FIELD"; RESET$
    PRINT ""
    PRINT "         ◆"
    IF onSecond THEN
        PRINT "      "; GREEN$; "[2B]"; RESET$
    ELSE
        PRINT "       2B"
    END IF
    PRINT "     ◆   ◆"
    PRINT "   ";
    IF onThird THEN
        PRINT GREEN$; "[3B]"; RESET$
    ELSE
        PRINT "3B"
    END IF
    PRINT "   ";
    IF onFirst THEN
        PRINT GREEN$; "[1B]"; RESET$
    ELSE
        PRINT "1B"
    END IF
    PRINT ""
    PRINT "      HOME"
    PRINT ""
END SUB

SUB ShowScoreboard()
    PRINT ""
    PRINT BOLD$; YELLOW$; "╔════════════════════════╗"; RESET$
    PRINT BOLD$; YELLOW$; "║    SCOREBOARD         ║"; RESET$
    PRINT BOLD$; YELLOW$; "╠════════════════════════╣"; RESET$
    PRINT YELLOW$; "║ Inning: "; inning; "/9        ║"; RESET$
    PRINT YELLOW$; "║ Outs: "; outs; "            ║"; RESET$
    PRINT YELLOW$; "║ Away: "; awayScore; "            ║"; RESET$
    PRINT YELLOW$; "║ Home: "; homeScore; "            ║"; RESET$
    PRINT BOLD$; YELLOW$; "╚════════════════════════╝"; RESET$
    PRINT ""
END SUB

FUNCTION SimulateAtBat() AS INTEGER
    REM Returns: 0=out, 1=single, 2=double, 3=triple, 4=home run
    DIM roll AS INTEGER
    roll = INT(RND() * 100)
    IF roll < 65 THEN
        SimulateAtBat = 0  REM Out
    ELSE IF roll < 85 THEN
        SimulateAtBat = 1  REM Single
    ELSE IF roll < 93 THEN
        SimulateAtBat = 2  REM Double
    ELSE IF roll < 98 THEN
        SimulateAtBat = 3  REM Triple
    ELSE
        SimulateAtBat = 4  REM Home run
    END IF
END FUNCTION

SUB ProcessHit(hitType AS INTEGER)
    DIM runsScored AS INTEGER
    runsScored = 0

    SELECT CASE hitType
        CASE 0  REM Out
            PRINT RED$; "  OUT!"; RESET$
            outs = outs + 1
        CASE 1  REM Single
            PRINT GREEN$; "  SINGLE!"; RESET$
            IF onThird THEN runsScored = runsScored + 1
            onThird = onSecond
            onSecond = onFirst
            onFirst = 1
        CASE 2  REM Double
            PRINT GREEN$; BOLD$; "  DOUBLE!"; RESET$
            IF onThird THEN runsScored = runsScored + 1
            IF onSecond THEN runsScored = runsScored + 1
            onThird = onFirst
            onSecond = 1
            onFirst = 0
        CASE 3  REM Triple
            PRINT GREEN$; BOLD$; "  TRIPLE!"; RESET$
            IF onThird THEN runsScored = runsScored + 1
            IF onSecond THEN runsScored = runsScored + 1
            IF onFirst THEN runsScored = runsScored + 1
            onThird = 1
            onSecond = 0
            onFirst = 0
        CASE 4  REM Home run
            PRINT YELLOW$; BOLD$; "  HOME RUN!"; RESET$
            runsScored = 1
            IF onFirst THEN runsScored = runsScored + 1
            IF onSecond THEN runsScored = runsScored + 1
            IF onThird THEN runsScored = runsScored + 1
            ClearBases()
    END SELECT

    IF runsScored > 0 THEN
        PRINT BOLD$; "  +"; runsScored; " RUN";
        IF runsScored > 1 THEN PRINT "S";
        PRINT "!"; RESET$
        IF inning <= 9 THEN
            awayScore = awayScore + runsScored
        ELSE
            homeScore = homeScore + runsScored
        END IF
    END IF
END SUB

REM Initialize game
homeScore = 0
awayScore = 0
inning = 1
outs = 0
ClearBases()

PRINT BOLD$; CYAN$
PRINT "╔═══════════════════════════════════╗"
PRINT "║  VIPER BASIC BASEBALL SIMULATOR  ║"
PRINT "╚═══════════════════════════════════╝"
PRINT RESET$

REM Play a quick 3-inning game for testing
FOR inning = 1 TO 3
    PRINT ""
    PRINT BOLD$; "═══ INNING "; inning; " ═══"; RESET$

    REM Away team bats
    PRINT ""
    PRINT BLUE$; "Away team batting..."; RESET$
    outs = 0
    ClearBases()

    DO WHILE outs < 3
        ShowScoreboard()
        ShowField()
        PRINT "Batter up..."
        DIM result AS INTEGER
        result = SimulateAtBat()
        ProcessHit(result)
    LOOP

    PRINT ""
    PRINT "Side retired!"
NEXT

ShowScoreboard()
PRINT ""
IF homeScore > awayScore THEN
    PRINT BOLD$; GREEN$; "HOME TEAM WINS!"; RESET$
ELSE IF awayScore > homeScore THEN
    PRINT BOLD$; RED$; "AWAY TEAM WINS!"; RESET$
ELSE
    PRINT BOLD$; YELLOW$; "TIE GAME!"; RESET$
END IF
PRINT ""
