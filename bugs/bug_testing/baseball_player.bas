REM baseball_player.bas - Player class with stats
REM Testing: OOP, string fields, integer fields, methods

CLASS Player
    name AS STRING
    position AS STRING
    battingAvg AS INTEGER  REM stored as thousandths (e.g., 300 = .300)
    hits AS INTEGER
    atBats AS INTEGER
    homeRuns AS INTEGER
    rbis AS INTEGER

    SUB Init(playerName AS STRING, pos AS STRING)
        ME.name = playerName
        ME.position = pos
        ME.battingAvg = 250  REM default .250 average
        ME.hits = 0
        ME.atBats = 0
        ME.homeRuns = 0
        ME.rbis = 0
    END SUB

    SUB RecordAtBat(isHit AS INTEGER, isHomeRun AS INTEGER, runsScored AS INTEGER)
        ME.atBats = ME.atBats + 1
        IF isHit THEN
            ME.hits = ME.hits + 1
        END IF
        IF isHomeRun THEN
            ME.homeRuns = ME.homeRuns + 1
        END IF
        ME.rbis = ME.rbis + runsScored
        REM Update batting average (hits / atBats * 1000)
        IF ME.atBats > 0 THEN
            ME.battingAvg = (ME.hits * 1000) \ ME.atBats
        END IF
    END SUB

    SUB ShowStats()
        PRINT ME.name; " ("; ME.position; ")"
        PRINT "  AVG: .";
        IF ME.battingAvg < 100 THEN PRINT "0";
        IF ME.battingAvg < 10 THEN PRINT "0";
        PRINT ME.battingAvg
        PRINT "  AB: "; ME.atBats; " H: "; ME.hits; " HR: "; ME.homeRuns; " RBI: "; ME.rbis
    END SUB
END CLASS
