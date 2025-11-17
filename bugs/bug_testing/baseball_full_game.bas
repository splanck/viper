REM baseball_full_game.bas - Complete 9-Inning Baseball Game with Full Rosters
ADDFILE "baseball_constants.bas"

REM Player class
CLASS Player
    name AS STRING
    position AS STRING
    battingAvg AS INTEGER
    hits AS INTEGER
    atBats AS INTEGER
    homeruns AS INTEGER

    SUB Init(playerName AS STRING, pos AS STRING)
        ME.name = playerName
        ME.position = pos
        ME.battingAvg = 300
        ME.hits = 0
        ME.atBats = 0
        ME.homeruns = 0
    END SUB

    SUB RecordAtBat(result AS INTEGER)
        ME.atBats = ME.atBats + 1
        IF result > 0 THEN
            ME.hits = ME.hits + 1
        END IF
        IF result = 4 THEN
            ME.homeruns = ME.homeruns + 1
        END IF
        IF ME.atBats > 0 THEN
            ME.battingAvg = (ME.hits * 1000) \ ME.atBats
        END IF
    END SUB
END CLASS

REM Team manager (WORKAROUND BUG-075: individual players instead of array)
CLASS Team
    teamName AS STRING
    score AS INTEGER
    player1 AS Player
    player2 AS Player
    player3 AS Player
    player4 AS Player
    player5 AS Player
    player6 AS Player
    player7 AS Player
    player8 AS Player
    player9 AS Player
    currentBatter AS INTEGER

    SUB Init(name AS STRING)
        ME.teamName = name
        ME.score = 0
        ME.currentBatter = 1
    END SUB

    SUB InitPlayer(num AS INTEGER, name AS STRING, pos AS STRING)
        REM WORKAROUND: Manual dispatch instead of array
        IF num = 1 THEN
            ME.player1 = NEW Player()
            ME.player1.Init(name, pos)
        ELSE IF num = 2 THEN
            ME.player2 = NEW Player()
            ME.player2.Init(name, pos)
        ELSE IF num = 3 THEN
            ME.player3 = NEW Player()
            ME.player3.Init(name, pos)
        ELSE IF num = 4 THEN
            ME.player4 = NEW Player()
            ME.player4.Init(name, pos)
        ELSE IF num = 5 THEN
            ME.player5 = NEW Player()
            ME.player5.Init(name, pos)
        ELSE IF num = 6 THEN
            ME.player6 = NEW Player()
            ME.player6.Init(name, pos)
        ELSE IF num = 7 THEN
            ME.player7 = NEW Player()
            ME.player7.Init(name, pos)
        ELSE IF num = 8 THEN
            ME.player8 = NEW Player()
            ME.player8.Init(name, pos)
        ELSE IF num = 9 THEN
            ME.player9 = NEW Player()
            ME.player9.Init(name, pos)
        END IF
    END SUB

    FUNCTION GetPlayerName$(num AS INTEGER) AS STRING
        IF num = 1 THEN GetPlayerName$ = ME.player1.name
        IF num = 2 THEN GetPlayerName$ = ME.player2.name
        IF num = 3 THEN GetPlayerName$ = ME.player3.name
        IF num = 4 THEN GetPlayerName$ = ME.player4.name
        IF num = 5 THEN GetPlayerName$ = ME.player5.name
        IF num = 6 THEN GetPlayerName$ = ME.player6.name
        IF num = 7 THEN GetPlayerName$ = ME.player7.name
        IF num = 8 THEN GetPlayerName$ = ME.player8.name
        IF num = 9 THEN GetPlayerName$ = ME.player9.name
    END FUNCTION

    SUB RecordResult(num AS INTEGER, result AS INTEGER)
        IF num = 1 THEN ME.player1.RecordAtBat(result)
        IF num = 2 THEN ME.player2.RecordAtBat(result)
        IF num = 3 THEN ME.player3.RecordAtBat(result)
        IF num = 4 THEN ME.player4.RecordAtBat(result)
        IF num = 5 THEN ME.player5.RecordAtBat(result)
        IF num = 6 THEN ME.player6.RecordAtBat(result)
        IF num = 7 THEN ME.player7.RecordAtBat(result)
        IF num = 8 THEN ME.player8.RecordAtBat(result)
        IF num = 9 THEN ME.player9.RecordAtBat(result)
    END SUB

    SUB NextBatter()
        ME.currentBatter = ME.currentBatter + 1
        IF ME.currentBatter > 9 THEN ME.currentBatter = 1
    END SUB
END CLASS

REM Game manager
CLASS Game
    inning AS INTEGER
    outs AS INTEGER
    onFirst AS INTEGER
    onSecond AS INTEGER
    onThird AS INTEGER
    battingHome AS INTEGER
    homeTeam AS Team
    awayTeam AS Team

    SUB Init()
        ME.inning = 1
        ME.outs = 0
        ME.onFirst = 0
        ME.onSecond = 0
        ME.onThird = 0
        ME.battingHome = 0
        ME.homeTeam = NEW Team()
        ME.homeTeam.Init("GIANTS")
        ME.awayTeam = NEW Team()
        ME.awayTeam.Init("DODGERS")
    END SUB

    SUB ClearBases()
        ME.onFirst = 0
        ME.onSecond = 0
        ME.onThird = 0
    END SUB

    SUB ShowScoreboard()
        PRINT ""
        PRINT BOLD$; YELLOW$; "╔══════════════════════╗"; RESET$
        PRINT YELLOW$; "║  INNING: "; ME.inning; "/9        ║"; RESET$
        PRINT YELLOW$; "║  OUTS: "; ME.outs; "            ║"; RESET$
        PRINT YELLOW$; "║ "; BOLD$; " "; ME.awayTeam.teamName; ": "; ME.awayTeam.score; RESET$; YELLOW$; "       ║"; RESET$
        PRINT YELLOW$; "║ "; BOLD$; " "; ME.homeTeam.teamName; ": "; ME.homeTeam.score; RESET$; YELLOW$; "        ║"; RESET$
        IF ME.battingHome THEN
            PRINT YELLOW$; "║  🏏 "; ME.homeTeam.teamName; " BAT  ║"; RESET$
        ELSE
            PRINT YELLOW$; "║  🏏 "; ME.awayTeam.teamName; " BAT ║"; RESET$
        END IF
        PRINT BOLD$; YELLOW$; "╚══════════════════════╝"; RESET$
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

    FUNCTION ProcessHit(hitType AS INTEGER) AS INTEGER
        DIM runsScored AS INTEGER
        runsScored = 0

        IF hitType = 0 THEN
            PRINT RED$; "  ⚠ OUT!"; RESET$
            ME.outs = ME.outs + 1
        ELSE IF hitType = 1 THEN
            PRINT GREEN$; "  ✓ SINGLE!"; RESET$
            IF ME.onThird THEN runsScored = runsScored + 1
            ME.onThird = ME.onSecond
            ME.onSecond = ME.onFirst
            ME.onFirst = 1
        ELSE IF hitType = 2 THEN
            PRINT GREEN$; BOLD$; "  ✓✓ DOUBLE!"; RESET$
            IF ME.onThird THEN runsScored = runsScored + 1
            IF ME.onSecond THEN runsScored = runsScored + 1
            ME.onThird = ME.onFirst
            ME.onSecond = 1
            ME.onFirst = 0
        ELSE IF hitType = 3 THEN
            PRINT GREEN$; BOLD$; "  ✓✓✓ TRIPLE!"; RESET$
            IF ME.onThird THEN runsScored = runsScored + 1
            IF ME.onSecond THEN runsScored = runsScored + 1
            IF ME.onFirst THEN runsScored = runsScored + 1
            ME.onThird = 1
            ME.onSecond = 0
            ME.onFirst = 0
        ELSE IF hitType = 4 THEN
            PRINT YELLOW$; BOLD$; "  ⚡ HOME RUN! ⚡"; RESET$
            runsScored = 1
            IF ME.onFirst THEN runsScored = runsScored + 1
            IF ME.onSecond THEN runsScored = runsScored + 1
            IF ME.onThird THEN runsScored = runsScored + 1
            ME.ClearBases()
        END IF

        IF runsScored > 0 THEN
            PRINT BOLD$; GREEN$; "  🎉 +"; runsScored; " RUN";
            IF runsScored > 1 THEN PRINT "S";
            PRINT "!"; RESET$
            IF ME.battingHome THEN
                ME.homeTeam.score = ME.homeTeam.score + runsScored
            ELSE
                ME.awayTeam.score = ME.awayTeam.score + runsScored
            END IF
        END IF

        ProcessHit = runsScored
    END FUNCTION

    SUB PlayHalfInning()
        ME.outs = 0
        ME.ClearBases()

        DIM team AS Team
        DIM teamColor AS STRING

        IF ME.battingHome THEN
            team = ME.homeTeam
            teamColor = BLUE$
        ELSE
            team = ME.awayTeam
            teamColor = YELLOW$
        END IF

        PRINT ""
        PRINT teamColor; "⚾ "; team.teamName; " batting..."; RESET$
        PRINT ""

        DO WHILE ME.outs < 3
            ME.ShowScoreboard()
            ME.ShowDiamond()

            DIM batterName AS STRING
            batterName = team.GetPlayerName$(team.currentBatter)
            PRINT "🏏 #"; team.currentBatter; " "; BOLD$; batterName; RESET$; " up..."

            DIM result AS INTEGER
            result = ME.SimulateAtBat()
            team.RecordResult(team.currentBatter, result)

            DIM runs AS INTEGER
            runs = ME.ProcessHit(result)

            team.NextBatter()
            PRINT ""
        LOOP

        PRINT BOLD$; "✓ Side retired!"; RESET$
    END SUB
END CLASS

REM Initialize game
DIM game AS Game
game = NEW Game()
game.Init()

REM Setup Dodgers lineup
game.awayTeam.InitPlayer(1, "Trout", "CF")
game.awayTeam.InitPlayer(2, "Ohtani", "DH")
game.awayTeam.InitPlayer(3, "Judge", "RF")
game.awayTeam.InitPlayer(4, "Betts", "RF")
game.awayTeam.InitPlayer(5, "Freeman", "1B")
game.awayTeam.InitPlayer(6, "Soto", "LF")
game.awayTeam.InitPlayer(7, "Harper", "RF")
game.awayTeam.InitPlayer(8, "Turner", "SS")
game.awayTeam.InitPlayer(9, "Acuna", "CF")

REM Setup Giants lineup
game.homeTeam.InitPlayer(1, "Lindor", "SS")
game.homeTeam.InitPlayer(2, "Arenado", "3B")
game.homeTeam.InitPlayer(3, "Devers", "3B")
game.homeTeam.InitPlayer(4, "Guerrero", "1B")
game.homeTeam.InitPlayer(5, "Tatis", "SS")
game.homeTeam.InitPlayer(6, "Seager", "SS")
game.homeTeam.InitPlayer(7, "Olson", "1B")
game.homeTeam.InitPlayer(8, "Riley", "3B")
game.homeTeam.InitPlayer(9, "Alvarez", "DH")

REM Play game
PRINT BOLD$; CYAN$
PRINT "╔══════════════════════════════╗"
PRINT "║  ⚾ VIPER BASEBALL GAME ⚾   ║"
PRINT "║    DODGERS vs GIANTS        ║"
PRINT "║      9 INNING GAME          ║"
PRINT "╚══════════════════════════════╝"
PRINT RESET$

REM BUG-078 FIX CONFIRMED: FOR loop with global now works!
FOR game.inning = 1 TO 9
    PRINT ""
    PRINT BOLD$; CYAN$; "════════ INNING "; game.inning; " ════════"; RESET$

    REM Top of inning
    game.battingHome = 0
    game.PlayHalfInning()

    REM Bottom of inning
    game.battingHome = 1
    game.PlayHalfInning()
NEXT

PRINT ""
PRINT BOLD$; CYAN$; "════════ FINAL SCORE ════════"; RESET$
game.ShowScoreboard()
PRINT ""

IF game.homeTeam.score > game.awayTeam.score THEN
    PRINT BOLD$; GREEN$; "🏆 "; game.homeTeam.teamName; " WIN! 🏆"; RESET$
ELSE IF game.awayTeam.score > game.homeTeam.score THEN
    PRINT BOLD$; RED$; "🏆 "; game.awayTeam.teamName; " WIN! 🏆"; RESET$
ELSE
    PRINT BOLD$; YELLOW$; "⚖ TIE GAME! ⚖"; RESET$
END IF
PRINT ""
