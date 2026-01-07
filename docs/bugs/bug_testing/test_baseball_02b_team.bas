REM Test 2b: Team class without arrays (BUG-075 workaround)
ADDFILE "baseball_team_v2.bas"

DIM yankees AS Team
yankees = NEW Team()
yankees.Init("New York Yankees")

PRINT "=== Building Lineup ==="
yankees.AddPlayer("Aaron Judge", "RF")
yankees.AddPlayer("Juan Soto", "LF")
yankees.AddPlayer("Giancarlo Stanton", "DH")

PRINT ""
yankees.ShowLineup()

PRINT ""
PRINT "=== Recording at-bats ==="
DIM p AS Player
p = yankees.GetPlayer(1)
p.RecordAtBat(1, 1, 1)

PRINT ""
yankees.ShowLineup()

PRINT ""
PRINT "Test complete!"
