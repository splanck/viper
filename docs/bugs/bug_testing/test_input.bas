REM Test INPUT statement
DIM playerName AS STRING
DIM jerseyNum AS INTEGER
DIM battingAvg AS SINGLE

PRINT "=== PLAYER REGISTRATION ==="
PRINT ""

PRINT "Enter player name: ";
INPUT playerName

PRINT "Enter jersey number: ";
INPUT jerseyNum

PRINT "Enter batting average (e.g. 0.300): ";
INPUT battingAvg

PRINT ""
PRINT "=== PLAYER CARD ==="
PRINT "Name: "; playerName
PRINT "Jersey: #"; jerseyNum
PRINT "Avg: "; battingAvg
PRINT ""

IF battingAvg > 0.3 THEN
    PRINT "⭐ ALL-STAR PLAYER!"
ELSE IF battingAvg > 0.25 THEN
    PRINT "✓ Solid hitter"
ELSE
    PRINT "⚠ Needs improvement"
END IF
