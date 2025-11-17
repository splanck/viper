REM baseball_stats.bas - Simple stats tracking without arrays
REM Testing: String arrays, complex calculations, file I/O

DIM playerNames(9) AS STRING
DIM playerHits(9) AS INTEGER
DIM playerAtBats(9) AS INTEGER
DIM numPlayers AS INTEGER

SUB InitStats()
    numPlayers = 0
END SUB

SUB AddPlayerStat(name AS STRING)
    IF numPlayers >= 9 THEN
        PRINT "Roster full!"
        EXIT SUB
    END IF
    numPlayers = numPlayers + 1
    playerNames(numPlayers) = name
    playerHits(numPlayers) = 0
    playerAtBats(numPlayers) = 0
END SUB

SUB RecordAtBat(playerNum AS INTEGER, wasHit AS INTEGER)
    IF playerNum < 1 OR playerNum > numPlayers THEN
        EXIT SUB
    END IF
    playerAtBats(playerNum) = playerAtBats(playerNum) + 1
    IF wasHit THEN
        playerHits(playerNum) = playerHits(playerNum) + 1
    END IF
END SUB

SUB ShowStats()
    PRINT ""
    PRINT "=== PLAYER STATISTICS ==="
    DIM i AS INTEGER
    FOR i = 1 TO numPlayers
        DIM avg AS INTEGER
        IF playerAtBats(i) > 0 THEN
            avg = (playerHits(i) * 1000) \ playerAtBats(i)
        ELSE
            avg = 0
        END IF
        PRINT i; ". "; playerNames(i); " - AB: "; playerAtBats(i); " H: "; playerHits(i); " AVG: .";
        IF avg < 100 THEN PRINT "0";
        IF avg < 10 THEN PRINT "0";
        PRINT avg
    NEXT
    PRINT ""
END SUB

REM Initialize and test
InitStats()

PRINT "Testing stats tracking..."
PRINT ""

AddPlayerStat("Mike Trout")
AddPlayerStat("Shohei Ohtani")
AddPlayerStat("Aaron Judge")

PRINT "Initial stats:"
ShowStats()

PRINT "Recording at-bats..."
RecordAtBat(1, 1)  REM Trout - hit
RecordAtBat(1, 0)  REM Trout - out
RecordAtBat(1, 1)  REM Trout - hit
RecordAtBat(2, 1)  REM Ohtani - hit
RecordAtBat(2, 1)  REM Ohtani - hit
RecordAtBat(2, 1)  REM Ohtani - hit
RecordAtBat(3, 0)  REM Judge - out

PRINT "Updated stats:"
ShowStats()

PRINT "Test complete!"
