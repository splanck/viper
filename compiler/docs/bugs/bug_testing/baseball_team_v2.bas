REM baseball_team_v2.bas - Team class WITHOUT arrays (BUG-075 workaround)
REM Using individual fields instead of array

ADDFILE "baseball_player.bas"

CLASS Team
    teamName AS STRING
    REM WORKAROUND BUG-075: Use individual fields instead of array
    player1 AS Player
    player2 AS Player
    player3 AS Player
    player4 AS Player
    player5 AS Player
    player6 AS Player
    player7 AS Player
    player8 AS Player
    player9 AS Player
    rosterSize AS INTEGER
    wins AS INTEGER
    losses AS INTEGER

    SUB Init(name AS STRING)
        ME.teamName = name
        ME.rosterSize = 0
        ME.wins = 0
        ME.losses = 0
    END SUB

    SUB AddPlayer(playerName AS STRING, pos AS STRING)
        IF ME.rosterSize >= 9 THEN
            PRINT "Roster full!"
            EXIT SUB
        END IF
        ME.rosterSize = ME.rosterSize + 1
        REM Create and initialize based on roster position
        SELECT CASE ME.rosterSize
            CASE 1
                ME.player1 = NEW Player()
                ME.player1.Init(playerName, pos)
            CASE 2
                ME.player2 = NEW Player()
                ME.player2.Init(playerName, pos)
            CASE 3
                ME.player3 = NEW Player()
                ME.player3.Init(playerName, pos)
            CASE 4
                ME.player4 = NEW Player()
                ME.player4.Init(playerName, pos)
            CASE 5
                ME.player5 = NEW Player()
                ME.player5.Init(playerName, pos)
            CASE 6
                ME.player6 = NEW Player()
                ME.player6.Init(playerName, pos)
            CASE 7
                ME.player7 = NEW Player()
                ME.player7.Init(playerName, pos)
            CASE 8
                ME.player8 = NEW Player()
                ME.player8.Init(playerName, pos)
            CASE 9
                ME.player9 = NEW Player()
                ME.player9.Init(playerName, pos)
        END SELECT
    END SUB

    FUNCTION GetPlayer(idx AS INTEGER) AS Player
        DIM result AS Player
        SELECT CASE idx
            CASE 1
                result = ME.player1
            CASE 2
                result = ME.player2
            CASE 3
                result = ME.player3
            CASE 4
                result = ME.player4
            CASE 5
                result = ME.player5
            CASE 6
                result = ME.player6
            CASE 7
                result = ME.player7
            CASE 8
                result = ME.player8
            CASE 9
                result = ME.player9
            CASE ELSE
                result = ME.player1  REM Default fallback
        END SELECT
        GetPlayer = result
    END FUNCTION

    SUB ShowLineup()
        PRINT "=== "; ME.teamName; " Lineup ==="
        DIM i AS INTEGER
        FOR i = 1 TO ME.rosterSize
            PRINT i; ". ";
            DIM p AS Player
            p = ME.GetPlayer(i)
            p.ShowStats()
            PRINT ""
        NEXT
        PRINT "Record: "; ME.wins; "-"; ME.losses
    END SUB

    SUB RecordWin()
        ME.wins = ME.wins + 1
    END SUB

    SUB RecordLoss()
        ME.losses = ME.losses + 1
    END SUB
END CLASS
