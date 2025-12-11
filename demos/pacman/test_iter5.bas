' ============================================================================
' PACMAN Test Iteration 5 - Full maze init and draw test
' ============================================================================

AddFile "maze.bas"
AddFile "player.bas"

Dim TheMaze As Maze
Dim ThePacman As Pacman

PRINT "=== Pacman Full Integration Test ==="
PRINT ""

' Test 1: Initialize constants
PRINT "Test 1: InitMazeConstants()"
InitMazeConstants()
PRINT "PASS"

PRINT "Test 2: InitPlayerConstants()"
InitPlayerConstants()
PRINT "PASS"

' Test 3: Create and init maze
PRINT "Test 3: Create Maze"
TheMaze = New Maze()
TheMaze.Init()
PRINT "Maze created: "
PRINT TheMaze.GetWidth()
PRINT "x"
PRINT TheMaze.GetHeight()
PRINT " Dots: "
PRINT TheMaze.GetDotsRemaining()

' Test 4: Check specific cells
PRINT "Test 4: Check cells"
PRINT "Cell(0,0) = "
PRINT TheMaze.GetCell(0, 0)
PRINT " (should be 0/wall)"

PRINT "Cell(1,1) = "
PRINT TheMaze.GetCell(1, 1)
PRINT " (should be 2/dot)"

PRINT "Cell(14,23) = "
PRINT TheMaze.GetCell(14, 23)
PRINT " (should be 1/empty or 2/dot)"

' Test 5: Create Pacman
PRINT "Test 5: Create Pacman"
ThePacman = New Pacman()
ThePacman.Init(14, 23)
PRINT "Pacman at: ("
PRINT ThePacman.GetX()
PRINT ","
PRINT ThePacman.GetY()
PRINT ")"

' Test 6: Test walkable
PRINT "Test 6: IsWalkable"
PRINT "IsWalkable(0,0) = "
PRINT TheMaze.IsWalkable(0, 0)
PRINT " (should be 0)"
PRINT "IsWalkable(1,1) = "
PRINT TheMaze.IsWalkable(1, 1)
PRINT " (should be 1)"

' Test 7: Draw maze (output will be long)
PRINT ""
PRINT "Test 7: Draw Maze"
TheMaze.Draw()
ThePacman.Draw()

PRINT ""
PRINT "=== All Integration Tests Complete ==="
