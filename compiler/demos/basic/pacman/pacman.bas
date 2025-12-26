' ============================================================================
' PACMAN - Classic Arcade Game for Viper BASIC
' ============================================================================
' A comprehensive Pacman clone to stress test Viper BASIC and runtime
' Features: OOP classes, terminal graphics, game AI, scoring system
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
Dim GameRunning As Integer
Dim GamePaused As Integer
Dim FrameCount As Integer
Dim Level As Integer
Dim GhostPointsMultiplier As Integer
Dim ModeTimer As Integer
Dim ChaseMode As Integer

' ============================================================================
' Initialize Game Constants
' ============================================================================
Sub InitGameConstants()
    POWER_PELLET_DURATION = 100  ' Frames of frightened mode
    GHOST_POINTS_BASE = 200      ' Base points for eating ghost
End Sub

' ============================================================================
' Initialize Game
' ============================================================================
Sub InitGame()
    Dim i As Integer

    ' Initialize all constants
    InitMazeConstants()
    InitPlayerConstants()
    InitGhostConstants()
    InitGameConstants()

    ' Create maze
    TheMaze = New Maze()
    TheMaze.Init()

    ' Create Pacman (starting position: center bottom area)
    ThePacman = New Pacman()
    ThePacman.Init(14, 23)

    ' Create 4 ghosts in the pen
    ' Blinky (red) - starts at pen exit
    Ghosts(0) = New Ghost()
    Ghosts(0).Init(0, 14, 11, COLOR_BLINKY)

    ' Pinky (pink) - center of pen
    Ghosts(1) = New Ghost()
    Ghosts(1).Init(1, 14, 14, COLOR_PINKY)

    ' Inky (cyan) - left side of pen
    Ghosts(2) = New Ghost()
    Ghosts(2).Init(2, 12, 14, COLOR_INKY)

    ' Clyde (orange) - right side of pen
    Ghosts(3) = New Ghost()
    Ghosts(3).Init(3, 16, 14, COLOR_CLYDE)

    GameRunning = 1
    GamePaused = 0
    FrameCount = 0
    Level = 1
    GhostPointsMultiplier = 1
    ModeTimer = 0
    ChaseMode = 0

    Randomize
End Sub

' ============================================================================
' Draw HUD (score, lives, level, etc.)
' ============================================================================
Sub DrawHUD()
    ' Score
    Viper.Terminal.SetPosition(1, MAZE_LEFT)
    Viper.Terminal.SetColor(15, 0)
    PRINT "SCORE: ";
    Viper.Terminal.SetColor(14, 0)
    PRINT ThePacman.GetScore();
    PRINT "     ";

    ' Lives
    Viper.Terminal.SetPosition(1, MAZE_LEFT + 18)
    Viper.Terminal.SetColor(15, 0)
    PRINT "LIVES: ";
    Viper.Terminal.SetColor(14, 0)
    PRINT ThePacman.GetLives();

    ' Level
    Viper.Terminal.SetPosition(1, MAZE_LEFT + 28)
    Viper.Terminal.SetColor(15, 0)
    PRINT "LVL: ";
    Viper.Terminal.SetColor(11, 0)
    PRINT Level;

    ' Instructions
    Viper.Terminal.SetPosition(MAZE_TOP + 32, MAZE_LEFT)
    Viper.Terminal.SetColor(8, 0)
    PRINT "WASD=Move  P=Pause  Q=Quit";

    ' Dots remaining
    Viper.Terminal.SetPosition(MAZE_TOP + 33, MAZE_LEFT)
    PRINT "Dots: ";
    PRINT TheMaze.GetDotsRemaining();
    PRINT "  ";

    Viper.Terminal.SetColor(7, 0)
End Sub

' ============================================================================
' Handle Input
' ============================================================================
Sub HandleInput()
    Dim key As String
    key = Viper.Terminal.InKey()

    If Len(key) > 0 Then
        If key = "w" Or key = "W" Then
            ThePacman.SetDirection(DIR_UP)
        ElseIf key = "s" Or key = "S" Then
            ThePacman.SetDirection(DIR_DOWN)
        ElseIf key = "a" Or key = "A" Then
            ThePacman.SetDirection(DIR_LEFT)
        ElseIf key = "d" Or key = "D" Then
            ThePacman.SetDirection(DIR_RIGHT)
        ElseIf key = "p" Or key = "P" Then
            GamePaused = 1 - GamePaused
            If GamePaused = 1 Then
                Viper.Terminal.SetPosition(MAZE_TOP + 15, MAZE_LEFT + 8)
                Viper.Terminal.SetColor(14, 0)
                PRINT "*** PAUSED ***";
            End If
        ElseIf key = "q" Or key = "Q" Then
            GameRunning = 0
        End If
    End If
End Sub

' ============================================================================
' Update Ghosts
' ============================================================================
Sub UpdateGhosts()
    Dim i As Integer
    Dim targetX As Integer
    Dim targetY As Integer
    Dim px As Integer
    Dim py As Integer

    px = ThePacman.GetX()
    py = ThePacman.GetY()

    For i = 0 To 3
        ' Clear old position
        Ghosts(i).Clear(TheMaze)

        ' Calculate target based on ghost personality
        If Ghosts(i).GetMode() = MODE_CHASE Then
            If i = 0 Then
                ' Blinky targets Pacman directly
                targetX = px
                targetY = py
            ElseIf i = 1 Then
                ' Pinky targets 4 tiles ahead of Pacman
                targetX = px
                targetY = py - 4
            ElseIf i = 2 Then
                ' Inky uses complex targeting (simplified)
                targetX = px + 2
                targetY = py + 2
            Else
                ' Clyde runs away when close
                If Abs(Ghosts(i).GetX() - px) + Abs(Ghosts(i).GetY() - py) < 8 Then
                    targetX = 2
                    targetY = 30
                Else
                    targetX = px
                    targetY = py
                End If
            End If
        Else
            targetX = px
            targetY = py
        End If

        ' Move ghost
        Ghosts(i).Move(TheMaze, targetX, targetY)
    Next i
End Sub

' ============================================================================
' Check Ghost Collisions
' ============================================================================
Function CheckGhostCollisions() As Integer
    Dim i As Integer
    Dim px As Integer
    Dim py As Integer
    Dim result As Integer

    result = 0
    px = ThePacman.GetX()
    py = ThePacman.GetY()

    For i = 0 To 3
        If Ghosts(i).CheckCollision(px, py) = 1 Then
            If Ghosts(i).GetMode() = MODE_FRIGHTENED Then
                ' Eat the ghost
                Ghosts(i).Eaten()
                ThePacman.AddScore(GHOST_POINTS_BASE * GhostPointsMultiplier)
                GhostPointsMultiplier = GhostPointsMultiplier * 2

                ' Show points briefly
                Viper.Terminal.SetPosition(MAZE_TOP + py, MAZE_LEFT + px)
                Viper.Terminal.SetColor(11, 0)
                PRINT "*";
            ElseIf Ghosts(i).GetMode() <> MODE_EATEN Then
                ' Pacman dies
                result = 1
            End If
        End If
    Next i

    CheckGhostCollisions = result
End Function

' ============================================================================
' Activate Power Pellet
' ============================================================================
Sub ActivatePowerPellet()
    Dim i As Integer

    GhostPointsMultiplier = 1

    For i = 0 To 3
        Ghosts(i).SetFrightened(POWER_PELLET_DURATION)
    Next i
End Sub

' ============================================================================
' Reset After Death
' ============================================================================
Sub ResetAfterDeath()
    Dim i As Integer

    ThePacman.Reset()

    For i = 0 To 3
        Ghosts(i).Reset()
    Next i

    ' Brief pause
    Viper.Time.SleepMs(1000)
End Sub

' ============================================================================
' Update Game State
' ============================================================================
Sub UpdateGame()
    Dim oldX As Integer
    Dim oldY As Integer
    Dim points As Integer
    Dim cell As Integer

    oldX = ThePacman.GetX()
    oldY = ThePacman.GetY()

    ' Move Pacman
    If ThePacman.Move(TheMaze) = 1 Then
        ' Clear old position
        TheMaze.DrawCell(oldX, oldY)

        ' Check what's at new position
        cell = TheMaze.GetCell(ThePacman.GetX(), ThePacman.GetY())

        ' Check for dot eating
        points = TheMaze.EatDot(ThePacman.GetX(), ThePacman.GetY())
        If points > 0 Then
            ThePacman.AddScore(points)

            ' Check if it was a power pellet
            If points = 50 Then
                ActivatePowerPellet()
            End If
        End If
    End If

    ' Update ghosts
    UpdateGhosts()

    ' Check ghost collisions
    If CheckGhostCollisions() = 1 Then
        ThePacman.Die()
        If ThePacman.IsAlive() = 1 Then
            ResetAfterDeath()
        End If
    End If

    ' Update mode timer (alternate between scatter and chase)
    ModeTimer = ModeTimer + 1
    If ModeTimer >= 200 Then
        ModeTimer = 0
        ChaseMode = 1 - ChaseMode
    End If

    ' Check win condition
    If TheMaze.GetDotsRemaining() = 0 Then
        ' Level complete!
        Level = Level + 1
        TheMaze.Init()  ' Reset maze with dots

        ' Reset positions
        ThePacman.Reset()
        Dim i As Integer
        For i = 0 To 3
            Ghosts(i).Reset()
        Next i

        ' Show level message
        Viper.Terminal.SetPosition(MAZE_TOP + 15, MAZE_LEFT + 6)
        Viper.Terminal.SetColor(10, 0)
        PRINT "*** LEVEL ";
        PRINT Level;
        PRINT " ***";
        Viper.Time.SleepMs(2000)

        ' Redraw maze
        TheMaze.Draw()
    End If
End Sub

' ============================================================================
' Draw Game
' ============================================================================
Sub DrawGame()
    Dim i As Integer

    ' Draw Pacman
    ThePacman.Draw()

    ' Draw ghosts
    For i = 0 To 3
        Ghosts(i).Draw()
    Next i

    ' Update HUD
    DrawHUD()
End Sub

' ============================================================================
' Main Game Loop
' ============================================================================
Sub GameLoop()
    ' Clear screen
    Viper.Terminal.Clear()

    ' Draw initial maze
    TheMaze.Draw()
    DrawGame()

    Do While GameRunning = 1 And ThePacman.IsAlive() = 1
        HandleInput()

        If GamePaused = 0 Then
            UpdateGame()
            DrawGame()
        End If

        FrameCount = FrameCount + 1
        Viper.Time.SleepMs(80)
    Loop

    ' Game over screen
    Viper.Terminal.SetPosition(MAZE_TOP + 14, MAZE_LEFT + 6)
    Viper.Terminal.SetColor(12, 0)

    If TheMaze.GetDotsRemaining() = 0 Then
        PRINT "*** CONGRATULATIONS! ***"
    ElseIf ThePacman.IsAlive() = 0 Then
        PRINT "*** GAME OVER ***"
    Else
        PRINT "*** QUIT ***"
    End If

    Viper.Terminal.SetPosition(MAZE_TOP + 16, MAZE_LEFT + 6)
    Viper.Terminal.SetColor(15, 0)
    PRINT "Final Score: ";
    Viper.Terminal.SetColor(14, 0)
    PRINT ThePacman.GetScore()

    Viper.Terminal.SetPosition(MAZE_TOP + 17, MAZE_LEFT + 6)
    Viper.Terminal.SetColor(15, 0)
    PRINT "Level Reached: ";
    Viper.Terminal.SetColor(11, 0)
    PRINT Level

    Viper.Terminal.SetPosition(MAZE_TOP + 19, MAZE_LEFT + 4)
    Viper.Terminal.SetColor(8, 0)
    PRINT "Press any key to exit..."
    Viper.Terminal.SetColor(7, 0)

    ' Wait for key
    Dim exitKey As String
    exitKey = Viper.Terminal.GetKey()
End Sub

' ============================================================================
' Show Title Screen
' ============================================================================
Sub ShowTitleScreen()
    Viper.Terminal.Clear()

    Viper.Terminal.SetPosition(5, 15)
    Viper.Terminal.SetColor(14, 0)
    PRINT "##############################"

    Viper.Terminal.SetPosition(6, 15)
    PRINT "#                            #"

    Viper.Terminal.SetPosition(7, 15)
    PRINT "#   ";
    Viper.Terminal.SetColor(14, 0)
    PRINT "P A C M A N";
    Viper.Terminal.SetColor(14, 0)
    PRINT "            #"

    Viper.Terminal.SetPosition(8, 15)
    PRINT "#                            #"

    Viper.Terminal.SetPosition(9, 15)
    PRINT "##############################"

    Viper.Terminal.SetPosition(12, 18)
    Viper.Terminal.SetColor(15, 0)
    PRINT "Viper BASIC Demo 2024"

    Viper.Terminal.SetPosition(15, 20)
    Viper.Terminal.SetColor(14, 0)
    PRINT "C ";
    Viper.Terminal.SetColor(7, 0)
    PRINT "- Pacman"

    Viper.Terminal.SetPosition(16, 20)
    Viper.Terminal.SetColor(9, 0)
    PRINT "M ";
    Viper.Terminal.SetColor(7, 0)
    PRINT "- Ghost (Blinky)"

    Viper.Terminal.SetPosition(17, 20)
    Viper.Terminal.SetColor(15, 0)
    PRINT ". ";
    Viper.Terminal.SetColor(7, 0)
    PRINT "- Dot (10 pts)"

    Viper.Terminal.SetPosition(18, 20)
    Viper.Terminal.SetColor(14, 0)
    PRINT "o ";
    Viper.Terminal.SetColor(7, 0)
    PRINT "- Power Pellet (50 pts)"

    Viper.Terminal.SetPosition(21, 15)
    Viper.Terminal.SetColor(10, 0)
    PRINT "Press any key to start..."

    Viper.Terminal.SetColor(7, 0)

    Dim startKey As String
    startKey = Viper.Terminal.GetKey()
End Sub

' ============================================================================
' Main Entry Point
' ============================================================================
ShowTitleScreen()
InitGame()
GameLoop()

Viper.Terminal.Clear()
Viper.Terminal.SetPosition(1, 1)
Viper.Terminal.SetColor(7, 0)
PRINT "Thanks for playing PACMAN!"
PRINT ""
PRINT "Frames played: ";
PRINT FrameCount
PRINT "Final score: ";
PRINT ThePacman.GetScore()
