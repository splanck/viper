' ============================================================================
' BUGS PACMAN - An ANSI Pac-Man Clone for Viper BASIC
' ============================================================================
' A comprehensive Pac-Man clone featuring:
' - ANSI graphics for terminal display
' - OOP design with classes for game entities
' - Multiple ghost AI behaviors
' - Power pellets and ghost vulnerability
' - Multiple levels with increasing difficulty
' - High score tracking
' ============================================================================

' ============================================================================
' CONSTANTS AND CONFIGURATION
' ============================================================================

' Maze dimensions (playable area)
Dim MAZE_WIDTH As Integer
Dim MAZE_HEIGHT As Integer
MAZE_WIDTH = 28
MAZE_HEIGHT = 21

' Game timing (milliseconds)
Dim FRAME_DELAY As Integer
FRAME_DELAY = 80

' Direction constants
Dim DIR_NONE As Integer
Dim DIR_UP As Integer
Dim DIR_DOWN As Integer
Dim DIR_LEFT As Integer
Dim DIR_RIGHT As Integer
DIR_NONE = 0
DIR_UP = 1
DIR_DOWN = 2
DIR_LEFT = 3
DIR_RIGHT = 4

' Tile types
Dim TILE_EMPTY As Integer
Dim TILE_WALL As Integer
Dim TILE_PELLET As Integer
Dim TILE_POWER As Integer
Dim TILE_DOOR As Integer
TILE_EMPTY = 0
TILE_WALL = 1
TILE_PELLET = 2
TILE_POWER = 3
TILE_DOOR = 4

' Colors
Dim COL_WALL As Integer
Dim COL_PELLET As Integer
Dim COL_POWER As Integer
Dim COL_PACMAN As Integer
Dim COL_BLINKY As Integer
Dim COL_PINKY As Integer
Dim COL_INKY As Integer
Dim COL_CLYDE As Integer
Dim COL_VULN As Integer
COL_WALL = 4
COL_PELLET = 7
COL_POWER = 15
COL_PACMAN = 14
COL_BLINKY = 1
COL_PINKY = 13
COL_INKY = 11
COL_CLYDE = 6
COL_VULN = 4

' ============================================================================
' GLOBAL GAME STATE
' ============================================================================

Dim GameScore As Integer
Dim GameHighScore As Integer
Dim GameLevel As Integer
Dim GameLives As Integer
Dim PelletsRemaining As Integer
Dim PowerTimer As Integer
Dim GhostsEatenCount As Integer
Dim GameRunning As Integer
Dim FrameCounter As Integer

' ============================================================================
' MAZE DATA
' ============================================================================

Dim Maze(30, 25) As Integer

' ============================================================================
' PLAYER CLASS
' ============================================================================

Class Player
    Dim X As Integer
    Dim Y As Integer
    Dim Dir As Integer
    Dim NextDir As Integer
    Dim Alive As Integer
    Dim AnimFrame As Integer

    Sub New()
        Me.X = 14
        Me.Y = 17
        Me.Dir = DIR_NONE
        Me.NextDir = DIR_NONE
        Me.Alive = 1
        Me.AnimFrame = 0
    End Sub

    Sub Reset()
        Me.X = 14
        Me.Y = 17
        Me.Dir = DIR_NONE
        Me.NextDir = DIR_NONE
        Me.Alive = 1
        Me.AnimFrame = 0
    End Sub

    Function GetChar() As String
        Me.AnimFrame = Me.AnimFrame + 1
        If Me.AnimFrame > 3 Then Me.AnimFrame = 0

        If Me.Dir = DIR_RIGHT Then
            If Me.AnimFrame < 2 Then Return ">"
            Return "C"
        End If
        If Me.Dir = DIR_LEFT Then
            If Me.AnimFrame < 2 Then Return "<"
            Return "C"
        End If
        If Me.Dir = DIR_UP Then
            If Me.AnimFrame < 2 Then Return "v"
            Return "O"
        End If
        If Me.Dir = DIR_DOWN Then
            If Me.AnimFrame < 2 Then Return "^"
            Return "O"
        End If
        Return "O"
    End Function
End Class

' ============================================================================
' GHOST CLASS
' ============================================================================

Class Ghost
    Dim X As Integer
    Dim Y As Integer
    Dim StartX As Integer
    Dim StartY As Integer
    Dim Dir As Integer
    Dim GhostColor As Integer
    Dim GhostType As Integer
    Dim Vulnerable As Integer
    Dim Eaten As Integer
    Dim InHouse As Integer
    Dim ReleaseDelay As Integer
    Dim AnimFrame As Integer
    Dim TargetX As Integer
    Dim TargetY As Integer

    Sub New()
        Me.X = 14
        Me.Y = 11
        Me.StartX = 14
        Me.StartY = 11
        Me.Dir = DIR_UP
        Me.GhostColor = 1
        Me.GhostType = 0
        Me.Vulnerable = 0
        Me.Eaten = 0
        Me.InHouse = 0
        Me.ReleaseDelay = 0
        Me.AnimFrame = 0
        Me.TargetX = 14
        Me.TargetY = 0
    End Sub

    Sub Setup(gtype As Integer)
        Me.GhostType = gtype

        If gtype = 0 Then
            Me.GhostColor = COL_BLINKY
            Me.StartX = 14
            Me.StartY = 11
            Me.InHouse = 0
            Me.ReleaseDelay = 0
        End If

        If gtype = 1 Then
            Me.GhostColor = COL_PINKY
            Me.StartX = 14
            Me.StartY = 14
            Me.InHouse = 1
            Me.ReleaseDelay = 30
        End If

        If gtype = 2 Then
            Me.GhostColor = COL_INKY
            Me.StartX = 12
            Me.StartY = 14
            Me.InHouse = 1
            Me.ReleaseDelay = 60
        End If

        If gtype = 3 Then
            Me.GhostColor = COL_CLYDE
            Me.StartX = 16
            Me.StartY = 14
            Me.InHouse = 1
            Me.ReleaseDelay = 90
        End If

        Me.X = Me.StartX
        Me.Y = Me.StartY
    End Sub

    Sub ResetGhost()
        Me.X = Me.StartX
        Me.Y = Me.StartY
        Me.Dir = DIR_UP
        Me.Vulnerable = 0
        Me.Eaten = 0
        Me.AnimFrame = 0
        If Me.GhostType = 0 Then
            Me.InHouse = 0
        Else
            Me.InHouse = 1
        End If
    End Sub

    Function GetChar() As String
        Me.AnimFrame = Me.AnimFrame + 1
        If Me.AnimFrame > 7 Then Me.AnimFrame = 0
        If Me.Eaten = 1 Then Return Chr$(34)
        If Me.AnimFrame < 4 Then Return "M"
        Return "W"
    End Function

    Function GetColor() As Integer
        If Me.Eaten = 1 Then Return 7
        If Me.Vulnerable = 1 Then Return COL_VULN
        Return Me.GhostColor
    End Function
End Class

' ============================================================================
' GAME OBJECTS
' ============================================================================

Dim Pac As Player
Dim Ghosts(4) As Ghost

' ============================================================================
' MAZE INITIALIZATION
' ============================================================================

Sub InitMaze()
    Dim x As Integer
    Dim y As Integer

    ' Clear maze
    For y = 0 To MAZE_HEIGHT - 1
        For x = 0 To MAZE_WIDTH - 1
            Maze(x, y) = TILE_EMPTY
        Next x
    Next y

    ' Top and bottom walls
    For x = 0 To MAZE_WIDTH - 1
        Maze(x, 0) = TILE_WALL
        Maze(x, MAZE_HEIGHT - 1) = TILE_WALL
    Next x

    ' Left and right walls (with tunnel gaps at row 10)
    For y = 0 To MAZE_HEIGHT - 1
        If y <> 10 Then
            Maze(0, y) = TILE_WALL
            Maze(MAZE_WIDTH - 1, y) = TILE_WALL
        End If
    Next y

    ' Internal wall blocks - simplified classic maze pattern
    ' Row 2 walls
    For x = 2 To 5
        Maze(x, 2) = TILE_WALL
    Next x
    For x = 7 To 11
        Maze(x, 2) = TILE_WALL
    Next x
    For x = 13 To 14
        Maze(x, 2) = TILE_WALL
    Next x
    For x = 16 To 20
        Maze(x, 2) = TILE_WALL
    Next x
    For x = 22 To 25
        Maze(x, 2) = TILE_WALL
    Next x

    ' Row 4 walls
    For x = 2 To 5
        Maze(x, 4) = TILE_WALL
    Next x
    For x = 7 To 8
        Maze(x, 4) = TILE_WALL
    Next x
    For x = 10 To 17
        Maze(x, 4) = TILE_WALL
    Next x
    For x = 19 To 20
        Maze(x, 4) = TILE_WALL
    Next x
    For x = 22 To 25
        Maze(x, 4) = TILE_WALL
    Next x

    ' Row 6 walls (sides)
    For x = 0 To 5
        Maze(x, 6) = TILE_WALL
    Next x
    For x = 22 To 27
        Maze(x, 6) = TILE_WALL
    Next x
    Maze(7, 5) = TILE_WALL
    Maze(8, 5) = TILE_WALL
    Maze(7, 6) = TILE_WALL
    Maze(8, 6) = TILE_WALL
    Maze(13, 5) = TILE_WALL
    Maze(14, 5) = TILE_WALL
    Maze(13, 6) = TILE_WALL
    Maze(14, 6) = TILE_WALL
    Maze(19, 5) = TILE_WALL
    Maze(20, 5) = TILE_WALL
    Maze(19, 6) = TILE_WALL
    Maze(20, 6) = TILE_WALL

    ' Row 8 walls
    For x = 0 To 5
        Maze(x, 8) = TILE_WALL
    Next x
    Maze(7, 8) = TILE_WALL
    Maze(8, 8) = TILE_WALL
    For x = 10 To 11
        Maze(x, 8) = TILE_WALL
    Next x
    For x = 16 To 17
        Maze(x, 8) = TILE_WALL
    Next x
    Maze(19, 8) = TILE_WALL
    Maze(20, 8) = TILE_WALL
    For x = 22 To 27
        Maze(x, 8) = TILE_WALL
    Next x

    ' Ghost house area (rows 12-16)
    For x = 10 To 17
        Maze(x, 12) = TILE_WALL
    Next x
    Maze(13, 12) = TILE_DOOR
    Maze(14, 12) = TILE_DOOR
    Maze(10, 13) = TILE_WALL
    Maze(10, 14) = TILE_WALL
    Maze(10, 15) = TILE_WALL
    Maze(17, 13) = TILE_WALL
    Maze(17, 14) = TILE_WALL
    Maze(17, 15) = TILE_WALL
    For x = 10 To 17
        Maze(x, 16) = TILE_WALL
    Next x

    ' More walls
    Maze(7, 9) = TILE_WALL
    Maze(8, 9) = TILE_WALL
    Maze(7, 10) = TILE_WALL
    Maze(8, 10) = TILE_WALL
    Maze(19, 9) = TILE_WALL
    Maze(20, 9) = TILE_WALL
    Maze(19, 10) = TILE_WALL
    Maze(20, 10) = TILE_WALL

    For x = 10 To 11
        Maze(x, 10) = TILE_WALL
    Next x
    For x = 16 To 17
        Maze(x, 10) = TILE_WALL
    Next x

    For x = 0 To 5
        Maze(x, 12) = TILE_WALL
    Next x
    For x = 22 To 27
        Maze(x, 12) = TILE_WALL
    Next x
    Maze(7, 12) = TILE_WALL
    Maze(8, 12) = TILE_WALL
    Maze(19, 12) = TILE_WALL
    Maze(20, 12) = TILE_WALL

    For x = 0 To 5
        Maze(x, 14) = TILE_WALL
    Next x
    For x = 22 To 27
        Maze(x, 14) = TILE_WALL
    Next x
    Maze(7, 13) = TILE_WALL
    Maze(8, 13) = TILE_WALL
    Maze(7, 14) = TILE_WALL
    Maze(8, 14) = TILE_WALL
    Maze(19, 13) = TILE_WALL
    Maze(20, 13) = TILE_WALL
    Maze(19, 14) = TILE_WALL
    Maze(20, 14) = TILE_WALL

    ' Row 16 walls
    For x = 2 To 5
        Maze(x, 16) = TILE_WALL
    Next x
    For x = 7 To 11
        Maze(x, 16) = TILE_WALL
    Next x
    For x = 13 To 14
        Maze(x, 16) = TILE_WALL
    Next x
    For x = 16 To 20
        Maze(x, 16) = TILE_WALL
    Next x
    For x = 22 To 25
        Maze(x, 16) = TILE_WALL
    Next x

    ' Row 18 walls
    Maze(4, 17) = TILE_WALL
    Maze(23, 17) = TILE_WALL
    Maze(13, 17) = TILE_WALL
    Maze(14, 17) = TILE_WALL
    Maze(13, 18) = TILE_WALL
    Maze(14, 18) = TILE_WALL

    For x = 0 To 1
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 4 To 5
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 7 To 8
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 10 To 17
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 19 To 20
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 22 To 23
        Maze(x, 18) = TILE_WALL
    Next x
    For x = 26 To 27
        Maze(x, 18) = TILE_WALL
    Next x

    Maze(7, 19) = TILE_WALL
    Maze(8, 19) = TILE_WALL
    Maze(19, 19) = TILE_WALL
    Maze(20, 19) = TILE_WALL

    ' Fill with pellets
    Dim inGhostHouse As Integer
    For y = 1 To MAZE_HEIGHT - 2
        For x = 1 To MAZE_WIDTH - 2
            If Maze(x, y) = TILE_EMPTY Then
                ' Check if in ghost house interior
                inGhostHouse = 0
                If x >= 11 Then
                    If x <= 16 Then
                        If y >= 13 Then
                            If y <= 15 Then
                                inGhostHouse = 1
                            End If
                        End If
                    End If
                End If

                If inGhostHouse = 0 Then
                    Maze(x, y) = TILE_PELLET
                End If
            End If
        Next x
    Next y

    ' Power pellets
    Maze(1, 3) = TILE_POWER
    Maze(26, 3) = TILE_POWER
    Maze(1, 17) = TILE_POWER
    Maze(26, 17) = TILE_POWER

    ' Clear tunnel pellets
    For x = 1 To 5
        If Maze(x, 10) = TILE_PELLET Then Maze(x, 10) = TILE_EMPTY
    Next x
    For x = 22 To 26
        If Maze(x, 10) = TILE_PELLET Then Maze(x, 10) = TILE_EMPTY
    Next x

    ' Count pellets
    PelletsRemaining = 0
    For y = 0 To MAZE_HEIGHT - 1
        For x = 0 To MAZE_WIDTH - 1
            If Maze(x, y) = TILE_PELLET Then PelletsRemaining = PelletsRemaining + 1
            If Maze(x, y) = TILE_POWER Then PelletsRemaining = PelletsRemaining + 1
        Next x
    Next y
End Sub

' ============================================================================
' RENDERING
' ============================================================================

Sub DrawMaze()
    Dim x As Integer
    Dim y As Integer
    Dim tile As Integer

    For y = 0 To MAZE_HEIGHT - 1
        LOCATE y + 2, 1
        For x = 0 To MAZE_WIDTH - 1
            tile = Maze(x, y)

            If tile = TILE_WALL Then
                COLOR COL_WALL, 0
                Print Chr$(219);
            End If
            If tile = TILE_PELLET Then
                COLOR COL_PELLET, 0
                Print ".";
            End If
            If tile = TILE_POWER Then
                COLOR COL_POWER, 0
                Print "o";
            End If
            If tile = TILE_DOOR Then
                COLOR 5, 0
                Print "-";
            End If
            If tile = TILE_EMPTY Then
                Print " ";
            End If
        Next x
    Next y
    COLOR 7, 0
End Sub

Sub DrawPlayer()
    If Pac.Alive = 0 Then Return

    Dim sy As Integer
    Dim sx As Integer
    sy = Pac.Y + 2
    sx = Pac.X + 1

    If sy < 1 Then sy = 1
    If sy > 24 Then sy = 24
    If sx < 1 Then sx = 1
    If sx > 80 Then sx = 80

    LOCATE sy, sx
    COLOR COL_PACMAN, 0
    Print Pac.GetChar();
    COLOR 7, 0
End Sub

Sub DrawGhosts()
    Dim i As Integer
    Dim g As Ghost
    Dim sy As Integer
    Dim sx As Integer
    Dim shouldDraw As Integer

    For i = 0 To 3
        g = Ghosts(i)

        ' Determine if we should draw this ghost
        shouldDraw = 1
        If g.InHouse = 1 Then
            If g.ReleaseDelay > 0 Then
                shouldDraw = 0
            End If
        End If

        If shouldDraw = 1 Then
            sy = g.Y + 2
            sx = g.X + 1

            ' Bounds check
            If sy < 1 Then sy = 1
            If sy > 24 Then sy = 24
            If sx < 1 Then sx = 1
            If sx > 80 Then sx = 80

            LOCATE sy, sx
            COLOR g.GetColor(), 0
            Print g.GetChar();
            COLOR 7, 0
        End If
    Next i
End Sub

Sub DrawUI()
    LOCATE 1, 1
    COLOR 14, 0
    Print "Score: "; GameScore; "  "

    LOCATE 1, 20
    Print "High: "; GameHighScore; "  "

    LOCATE 1, 40
    COLOR 7, 0
    Print "Level: "; GameLevel

    LOCATE 1, 55
    COLOR COL_PACMAN, 0
    Print "Lives: ";
    Dim i As Integer
    For i = 1 To GameLives
        Print "C ";
    Next i
    Print "  "

    If PowerTimer > 0 Then
        LOCATE MAZE_HEIGHT + 3, 1
        COLOR 12, 0
        Print "POWER! "; PowerTimer / 12; "s  "
    Else
        LOCATE MAZE_HEIGHT + 3, 1
        Print "                    "
    End If
    COLOR 7, 0
End Sub

' ============================================================================
' COLLISION AND MOVEMENT
' ============================================================================

Function CanMove(x As Integer, y As Integer) As Integer
    If x < 0 Then Return 1
    If x >= MAZE_WIDTH Then Return 1
    If y < 0 Then Return 0
    If y >= MAZE_HEIGHT Then Return 0

    Dim tile As Integer
    tile = Maze(x, y)
    If tile = TILE_WALL Then Return 0
    If tile = TILE_DOOR Then Return 0
    Return 1
End Function

Function CanGhostMove(x As Integer, y As Integer, eaten As Integer) As Integer
    If x < 0 Then Return 1
    If x >= MAZE_WIDTH Then Return 1
    If y < 0 Then Return 0
    If y >= MAZE_HEIGHT Then Return 0

    Dim tile As Integer
    tile = Maze(x, y)
    If tile = TILE_WALL Then Return 0
    If tile = TILE_DOOR Then
        If eaten = 1 Then Return 1
        Return 0
    End If
    Return 1
End Function

' ============================================================================
' INPUT HANDLING
' ============================================================================

Sub HandleInput()
    Dim k As String
    k = Inkey$()
    If k = "" Then Return

    If k = "w" Then Pac.NextDir = DIR_UP
    If k = "W" Then Pac.NextDir = DIR_UP
    If k = "s" Then Pac.NextDir = DIR_DOWN
    If k = "S" Then Pac.NextDir = DIR_DOWN
    If k = "a" Then Pac.NextDir = DIR_LEFT
    If k = "A" Then Pac.NextDir = DIR_LEFT
    If k = "d" Then Pac.NextDir = DIR_RIGHT
    If k = "D" Then Pac.NextDir = DIR_RIGHT
    If k = "q" Then GameRunning = 0
    If k = "Q" Then GameRunning = 0
End Sub

' ============================================================================
' PLAYER UPDATE
' ============================================================================

Sub UpdatePlayer()
    If Pac.Alive = 0 Then Return

    Dim nx As Integer
    Dim ny As Integer

    ' Try to turn
    If Pac.NextDir <> DIR_NONE Then
        nx = Pac.X
        ny = Pac.Y
        If Pac.NextDir = DIR_UP Then ny = Pac.Y - 1
        If Pac.NextDir = DIR_DOWN Then ny = Pac.Y + 1
        If Pac.NextDir = DIR_LEFT Then nx = Pac.X - 1
        If Pac.NextDir = DIR_RIGHT Then nx = Pac.X + 1

        If CanMove(nx, ny) = 1 Then
            Pac.Dir = Pac.NextDir
        End If
    End If

    ' Move
    If Pac.Dir <> DIR_NONE Then
        nx = Pac.X
        ny = Pac.Y
        If Pac.Dir = DIR_UP Then ny = Pac.Y - 1
        If Pac.Dir = DIR_DOWN Then ny = Pac.Y + 1
        If Pac.Dir = DIR_LEFT Then nx = Pac.X - 1
        If Pac.Dir = DIR_RIGHT Then nx = Pac.X + 1

        If CanMove(nx, ny) = 1 Then
            Pac.X = nx
            Pac.Y = ny

            ' Wrap tunnel
            If Pac.X < 0 Then Pac.X = MAZE_WIDTH - 1
            If Pac.X >= MAZE_WIDTH Then Pac.X = 0

            ' Collect pellet
            Dim tile As Integer
            tile = Maze(Pac.X, Pac.Y)
            If tile = TILE_PELLET Then
                Maze(Pac.X, Pac.Y) = TILE_EMPTY
                GameScore = GameScore + 10
                PelletsRemaining = PelletsRemaining - 1
            End If
            If tile = TILE_POWER Then
                Maze(Pac.X, Pac.Y) = TILE_EMPTY
                GameScore = GameScore + 50
                PelletsRemaining = PelletsRemaining - 1
                ActivatePower()
            End If
        Else
            Pac.Dir = DIR_NONE
        End If
    End If
End Sub

Sub ActivatePower()
    Dim i As Integer
    Dim g As Ghost

    PowerTimer = 300
    GhostsEatenCount = 0

    For i = 0 To 3
        g = Ghosts(i)
        If g.Eaten = 0 Then
            g.Vulnerable = 1
            ' Reverse direction
            If g.Dir = DIR_UP Then
                g.Dir = DIR_DOWN
            Else
                If g.Dir = DIR_DOWN Then
                    g.Dir = DIR_UP
                Else
                    If g.Dir = DIR_LEFT Then
                        g.Dir = DIR_RIGHT
                    Else
                        If g.Dir = DIR_RIGHT Then
                            g.Dir = DIR_LEFT
                        End If
                    End If
                End If
            End If
        End If
        Ghosts(i) = g
    Next i
End Sub

Sub DeactivatePower()
    Dim i As Integer
    Dim g As Ghost

    PowerTimer = 0
    For i = 0 To 3
        g = Ghosts(i)
        g.Vulnerable = 0
        Ghosts(i) = g
    Next i
End Sub

' ============================================================================
' GHOST AI
' ============================================================================

Sub UpdateGhosts()
    Dim i As Integer
    Dim g As Ghost

    For i = 0 To 3
        g = Ghosts(i)
        UpdateSingleGhost(g)
        Ghosts(i) = g
    Next i

    ' Power timer
    If PowerTimer > 0 Then
        PowerTimer = PowerTimer - 1
        If PowerTimer = 0 Then
            DeactivatePower()
        End If
    End If
End Sub

Sub UpdateSingleGhost(g As Ghost)
    ' Release from house
    If g.InHouse = 1 Then
        If g.ReleaseDelay > 0 Then
            g.ReleaseDelay = g.ReleaseDelay - 1
            Return
        End If
        ' Exit house
        g.InHouse = 0
        g.X = 14
        g.Y = 11
        g.Dir = DIR_LEFT
    End If

    ' Calculate target
    If g.Eaten = 1 Then
        g.TargetX = 14
        g.TargetY = 14
    Else
        If g.Vulnerable = 1 Then
            ' Random movement when vulnerable
            g.TargetX = Int(Rnd() * MAZE_WIDTH)
            g.TargetY = Int(Rnd() * MAZE_HEIGHT)
        Else
            ' Chase Pac-Man
            g.TargetX = Pac.X
            g.TargetY = Pac.Y

            ' Different targeting per ghost
            If g.GhostType = 1 Then
                ' Pinky - ahead of pac-man
                If Pac.Dir = DIR_UP Then g.TargetY = Pac.Y - 4
                If Pac.Dir = DIR_DOWN Then g.TargetY = Pac.Y + 4
                If Pac.Dir = DIR_LEFT Then g.TargetX = Pac.X - 4
                If Pac.Dir = DIR_RIGHT Then g.TargetX = Pac.X + 4
            End If
        End If
    End If

    ' Choose best direction
    Dim bestDir As Integer
    Dim bestDist As Integer
    Dim testX As Integer
    Dim testY As Integer
    Dim dx As Integer
    Dim dy As Integer
    Dim testDist As Integer

    bestDir = g.Dir
    bestDist = 9999

    ' Check up
    If g.Dir <> DIR_DOWN Then
        testX = g.X
        testY = g.Y - 1
        If CanGhostMove(testX, testY, g.Eaten) = 1 Then
            dx = g.TargetX - testX
            dy = g.TargetY - testY
            If dx < 0 Then dx = 0 - dx
            If dy < 0 Then dy = 0 - dy
            testDist = dx + dy
            If testDist < bestDist Then
                bestDist = testDist
                bestDir = DIR_UP
            End If
        End If
    End If

    ' Check left
    If g.Dir <> DIR_RIGHT Then
        testX = g.X - 1
        testY = g.Y
        If CanGhostMove(testX, testY, g.Eaten) = 1 Then
            dx = g.TargetX - testX
            dy = g.TargetY - testY
            If dx < 0 Then dx = 0 - dx
            If dy < 0 Then dy = 0 - dy
            testDist = dx + dy
            If testDist < bestDist Then
                bestDist = testDist
                bestDir = DIR_LEFT
            End If
        End If
    End If

    ' Check down
    If g.Dir <> DIR_UP Then
        testX = g.X
        testY = g.Y + 1
        If CanGhostMove(testX, testY, g.Eaten) = 1 Then
            dx = g.TargetX - testX
            dy = g.TargetY - testY
            If dx < 0 Then dx = 0 - dx
            If dy < 0 Then dy = 0 - dy
            testDist = dx + dy
            If testDist < bestDist Then
                bestDist = testDist
                bestDir = DIR_DOWN
            End If
        End If
    End If

    ' Check right
    If g.Dir <> DIR_LEFT Then
        testX = g.X + 1
        testY = g.Y
        If CanGhostMove(testX, testY, g.Eaten) = 1 Then
            dx = g.TargetX - testX
            dy = g.TargetY - testY
            If dx < 0 Then dx = 0 - dx
            If dy < 0 Then dy = 0 - dy
            testDist = dx + dy
            If testDist < bestDist Then
                bestDist = testDist
                bestDir = DIR_RIGHT
            End If
        End If
    End If

    g.Dir = bestDir

    ' Move
    Dim nx As Integer
    Dim ny As Integer
    nx = g.X
    ny = g.Y
    If g.Dir = DIR_UP Then ny = g.Y - 1
    If g.Dir = DIR_DOWN Then ny = g.Y + 1
    If g.Dir = DIR_LEFT Then nx = g.X - 1
    If g.Dir = DIR_RIGHT Then nx = g.X + 1

    If CanGhostMove(nx, ny, g.Eaten) = 1 Then
        g.X = nx
        g.Y = ny
        ' Tunnel wrap
        If g.X < 0 Then g.X = MAZE_WIDTH - 1
        If g.X >= MAZE_WIDTH Then g.X = 0
    End If

    ' Check if eaten ghost reached home
    If g.Eaten = 1 Then
        If g.X >= 12 Then
            If g.X <= 15 Then
                If g.Y >= 13 Then
                    If g.Y <= 14 Then
                        g.Eaten = 0
                        g.InHouse = 1
                        g.ReleaseDelay = 20
                        g.X = g.StartX
                        g.Y = g.StartY
                    End If
                End If
            End If
        End If
    End If
End Sub

' ============================================================================
' COLLISION CHECKING
' ============================================================================

Sub CheckCollisions()
    Dim i As Integer
    Dim g As Ghost

    For i = 0 To 3
        g = Ghosts(i)

        If g.InHouse = 0 Then
            If g.Eaten = 0 Then
                If Pac.X = g.X Then
                    If Pac.Y = g.Y Then
                        If g.Vulnerable = 1 Then
                            ' Eat ghost
                            g.Eaten = 1
                            g.Vulnerable = 0
                            GhostsEatenCount = GhostsEatenCount + 1

                            Dim pts As Integer
                            pts = 200
                            If GhostsEatenCount = 2 Then pts = 400
                            If GhostsEatenCount = 3 Then pts = 800
                            If GhostsEatenCount = 4 Then pts = 1600
                            GameScore = GameScore + pts
                            Ghosts(i) = g
                        Else
                            ' Player dies
                            PlayerDies()
                            Return
                        End If
                    End If
                End If
            End If
        End If
        Ghosts(i) = g
    Next i
End Sub

Sub PlayerDies()
    Pac.Alive = 0
    GameLives = GameLives - 1

    ' Death animation
    Dim j As Integer
    For j = 1 To 8
        LOCATE Pac.Y + 2, Pac.X + 1
        COLOR COL_PACMAN, 0
        If (j Mod 2) = 0 Then
            Print "X"
        Else
            Print "+"
        End If
        COLOR 7, 0
        Sleep 150
    Next j

    If GameLives > 0 Then
        ResetPositions()
        Sleep 500
    End If
End Sub

Sub ResetPositions()
    Dim i As Integer
    Dim g As Ghost

    Pac.Reset()

    For i = 0 To 3
        g = Ghosts(i)
        ' Inline reset since method calls on array copies don't work
        g.X = g.StartX
        g.Y = g.StartY
        g.Dir = DIR_UP
        g.Vulnerable = 0
        g.Eaten = 0
        g.AnimFrame = 0
        If g.GhostType = 0 Then
            g.InHouse = 0
        Else
            g.InHouse = 1
        End If
        Ghosts(i) = g
    Next i
End Sub

' ============================================================================
' LEVEL MANAGEMENT
' ============================================================================

Sub NextLevel()
    GameLevel = GameLevel + 1
    GameScore = GameScore + 1000

    LOCATE 10, 10
    COLOR 10, 0
    Print "LEVEL COMPLETE! +1000 pts"
    COLOR 7, 0
    Sleep 2000

    InitMaze()
    ResetPositions()
End Sub

' ============================================================================
' GAME INITIALIZATION
' ============================================================================

Sub InitGame()
    GameScore = 0
    GameLevel = 1
    GameLives = 3
    PowerTimer = 0
    GhostsEatenCount = 0
    GameRunning = 1
    FrameCounter = 0

    Randomize

    Pac = New Player()

    Dim i As Integer
    For i = 0 To 3
        Ghosts(i) = New Ghost()
        Ghosts(i).Setup(i)
    Next i

    InitMaze()

    CLS
    DrawMaze()
    DrawUI()
End Sub

' ============================================================================
' MAIN MENU
' ============================================================================

Sub ShowMenu()
    CLS

    LOCATE 4, 18
    COLOR 14, 0
    Print "=================================="
    LOCATE 5, 18
    Print "         BUGS PACMAN              "
    LOCATE 6, 18
    Print "     A Viper BASIC Game           "
    LOCATE 7, 18
    Print "=================================="

    COLOR 7, 0
    LOCATE 10, 20
    Print "Controls:"
    LOCATE 11, 20
    Print "  W/A/S/D - Move"
    LOCATE 12, 20
    Print "  Q - Quit"

    LOCATE 14, 20
    COLOR 3, 0
    Print "Ghosts:"
    LOCATE 15, 20
    COLOR COL_BLINKY, 0
    Print "  M Blinky - Chases you"
    LOCATE 16, 20
    COLOR COL_PINKY, 0
    Print "  M Pinky  - Ambushes ahead"
    LOCATE 17, 20
    COLOR COL_INKY, 0
    Print "  M Inky   - Unpredictable"
    LOCATE 18, 20
    COLOR COL_CLYDE, 0
    Print "  M Clyde  - Random"

    COLOR 15, 0
    LOCATE 21, 20
    Print "Press any key to start..."

    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' ============================================================================
' GAME LOOP
' ============================================================================

Sub GameLoop()
    While GameRunning = 1
        HandleInput()

        If Pac.Alive = 1 Then
            UpdatePlayer()
            UpdateGhosts()
            CheckCollisions()

            If PelletsRemaining = 0 Then
                NextLevel()
            End If

            If GameScore > GameHighScore Then
                GameHighScore = GameScore
            End If
        Else
            If GameLives = 0 Then
                ShowGameOver()
                GameRunning = 0
            End If
        End If

        DrawMaze()
        DrawPlayer()
        DrawGhosts()
        DrawUI()

        Sleep FRAME_DELAY
        FrameCounter = FrameCounter + 1
    Wend
End Sub

Sub ShowGameOver()
    LOCATE 9, 10
    COLOR 1, 0
    Print "=========================="
    LOCATE 10, 10
    Print "       GAME OVER          "
    LOCATE 11, 10
    Print "  Final Score: "; GameScore; "      "
    LOCATE 12, 10
    Print "=========================="
    COLOR 7, 0

    Sleep 3000
End Sub

' ============================================================================
' MAIN ENTRY POINT
' ============================================================================

GameHighScore = 0
ShowMenu()
InitGame()
GameLoop()

' ============================================================================
' FRUIT/BONUS SYSTEM
' ============================================================================

' Fruit types and their point values
Dim FRUIT_CHERRY As Integer
Dim FRUIT_STRAWBERRY As Integer
Dim FRUIT_ORANGE As Integer
Dim FRUIT_APPLE As Integer
Dim FRUIT_MELON As Integer
Dim FRUIT_GALAXIAN As Integer
Dim FRUIT_BELL As Integer
Dim FRUIT_KEY As Integer
FRUIT_CHERRY = 0
FRUIT_STRAWBERRY = 1
FRUIT_ORANGE = 2
FRUIT_APPLE = 3
FRUIT_MELON = 4
FRUIT_GALAXIAN = 5
FRUIT_BELL = 6
FRUIT_KEY = 7

' Fruit state
Dim FruitActive As Integer
Dim FruitType As Integer
Dim FruitX As Integer
Dim FruitY As Integer
Dim FruitTimer As Integer
Dim FruitSpawnTimer As Integer
Dim FruitsCollected As Integer

Function GetFruitPoints(ftype As Integer) As Integer
    If ftype = FRUIT_CHERRY Then Return 100
    If ftype = FRUIT_STRAWBERRY Then Return 300
    If ftype = FRUIT_ORANGE Then Return 500
    If ftype = FRUIT_APPLE Then Return 700
    If ftype = FRUIT_MELON Then Return 1000
    If ftype = FRUIT_GALAXIAN Then Return 2000
    If ftype = FRUIT_BELL Then Return 3000
    If ftype = FRUIT_KEY Then Return 5000
    Return 100
End Function

Function GetFruitChar(ftype As Integer) As String
    If ftype = FRUIT_CHERRY Then Return "@"
    If ftype = FRUIT_STRAWBERRY Then Return "*"
    If ftype = FRUIT_ORANGE Then Return "O"
    If ftype = FRUIT_APPLE Then Return "A"
    If ftype = FRUIT_MELON Then Return "M"
    If ftype = FRUIT_GALAXIAN Then Return "G"
    If ftype = FRUIT_BELL Then Return "B"
    If ftype = FRUIT_KEY Then Return "K"
    Return "@"
End Function

Function GetFruitColor(ftype As Integer) As Integer
    If ftype = FRUIT_CHERRY Then Return 1
    If ftype = FRUIT_STRAWBERRY Then Return 9
    If ftype = FRUIT_ORANGE Then Return 6
    If ftype = FRUIT_APPLE Then Return 2
    If ftype = FRUIT_MELON Then Return 10
    If ftype = FRUIT_GALAXIAN Then Return 11
    If ftype = FRUIT_BELL Then Return 14
    If ftype = FRUIT_KEY Then Return 15
    Return 1
End Function

Function GetLevelFruit(level As Integer) As Integer
    If level = 1 Then Return FRUIT_CHERRY
    If level = 2 Then Return FRUIT_STRAWBERRY
    If level = 3 Then Return FRUIT_ORANGE
    If level = 4 Then Return FRUIT_ORANGE
    If level = 5 Then Return FRUIT_APPLE
    If level = 6 Then Return FRUIT_APPLE
    If level = 7 Then Return FRUIT_MELON
    If level = 8 Then Return FRUIT_MELON
    If level = 9 Then Return FRUIT_GALAXIAN
    If level = 10 Then Return FRUIT_GALAXIAN
    If level = 11 Then Return FRUIT_BELL
    If level = 12 Then Return FRUIT_BELL
    If level >= 13 Then Return FRUIT_KEY
    Return FRUIT_CHERRY
End Function

Sub SpawnFruit()
    FruitActive = 1
    FruitType = GetLevelFruit(GameLevel)
    FruitX = 14
    FruitY = 14
    FruitTimer = 500
End Sub

Sub UpdateFruit()
    If FruitActive = 0 Then
        FruitSpawnTimer = FruitSpawnTimer + 1
        ' Spawn fruit after eating certain number of pellets
        Dim pelletsEaten As Integer
        pelletsEaten = 244 - PelletsRemaining
        If pelletsEaten = 70 Then SpawnFruit()
        If pelletsEaten = 170 Then SpawnFruit()
    Else
        FruitTimer = FruitTimer - 1
        If FruitTimer <= 0 Then
            FruitActive = 0
        End If
    End If
End Sub

Sub CollectFruit()
    If FruitActive = 1 Then
        If Pac.X = FruitX Then
            If Pac.Y = FruitY Then
                Dim pts As Integer
                pts = GetFruitPoints(FruitType)
                GameScore = GameScore + pts
                FruitActive = 0
                FruitsCollected = FruitsCollected + 1

                ' Flash bonus points
                LOCATE FruitY + 2, FruitX + 1
                COLOR 15, 0
                Print pts;
                COLOR 7, 0
                Sleep 500
            End If
        End If
    End If
End Sub

Sub DrawFruit()
    If FruitActive = 1 Then
        Dim sy As Integer
        Dim sx As Integer
        sy = FruitY + 2
        sx = FruitX + 1

        If sy >= 1 Then
            If sy <= 24 Then
                If sx >= 1 Then
                    If sx <= 80 Then
                        LOCATE sy, sx
                        COLOR GetFruitColor(FruitType), 0
                        Print GetFruitChar(FruitType);
                        COLOR 7, 0
                    End If
                End If
            End If
        End If
    End If
End Sub

' ============================================================================
' SCATTER/CHASE MODE TIMING
' ============================================================================

Dim ModeTimer As Integer
Dim CurrentMode As Integer
Dim ModePhase As Integer

' Mode constants
Dim MODE_SCATTER As Integer
Dim MODE_CHASE As Integer
MODE_SCATTER = 0
MODE_CHASE = 1

Sub InitModeTimer()
    ModeTimer = 0
    CurrentMode = MODE_SCATTER
    ModePhase = 0
End Sub

Sub UpdateModeTimer()
    ModeTimer = ModeTimer + 1

    ' Classic Pac-Man mode timing (in frames at ~12fps)
    ' Scatter for 7s, Chase for 20s, Scatter for 7s, Chase for 20s...
    ' Then permanent chase after 4 cycles

    Dim scatterFrames As Integer
    Dim chaseFrames As Integer
    scatterFrames = 84
    chaseFrames = 240

    If ModePhase < 4 Then
        If CurrentMode = MODE_SCATTER Then
            If ModeTimer >= scatterFrames Then
                CurrentMode = MODE_CHASE
                ModeTimer = 0
                ModePhase = ModePhase + 1
                ReverseAllGhosts()
            End If
        Else
            If ModeTimer >= chaseFrames Then
                CurrentMode = MODE_SCATTER
                ModeTimer = 0
                ReverseAllGhosts()
            End If
        End If
    End If
End Sub

Sub ReverseAllGhosts()
    Dim i As Integer
    Dim g As Ghost

    For i = 0 To 3
        g = Ghosts(i)
        If g.Vulnerable = 0 Then
            If g.Eaten = 0 Then
                If g.Dir = DIR_UP Then
                    g.Dir = DIR_DOWN
                Else
                    If g.Dir = DIR_DOWN Then
                        g.Dir = DIR_UP
                    Else
                        If g.Dir = DIR_LEFT Then
                            g.Dir = DIR_RIGHT
                        Else
                            If g.Dir = DIR_RIGHT Then
                                g.Dir = DIR_LEFT
                            End If
                        End If
                    End If
                End If
            End If
        End If
        Ghosts(i) = g
    Next i
End Sub

' ============================================================================
' STATISTICS TRACKING
' ============================================================================

Dim TotalPelletsEaten As Integer
Dim TotalGhostsEaten As Integer
Dim TotalFruitsEaten As Integer
Dim TotalDeaths As Integer
Dim LongestPowerStreak As Integer
Dim CurrentPowerStreak As Integer
Dim GamesPlayed As Integer
Dim HighestLevel As Integer
Dim TotalPlayTime As Integer

Sub InitStats()
    TotalPelletsEaten = 0
    TotalGhostsEaten = 0
    TotalFruitsEaten = 0
    TotalDeaths = 0
    LongestPowerStreak = 0
    CurrentPowerStreak = 0
    GamesPlayed = 0
    HighestLevel = 1
    TotalPlayTime = 0
End Sub

Sub UpdateStats()
    TotalPlayTime = TotalPlayTime + 1
End Sub

Sub ShowStats()
    CLS
    LOCATE 3, 20
    COLOR 14, 0
    Print "=================================="
    LOCATE 4, 20
    Print "       GAME STATISTICS            "
    LOCATE 5, 20
    Print "=================================="

    COLOR 7, 0
    LOCATE 7, 20
    Print "Games Played: "; GamesPlayed

    LOCATE 8, 20
    Print "Highest Level: "; HighestLevel

    LOCATE 9, 20
    Print "Total Pellets: "; TotalPelletsEaten

    LOCATE 10, 20
    Print "Ghosts Eaten: "; TotalGhostsEaten

    LOCATE 11, 20
    Print "Fruits Collected: "; TotalFruitsEaten

    LOCATE 12, 20
    Print "Total Deaths: "; TotalDeaths

    LOCATE 13, 20
    Print "Best Ghost Streak: "; LongestPowerStreak

    Dim minutes As Integer
    Dim seconds As Integer
    minutes = TotalPlayTime / 720
    seconds = TotalPlayTime / 12
    seconds = seconds - (minutes * 60)
    LOCATE 14, 20
    Print "Play Time: "; minutes; "m "; seconds; "s"

    LOCATE 17, 20
    COLOR 15, 0
    Print "Press any key to continue..."
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' ============================================================================
' READY SCREEN
' ============================================================================

Sub ShowReady()
    LOCATE 11, 11
    COLOR 14, 0
    Print "READY!"
    COLOR 7, 0
    Sleep 1500

    LOCATE 11, 11
    Print "      "
End Sub

Sub ShowGetReady()
    CLS
    DrawMaze()
    DrawUI()

    LOCATE 11, 10
    COLOR 14, 0
    Print "GET READY!"
    COLOR 7, 0
    Sleep 2000

    LOCATE 11, 10
    Print "          "
End Sub

' ============================================================================
' ENHANCED MAIN MENU
' ============================================================================

Sub ShowEnhancedMenu()
    Dim choice As Integer
    choice = 0

    While choice = 0
        CLS

        LOCATE 2, 18
        COLOR 14, 0
        Print "  ____  _    _  _____  _____  "
        LOCATE 3, 18
        Print " |  _ \\| |  | |/ ____||  ___| "
        LOCATE 4, 18
        Print " | |_) | |  | | |  __ | |___  "
        LOCATE 5, 18
        Print " |  _ <| |  | | | |_ ||___  | "
        LOCATE 6, 18
        Print " | |_) | |__| | |__| | ___| | "
        LOCATE 7, 18
        Print " |____/ \\____/ \\_____||_____/ "

        LOCATE 9, 18
        Print " ____   _    ____  __  __    _    _   _ "
        LOCATE 10, 18
        Print "|  _ \\ / \\  / ___||  \\/  |  / \\  | \\ | |"
        LOCATE 11, 18
        Print "| |_) / _ \\| |    | |\\/| | / _ \\ |  \\| |"
        LOCATE 12, 18
        Print "|  __/ ___ \\ |___ | |  | |/ ___ \\| |\\  |"
        LOCATE 13, 18
        Print "|_| /_/   \\_\\____||_|  |_/_/   \\_\\_| \\_|"

        COLOR 7, 0
        LOCATE 16, 25
        Print "[1] Start Game"
        LOCATE 17, 25
        Print "[2] View Statistics"
        LOCATE 18, 25
        Print "[3] How To Play"
        LOCATE 19, 25
        Print "[Q] Quit"

        LOCATE 21, 20
        COLOR 11, 0
        Print "High Score: "; GameHighScore
        COLOR 7, 0

        Dim k As String
        k = ""
        While k = ""
            k = Inkey$()
        Wend

        If k = "1" Then choice = 1
        If k = "2" Then choice = 2
        If k = "3" Then choice = 3
        If k = "q" Then choice = 4
        If k = "Q" Then choice = 4

        If choice = 2 Then
            ShowStats()
            choice = 0
        End If

        If choice = 3 Then
            ShowHowToPlay()
            choice = 0
        End If
    Wend

    If choice = 4 Then
        GameRunning = 0
    End If
End Sub

Sub ShowHowToPlay()
    CLS

    LOCATE 2, 20
    COLOR 14, 0
    Print "=================================="
    LOCATE 3, 20
    Print "         HOW TO PLAY              "
    LOCATE 4, 20
    Print "=================================="

    COLOR 7, 0
    LOCATE 6, 5
    Print "OBJECTIVE:"
    LOCATE 7, 5
    Print "  Eat all the pellets (.) in the maze while avoiding ghosts."

    LOCATE 9, 5
    Print "CONTROLS:"
    LOCATE 10, 5
    Print "  W - Move Up"
    LOCATE 11, 5
    Print "  S - Move Down"
    LOCATE 12, 5
    Print "  A - Move Left"
    LOCATE 13, 5
    Print "  D - Move Right"
    LOCATE 14, 5
    Print "  Q - Quit Game"

    LOCATE 16, 5
    Print "POWER PELLETS (o):"
    LOCATE 17, 5
    Print "  Eating a power pellet makes ghosts vulnerable (turn blue)."
    LOCATE 18, 5
    Print "  You can eat vulnerable ghosts for bonus points!"

    LOCATE 20, 5
    COLOR 11, 0
    Print "GHOST PERSONALITIES:"
    COLOR COL_BLINKY, 0
    LOCATE 21, 5
    Print "  BLINKY - Chases you directly"
    COLOR COL_PINKY, 0
    LOCATE 22, 5
    Print "  PINKY  - Tries to ambush ahead of you"
    COLOR COL_INKY, 0
    LOCATE 23, 5
    Print "  INKY   - Unpredictable, uses Blinky's position"
    COLOR COL_CLYDE, 0
    LOCATE 24, 5
    Print "  CLYDE  - Random, backs off when close"

    COLOR 15, 0
    LOCATE 26, 20
    Print "Press any key to return..."
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' ============================================================================
' ENHANCED SCORING DISPLAY
' ============================================================================

Sub ShowBonusAnimation(points As Integer, x As Integer, y As Integer)
    Dim sy As Integer
    Dim sx As Integer
    sy = y + 2
    sx = x + 1

    If sy >= 1 Then
        If sy <= 24 Then
            If sx >= 1 Then
                If sx <= 70 Then
                    LOCATE sy, sx
                    COLOR 15, 0
                    Print points;
                    COLOR 7, 0
                    Sleep 300
                End If
            End If
        End If
    End If
End Sub

' ============================================================================
' ENHANCED LEVEL SYSTEM
' ============================================================================

Function GetGhostSpeed(level As Integer) As Integer
    ' Ghosts get faster each level
    Dim baseSpeed As Integer
    baseSpeed = 3

    If level >= 2 Then baseSpeed = 2
    If level >= 5 Then baseSpeed = 2
    If level >= 10 Then baseSpeed = 1

    Return baseSpeed
End Function

Function GetPowerDuration(level As Integer) As Integer
    ' Power mode gets shorter each level
    Dim baseDuration As Integer
    baseDuration = 300

    If level >= 2 Then baseDuration = 250
    If level >= 5 Then baseDuration = 200
    If level >= 10 Then baseDuration = 150
    If level >= 15 Then baseDuration = 100
    If level >= 20 Then baseDuration = 50

    Return baseDuration
End Function

' ============================================================================
' LEVEL TRANSITION
' ============================================================================

Sub ShowLevelTransition()
    LOCATE 9, 10
    COLOR 10, 0
    Print "==============================="
    LOCATE 10, 10
    Print "       LEVEL "; GameLevel; " COMPLETE!     "
    LOCATE 11, 10
    Print "==============================="
    LOCATE 12, 10
    Print "       Bonus: 1000 pts         "
    LOCATE 13, 10
    Print "==============================="
    COLOR 7, 0

    Sleep 2000

    LOCATE 15, 10
    COLOR 14, 0
    Print "   Get ready for Level "; GameLevel + 1; "...   "
    COLOR 7, 0

    Sleep 1500
End Sub

' ============================================================================
' ENHANCED GAME OVER SCREEN
' ============================================================================

Sub ShowEnhancedGameOver()
    CLS

    LOCATE 5, 18
    COLOR 1, 0
    Print "=================================="
    LOCATE 6, 18
    Print "          GAME OVER               "
    LOCATE 7, 18
    Print "=================================="

    COLOR 7, 0
    LOCATE 9, 20
    Print "Final Score: "; GameScore

    LOCATE 10, 20
    If GameScore > GameHighScore Then
        COLOR 14, 0
        Print "NEW HIGH SCORE!"
        GameHighScore = GameScore
    Else
        Print "High Score: "; GameHighScore
    End If

    COLOR 7, 0
    LOCATE 12, 20
    Print "Level Reached: "; GameLevel

    LOCATE 13, 20
    Print "Pellets Eaten: "; 244 - PelletsRemaining + (GameLevel - 1) * 244

    LOCATE 14, 20
    Print "Ghosts Eaten: "; GhostsEatenCount

    LOCATE 15, 20
    Print "Fruits Collected: "; FruitsCollected

    ' Update global stats
    TotalDeaths = TotalDeaths + 1
    GamesPlayed = GamesPlayed + 1
    If GameLevel > HighestLevel Then
        HighestLevel = GameLevel
    End If

    COLOR 15, 0
    LOCATE 18, 20
    Print "Press any key to continue..."
    COLOR 7, 0

    Sleep 3000
End Sub

' ============================================================================
' ALTERNATE MAZE PATTERNS
' ============================================================================

Sub InitMazePattern2()
    ' A different maze layout for variety
    Dim x As Integer
    Dim y As Integer

    ' Clear maze
    For y = 0 To MAZE_HEIGHT - 1
        For x = 0 To MAZE_WIDTH - 1
            Maze(x, y) = TILE_EMPTY
        Next x
    Next y

    ' Border walls
    For x = 0 To MAZE_WIDTH - 1
        Maze(x, 0) = TILE_WALL
        Maze(x, MAZE_HEIGHT - 1) = TILE_WALL
    Next x

    For y = 0 To MAZE_HEIGHT - 1
        If y <> 10 Then
            Maze(0, y) = TILE_WALL
            Maze(MAZE_WIDTH - 1, y) = TILE_WALL
        End If
    Next y

    ' Cross pattern walls
    For x = 5 To 22
        Maze(x, 5) = TILE_WALL
        Maze(x, 15) = TILE_WALL
    Next x

    For y = 5 To 15
        Maze(5, y) = TILE_WALL
        Maze(22, y) = TILE_WALL
    Next y

    ' Inner box
    For x = 10 To 17
        Maze(x, 8) = TILE_WALL
        Maze(x, 12) = TILE_WALL
    Next x
    For y = 8 To 12
        Maze(10, y) = TILE_WALL
        Maze(17, y) = TILE_WALL
    Next y

    ' Ghost house door
    Maze(13, 8) = TILE_DOOR
    Maze(14, 8) = TILE_DOOR

    ' Corner blocks
    For x = 2 To 3
        For y = 2 To 3
            Maze(x, y) = TILE_WALL
        Next y
    Next x
    For x = 24 To 25
        For y = 2 To 3
            Maze(x, y) = TILE_WALL
        Next y
    Next x
    For x = 2 To 3
        For y = 17 To 18
            Maze(x, y) = TILE_WALL
        Next y
    Next x
    For x = 24 To 25
        For y = 17 To 18
            Maze(x, y) = TILE_WALL
        Next y
    Next x

    ' Fill with pellets
    Dim inGhostHouse2 As Integer
    For y = 1 To MAZE_HEIGHT - 2
        For x = 1 To MAZE_WIDTH - 2
            If Maze(x, y) = TILE_EMPTY Then
                ' Check if in ghost house interior
                inGhostHouse2 = 0
                If x >= 11 Then
                    If x <= 16 Then
                        If y >= 9 Then
                            If y <= 11 Then
                                inGhostHouse2 = 1
                            End If
                        End If
                    End If
                End If

                If inGhostHouse2 = 0 Then
                    Maze(x, y) = TILE_PELLET
                End If
            End If
        Next x
    Next y

    ' Power pellets
    Maze(1, 1) = TILE_POWER
    Maze(26, 1) = TILE_POWER
    Maze(1, 19) = TILE_POWER
    Maze(26, 19) = TILE_POWER

    ' Count pellets
    PelletsRemaining = 0
    For y = 0 To MAZE_HEIGHT - 1
        For x = 0 To MAZE_WIDTH - 1
            If Maze(x, y) = TILE_PELLET Then PelletsRemaining = PelletsRemaining + 1
            If Maze(x, y) = TILE_POWER Then PelletsRemaining = PelletsRemaining + 1
        Next x
    Next y
End Sub

' ============================================================================
' SOUND EFFECT VISUALIZATIONS
' ============================================================================

Sub PlayWakaSound()
    ' Visual feedback since we don't have real sound
    ' Just a quick visual flash
End Sub

Sub PlayPowerUpSound()
    LOCATE 1, 70
    COLOR 12, 0
    Print "POW!"
    COLOR 7, 0
End Sub

Sub PlayDeathSound()
    LOCATE 1, 70
    COLOR 1, 0
    Print "DEAD"
    COLOR 7, 0
End Sub

Sub PlayEatGhostSound()
    LOCATE 1, 70
    COLOR 11, 0
    Print "MUNCH!"
    COLOR 7, 0
End Sub

Sub ClearSoundDisplay()
    LOCATE 1, 70
    Print "      "
End Sub

' ============================================================================
' FINAL PROGRAM EXECUTION
' ============================================================================

InitStats()
GameHighScore = 0

ShowEnhancedMenu()

If GameRunning = 1 Then
    GamesPlayed = GamesPlayed + 1
    InitGame()
    InitModeTimer()
    FruitActive = 0
    FruitSpawnTimer = 0
    FruitsCollected = 0
    ShowGetReady()
    ShowReady()
    GameLoop()
    ShowEnhancedGameOver()
End If

CLS
LOCATE 10, 25
Print "Thanks for playing BUGS PACMAN!"
LOCATE 12, 25
Print "Final Score: "; GameScore
LOCATE 14, 25
Print "High Score: "; GameHighScore
LOCATE 16, 25
Print "See you next time!"
