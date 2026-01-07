REM Test 2: Team class with lineup
ADDFILE "baseball_team.bas"

DIM yankees AS Team
yankees = NEW Team()
yankees.Init("New York Yankees")

PRINT "=== Building Lineup ==="
yankees.AddPlayer("Aaron Judge", "RF")
yankees.AddPlayer("Juan Soto", "LF")
yankees.AddPlayer("Giancarlo Stanton", "DH")
yankees.AddPlayer("Anthony Volpe", "SS")
yankees.AddPlayer("Gleyber Torres", "2B")
yankees.AddPlayer("Austin Wells", "C")
yankees.AddPlayer("Jazz Chisholm", "3B")
yankees.AddPlayer("Anthony Rizzo", "1B")
yankees.AddPlayer("Trent Grisham", "CF")

PRINT ""
yankees.ShowLineup()

PRINT ""
PRINT "=== Simulating some at-bats ==="
REM Judge hits a homer
yankees.lineup(1).RecordAtBat(1, 1, 1)
REM Soto gets a hit
yankees.lineup(2).RecordAtBat(1, 0, 0)
REM Stanton strikes out
yankees.lineup(3).RecordAtBat(0, 0, 0)

PRINT ""
yankees.ShowLineup()

yankees.RecordWin()
PRINT ""
PRINT "After win - Record: "; yankees.wins; "-"; yankees.losses

PRINT ""
PRINT "Test complete!"
