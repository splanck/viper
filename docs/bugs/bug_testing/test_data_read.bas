REM Test DATA/READ statements
DIM playerName AS STRING
DIM battingAvg AS INTEGER
DIM i AS INTEGER

PRINT "=== BASEBALL ROSTER ==="
PRINT ""

FOR i = 1 TO 5
    READ playerName, battingAvg
    PRINT i; ". "; playerName; " - .";
    IF battingAvg < 100 THEN PRINT "0";
    IF battingAvg < 10 THEN PRINT "0";
    PRINT battingAvg
NEXT

PRINT ""
PRINT "Roster loaded!"

DATA "Mike Trout", 305
DATA "Shohei Ohtani", 304
DATA "Aaron Judge", 311
DATA "Mookie Betts", 292
DATA "Freddie Freeman", 325
