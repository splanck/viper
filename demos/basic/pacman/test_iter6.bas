' ============================================================================
' PACMAN Test Iteration 6 - Ghost Module Test
' ============================================================================

AddFile "maze.bas"
AddFile "player.bas"
AddFile "ghost.bas"

Dim TheMaze As Maze
Dim ghost1 As Ghost

PRINT "=== Ghost Module Test ==="
PRINT ""

' Initialize constants
InitMazeConstants()
InitPlayerConstants()
InitGhostConstants()

' Create maze
TheMaze = New Maze()
TheMaze.Init()

' Test 1: Create ghost
PRINT "Test 1: Create Ghost (Blinky)"
ghost1 = New Ghost()
ghost1.Init(0, 14, 14, COLOR_BLINKY)
PRINT "Ghost at: ("
PRINT ghost1.GetX()
PRINT ","
PRINT ghost1.GetY()
PRINT ")"
PRINT "PASS"

' Test 2: Check ghost is in pen
PRINT "Test 2: Ghost in pen = "
PRINT ghost1.IsInPen()
If ghost1.IsInPen() = 1 Then
    PRINT "PASS"
Else
    PRINT "FAIL"
End If

' Test 3: Ghost mode
PRINT "Test 3: Ghost mode = "
PRINT ghost1.GetMode()
PRINT " (should be 1/SCATTER)"

' Test 4: Set frightened
PRINT "Test 4: Set frightened"
ghost1.SetFrightened(100)
PRINT "Mode after frightened: "
PRINT ghost1.GetMode()
PRINT " (should be 2/FRIGHTENED)"

' Test 5: Ghost movement (needs Randomize for frightened mode)
PRINT "Test 5: Ghost move"
Randomize
Dim moved As Integer
moved = ghost1.Move(TheMaze, 14, 23)
PRINT "Moved: "
PRINT moved

PRINT ""
PRINT "=== Ghost Tests Complete ==="
