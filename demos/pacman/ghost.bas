' ============================================================================
' PACMAN - Ghost Module
' ============================================================================
' Contains ghost AI and behavior
' ============================================================================

' Ghost mode constants
Dim MODE_CHASE As Integer
Dim MODE_SCATTER As Integer
Dim MODE_FRIGHTENED As Integer
Dim MODE_EATEN As Integer

' Ghost colors
Dim COLOR_BLINKY As Integer  ' Red ghost
Dim COLOR_PINKY As Integer   ' Pink ghost
Dim COLOR_INKY As Integer    ' Cyan ghost
Dim COLOR_CLYDE As Integer   ' Orange ghost
Dim COLOR_FRIGHTENED As Integer
Dim COLOR_EATEN As Integer

Sub InitGhostConstants()
    MODE_CHASE = 0
    MODE_SCATTER = 1
    MODE_FRIGHTENED = 2
    MODE_EATEN = 3

    COLOR_BLINKY = 9       ' Bright red
    COLOR_PINKY = 13       ' Bright magenta
    COLOR_INKY = 11        ' Bright cyan
    COLOR_CLYDE = 6        ' Yellow/orange
    COLOR_FRIGHTENED = 1   ' Blue
    COLOR_EATEN = 8        ' Dark gray
End Sub

' ============================================================================
' Ghost Class - Enemy characters
' ============================================================================
Class Ghost
    Dim x As Integer
    Dim y As Integer
    Dim direction As Integer
    Dim mode As Integer
    Dim color As Integer
    Dim startX As Integer
    Dim startY As Integer
    Dim scatterX As Integer  ' Target corner for scatter mode
    Dim scatterY As Integer
    Dim frightTimer As Integer
    Dim ghostId As Integer   ' 0=Blinky, 1=Pinky, 2=Inky, 3=Clyde
    Dim inPen As Integer     ' 1 if ghost is in the pen
    Dim penTimer As Integer  ' Timer until ghost leaves pen
    Dim alive As Integer

    Sub Init(id As Integer, sx As Integer, sy As Integer, clr As Integer)
        ghostId = id
        startX = sx
        startY = sy
        x = sx
        y = sy
        color = clr
        direction = DIR_UP
        mode = MODE_SCATTER
        frightTimer = 0
        inPen = 1
        penTimer = id * 50  ' Stagger ghost releases
        alive = 1

        ' Set scatter targets (corners)
        If id = 0 Then  ' Blinky - top right
            scatterX = 25
            scatterY = 0
        ElseIf id = 1 Then  ' Pinky - top left
            scatterX = 2
            scatterY = 0
        ElseIf id = 2 Then  ' Inky - bottom right
            scatterX = 25
            scatterY = 30
        Else  ' Clyde - bottom left
            scatterX = 2
            scatterY = 30
        End If
    End Sub

    Function GetX() As Integer
        GetX = x
    End Function

    Function GetY() As Integer
        GetY = y
    End Function

    Function GetMode() As Integer
        GetMode = mode
    End Function

    Function IsInPen() As Integer
        IsInPen = inPen
    End Function

    Sub SetFrightened(duration As Integer)
        If mode <> MODE_EATEN Then
            mode = MODE_FRIGHTENED
            frightTimer = duration
            ' Reverse direction when frightened
            If direction = DIR_UP Then
                direction = DIR_DOWN
            ElseIf direction = DIR_DOWN Then
                direction = DIR_UP
            ElseIf direction = DIR_LEFT Then
                direction = DIR_RIGHT
            ElseIf direction = DIR_RIGHT Then
                direction = DIR_LEFT
            End If
        End If
    End Sub

    Sub Eaten()
        mode = MODE_EATEN
        frightTimer = 0
    End Sub

    ' Simple AI: move toward target, avoiding walls
    Function Move(maze As Maze, targetX As Integer, targetY As Integer) As Integer
        Dim newX As Integer
        Dim newY As Integer
        Dim bestDir As Integer
        Dim bestDist As Integer
        Dim testDist As Integer
        Dim dx As Integer
        Dim dy As Integer
        Dim moved As Integer

        moved = 0

        ' Handle pen exit
        If inPen = 1 Then
            penTimer = penTimer - 1
            If penTimer <= 0 Then
                ' Move toward pen exit (14, 11)
                If y > 11 Then
                    y = y - 1
                    moved = 1
                ElseIf y < 11 Then
                    y = y + 1
                    moved = 1
                ElseIf x > 14 Then
                    x = x - 1
                    moved = 1
                ElseIf x < 14 Then
                    x = x + 1
                    moved = 1
                Else
                    inPen = 0
                End If
            End If
            Move = moved
            Exit Function
        End If

        ' Decrement fright timer
        If mode = MODE_FRIGHTENED Then
            frightTimer = frightTimer - 1
            If frightTimer <= 0 Then
                mode = MODE_CHASE
            End If
        End If

        ' If eaten, head back to pen
        If mode = MODE_EATEN Then
            targetX = 14
            targetY = 14
            If x = targetX And y = targetY Then
                mode = MODE_SCATTER
                inPen = 0
            End If
        End If

        ' Determine target based on mode
        If mode = MODE_SCATTER Then
            targetX = scatterX
            targetY = scatterY
        ElseIf mode = MODE_FRIGHTENED Then
            ' Random direction when frightened
            targetX = Int(Rnd() * 28)
            targetY = Int(Rnd() * 31)
        End If

        ' Simple pathfinding: choose direction that gets closest to target
        ' while not reversing direction and avoiding walls
        bestDir = direction
        bestDist = 9999

        ' Try each direction (but not reverse)
        ' Try UP
        If direction <> DIR_DOWN Then
            newX = x
            newY = y - 1
            If maze.IsWalkable(newX, newY) = 1 Or (newY >= 12 And newY <= 16 And newX >= 11 And newX <= 16) Then
                dx = targetX - newX
                dy = targetY - newY
                testDist = dx * dx + dy * dy
                If testDist < bestDist Then
                    bestDist = testDist
                    bestDir = DIR_UP
                End If
            End If
        End If

        ' Try DOWN
        If direction <> DIR_UP Then
            newX = x
            newY = y + 1
            If maze.IsWalkable(newX, newY) = 1 Or (newY >= 12 And newY <= 16 And newX >= 11 And newX <= 16) Then
                dx = targetX - newX
                dy = targetY - newY
                testDist = dx * dx + dy * dy
                If testDist < bestDist Then
                    bestDist = testDist
                    bestDir = DIR_DOWN
                End If
            End If
        End If

        ' Try LEFT
        If direction <> DIR_RIGHT Then
            newX = x - 1
            newY = y
            If newX < 0 Then newX = 27
            If maze.IsWalkable(newX, newY) = 1 Then
                dx = targetX - newX
                dy = targetY - newY
                testDist = dx * dx + dy * dy
                If testDist < bestDist Then
                    bestDist = testDist
                    bestDir = DIR_LEFT
                End If
            End If
        End If

        ' Try RIGHT
        If direction <> DIR_LEFT Then
            newX = x + 1
            newY = y
            If newX >= 28 Then newX = 0
            If maze.IsWalkable(newX, newY) = 1 Then
                dx = targetX - newX
                dy = targetY - newY
                testDist = dx * dx + dy * dy
                If testDist < bestDist Then
                    bestDist = testDist
                    bestDir = DIR_RIGHT
                End If
            End If
        End If

        ' Move in best direction
        direction = bestDir

        If direction = DIR_UP Then
            newY = y - 1
            If maze.IsWalkable(x, newY) = 1 Or (newY >= 12 And newY <= 16 And x >= 11 And x <= 16) Then
                y = newY
                moved = 1
            End If
        ElseIf direction = DIR_DOWN Then
            newY = y + 1
            If maze.IsWalkable(x, newY) = 1 Or (newY >= 12 And newY <= 16 And x >= 11 And x <= 16) Then
                y = newY
                moved = 1
            End If
        ElseIf direction = DIR_LEFT Then
            newX = x - 1
            If newX < 0 Then newX = 27
            If maze.IsWalkable(newX, y) = 1 Then
                x = newX
                moved = 1
            End If
        ElseIf direction = DIR_RIGHT Then
            newX = x + 1
            If newX >= 28 Then newX = 0
            If maze.IsWalkable(newX, y) = 1 Then
                x = newX
                moved = 1
            End If
        End If

        Move = moved
    End Function

    Sub Reset()
        x = startX
        y = startY
        direction = DIR_UP
        mode = MODE_SCATTER
        frightTimer = 0
        inPen = 1
        penTimer = ghostId * 50
    End Sub

    ' Check collision with Pacman
    Function CheckCollision(px As Integer, py As Integer) As Integer
        If x = px And y = py Then
            CheckCollision = 1
        Else
            CheckCollision = 0
        End If
    End Function

    ' Draw ghost at current position
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer
        Dim drawColor As Integer
        Dim ch As String

        screenX = MAZE_LEFT + x
        screenY = MAZE_TOP + y

        Viper.Terminal.SetPosition(screenY, screenX)

        If mode = MODE_FRIGHTENED Then
            drawColor = COLOR_FRIGHTENED
            ch = "M"  ' Scared ghost
        ElseIf mode = MODE_EATEN Then
            drawColor = COLOR_EATEN
            ch = "\""  ' Eyes only
        Else
            drawColor = color
            ch = "M"  ' Normal ghost
        End If

        Viper.Terminal.SetColor(drawColor, 0)
        PRINT ch;
        Viper.Terminal.SetColor(7, 0)
    End Sub

    ' Clear ghost from current position
    Sub Clear(maze As Maze)
        maze.DrawCell(x, y)
    End Sub
End Class
