REM Test file I/O - saving and loading game data
DIM score AS INTEGER
DIM playerName AS STRING

PRINT "Testing file I/O..."
PRINT ""

REM Set some data
score = 42
playerName = "Mike Trout"

PRINT "Writing to file..."
OPEN "baseball_save.txt" FOR OUTPUT AS #1
PRINT #1, "SAVE_DATA"
PRINT #1, playerName
PRINT #1, score
CLOSE #1
PRINT "File written!"

PRINT ""
PRINT "Reading from file..."
DIM loadedName AS STRING
DIM loadedScore AS INTEGER
DIM header AS STRING

OPEN "baseball_save.txt" FOR INPUT AS #1
LINE INPUT #1, header
LINE INPUT #1, loadedName
INPUT #1, loadedScore
CLOSE #1

PRINT "File read!"
PRINT ""
PRINT "Loaded data:"
PRINT "Header: "; header
PRINT "Name: "; loadedName
PRINT "Score: "; loadedScore

IF loadedName = playerName AND loadedScore = score THEN
    PRINT ""
    PRINT "✓ File I/O test PASSED!"
ELSE
    PRINT ""
    PRINT "✗ File I/O test FAILED!"
END IF
