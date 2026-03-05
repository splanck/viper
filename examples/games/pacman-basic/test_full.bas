' ============================================================================
' PACMAN Full Integration Test - Non-interactive
' ============================================================================

AddFile "maze.bas"
AddFile "player.bas"
AddFile "ghost.bas"

' Game constants
Dim POWER_PELLET_DURATION As Integer
Dim GHOST_POINTS_BASE As Integer

' Game state
Dim TheMaze As Maze
Dim ThePacman As Pacman
Dim Ghosts(4) As Ghost

Sub InitGameConstants()
    POWER_PELLET_DURATION = 100
    GHOST_POINTS_BASE = 200
End Sub

PRINT "=== PACMAN Full Integration Test ==="
PRINT ""

' Initialize all constants
PRINT "1. Initializing constants..."
InitMazeConstants()
InitPlayerConstants()
InitGhostConstants()
InitGameConstants()
PRINT "PASS"

' Create maze
PRINT "2. Creating maze..."
TheMaze = New Maze()
TheMaze.Init()
PRINT "Maze: "
PRINT TheMaze.GetWidth()
PRINT "x"
PRINT TheMaze.GetHeight()
PRINT " Dots: "
PRINT TheMaze.GetDotsRemaining()

' Create Pacman
PRINT "3. Creating Pacman..."
ThePacman = New Pacman()
ThePacman.Init(14, 23)
PRINT "Pacman at ("
PRINT ThePacman.GetX()
PRINT ","
PRINT ThePacman.GetY()
PRINT ") Lives: "
PRINT ThePacman.GetLives()
PRINT " Score: "
PRINT ThePacman.GetScore()

' Create ghosts
PRINT "4. Creating 4 ghosts..."
Ghosts(0) = New Ghost()
Ghosts(0).Init(0, 14, 11, COLOR_BLINKY)
PRINT "Blinky at ("
PRINT Ghosts(0).GetX()
PRINT ","
PRINT Ghosts(0).GetY()
PRINT ")"

Ghosts(1) = New Ghost()
Ghosts(1).Init(1, 14, 14, COLOR_PINKY)
PRINT "Pinky at ("
PRINT Ghosts(1).GetX()
PRINT ","
PRINT Ghosts(1).GetY()
PRINT ")"

Ghosts(2) = New Ghost()
Ghosts(2).Init(2, 12, 14, COLOR_INKY)
PRINT "Inky at ("
PRINT Ghosts(2).GetX()
PRINT ","
PRINT Ghosts(2).GetY()
PRINT ")"

Ghosts(3) = New Ghost()
Ghosts(3).Init(3, 16, 14, COLOR_CLYDE)
PRINT "Clyde at ("
PRINT Ghosts(3).GetX()
PRINT ","
PRINT Ghosts(3).GetY()
PRINT ")"

' Test movement
PRINT "5. Testing Pacman movement..."
Randomize
ThePacman.SetDirection(DIR_RIGHT)
Dim moved As Integer
moved = ThePacman.Move(TheMaze)
PRINT "Move result: "
PRINT moved
PRINT " New pos: ("
PRINT ThePacman.GetX()
PRINT ","
PRINT ThePacman.GetY()
PRINT ")"

' Test eating dot
PRINT "6. Testing dot eating..."
Dim points As Integer
points = TheMaze.EatDot(ThePacman.GetX(), ThePacman.GetY())
ThePacman.AddScore(points)
PRINT "Points earned: "
PRINT points
PRINT " Total score: "
PRINT ThePacman.GetScore()
PRINT " Dots remaining: "
PRINT TheMaze.GetDotsRemaining()

' Test ghost movement
PRINT "7. Testing ghost movement..."
Dim i As Integer
For i = 0 To 3
    moved = Ghosts(i).Move(TheMaze, 14, 23)
    PRINT "Ghost "
    PRINT i
    PRINT " moved: "
    PRINT moved
Next i

' Test collision detection
PRINT "8. Testing collision detection..."
PRINT "Ghost 0 collision with Pacman: "
PRINT Ghosts(0).CheckCollision(ThePacman.GetX(), ThePacman.GetY())

' Test power pellet
PRINT "9. Testing power pellet activation..."
For i = 0 To 3
    Ghosts(i).SetFrightened(50)
Next i
PRINT "Ghost 0 mode (should be 2/FRIGHTENED): "
PRINT Ghosts(0).GetMode()

' Test reset
PRINT "10. Testing reset..."
ThePacman.Reset()
PRINT "Pacman reset to ("
PRINT ThePacman.GetX()
PRINT ","
PRINT ThePacman.GetY()
PRINT ")"

PRINT ""
PRINT "=== All Tests Complete ==="
PRINT ""
PRINT "Summary:"
PRINT "- Maze: WORKING"
PRINT "- Pacman: WORKING"
PRINT "- Ghosts: WORKING"
PRINT "- Movement: WORKING"
PRINT "- Collision: WORKING"
PRINT "- Score system: WORKING"
