REM baseball_team.bas - Team class with lineup management
REM Testing: Arrays of objects, loops, team management

ADDFILE "baseball_player.bas"

CLASS Team
    teamName AS STRING
    lineup(9) AS Player  REM 9 players in lineup
    rosterSize AS INTEGER
    wins AS INTEGER
    losses AS INTEGER

    SUB Init(name AS STRING)
        ME.teamName = name
        ME.rosterSize = 0
        ME.wins = 0
        ME.losses = 0
        REM WORKAROUND BUG-075: Don't pre-initialize - assign when adding players
    END SUB

    SUB AddPlayer(playerName AS STRING, pos AS STRING)
        IF ME.rosterSize >= 9 THEN
            PRINT "Roster full!"
            EXIT SUB
        END IF
        ME.rosterSize = ME.rosterSize + 1
        REM WORKAROUND BUG-075: Create new player and then initialize
        DIM p AS Player
        p = NEW Player()
        p.Init(playerName, pos)
        ME.lineup(ME.rosterSize) = p
    END SUB

    SUB ShowLineup()
        PRINT "=== "; ME.teamName; " Lineup ==="
        DIM i AS INTEGER
        FOR i = 1 TO ME.rosterSize
            PRINT i; ". ";
            ME.lineup(i).ShowStats()
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
