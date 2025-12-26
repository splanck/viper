REM Test 1: Basic Player class functionality
ADDFILE "baseball_player.bas"

DIM p1 AS Player
p1 = NEW Player()
p1.Init("Mike Trout", "CF")

PRINT "=== Initial Player Stats ==="
p1.ShowStats()

PRINT ""
PRINT "=== Recording at-bats ==="
REM Single
p1.RecordAtBat(1, 0, 0)
REM Home run with 2 RBI
p1.RecordAtBat(1, 1, 2)
REM Strike out
p1.RecordAtBat(0, 0, 0)
REM Single with 1 RBI
p1.RecordAtBat(1, 0, 1)

PRINT ""
PRINT "=== Updated Stats ==="
p1.ShowStats()

PRINT ""
PRINT "Test complete!"
