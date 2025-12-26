' ============================================================================
' PACMAN - Player Module
' ============================================================================
' Contains the Pacman player character
' ============================================================================

' Direction constants
Dim DIR_NONE As Integer
Dim DIR_UP As Integer
Dim DIR_DOWN As Integer
Dim DIR_LEFT As Integer
Dim DIR_RIGHT As Integer

' Pacman colors
Dim COLOR_PACMAN As Integer

Sub InitPlayerConstants()
    DIR_NONE = 0
    DIR_UP = 1
    DIR_DOWN = 2
    DIR_LEFT = 3
    DIR_RIGHT = 4

    COLOR_PACMAN = 14  ' Yellow
End Sub

' ============================================================================
' Pacman Class - The player character
' ============================================================================
Class Pacman
    Dim x As Integer
    Dim y As Integer
    Dim direction As Integer
    Dim nextDirection As Integer
    Dim lives As Integer
    Dim score As Integer
    Dim startX As Integer
    Dim startY As Integer
    Dim alive As Integer
    Dim mouthOpen As Integer
    Dim animTimer As Integer

    Sub Init(sx As Integer, sy As Integer)
        startX = sx
        startY = sy
        x = sx
        y = sy
        direction = DIR_NONE
        nextDirection = DIR_NONE
        lives = 3
        score = 0
        alive = 1
        mouthOpen = 1
        animTimer = 0
    End Sub

    Function GetX() As Integer
        GetX = x
    End Function

    Function GetY() As Integer
        GetY = y
    End Function

    Function GetScore() As Integer
        GetScore = score
    End Function

    Function GetLives() As Integer
        GetLives = lives
    End Function

    Function IsAlive() As Integer
        IsAlive = alive
    End Function

    Sub AddScore(points As Integer)
        score = score + points
    End Sub

    Sub SetDirection(dir As Integer)
        nextDirection = dir
    End Sub

    ' Try to move in current direction, with wall collision
    Function Move(maze As Maze) As Integer
        Dim newX As Integer
        Dim newY As Integer
        Dim canMove As Integer
        Dim moved As Integer

        moved = 0

        ' First try the queued direction
        If nextDirection <> DIR_NONE Then
            newX = x
            newY = y

            If nextDirection = DIR_UP Then
                newY = y - 1
            ElseIf nextDirection = DIR_DOWN Then
                newY = y + 1
            ElseIf nextDirection = DIR_LEFT Then
                newX = x - 1
            ElseIf nextDirection = DIR_RIGHT Then
                newX = x + 1
            End If

            ' Handle tunnel wrap
            If newX < 0 Then
                newX = maze.GetWidth() - 1
            ElseIf newX >= maze.GetWidth() Then
                newX = 0
            End If

            If maze.IsWalkable(newX, newY) = 1 Then
                direction = nextDirection
            End If
        End If

        ' Now try to move in current direction
        If direction <> DIR_NONE Then
            newX = x
            newY = y

            If direction = DIR_UP Then
                newY = y - 1
            ElseIf direction = DIR_DOWN Then
                newY = y + 1
            ElseIf direction = DIR_LEFT Then
                newX = x - 1
            ElseIf direction = DIR_RIGHT Then
                newX = x + 1
            End If

            ' Handle tunnel wrap
            If newX < 0 Then
                newX = maze.GetWidth() - 1
            ElseIf newX >= maze.GetWidth() Then
                newX = 0
            End If

            If maze.IsWalkable(newX, newY) = 1 Then
                x = newX
                y = newY
                moved = 1

                ' Animate mouth
                animTimer = animTimer + 1
                If animTimer >= 2 Then
                    animTimer = 0
                    If mouthOpen = 1 Then
                        mouthOpen = 0
                    Else
                        mouthOpen = 1
                    End If
                End If
            End If
        End If

        Move = moved
    End Function

    Sub Die()
        lives = lives - 1
        If lives <= 0 Then
            alive = 0
        Else
            ' Reset position
            x = startX
            y = startY
            direction = DIR_NONE
            nextDirection = DIR_NONE
        End If
    End Sub

    Sub Reset()
        x = startX
        y = startY
        direction = DIR_NONE
        nextDirection = DIR_NONE
        mouthOpen = 1
        animTimer = 0
    End Sub

    ' Draw Pacman at current position
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer
        Dim ch As String

        screenX = MAZE_LEFT + x
        screenY = MAZE_TOP + y

        Viper.Terminal.SetPosition(screenY, screenX)
        Viper.Terminal.SetColor(COLOR_PACMAN, 0)

        ' Choose character based on direction and mouth state
        If mouthOpen = 1 Then
            If direction = DIR_RIGHT Then
                ch = ">"
            ElseIf direction = DIR_LEFT Then
                ch = "<"
            ElseIf direction = DIR_UP Then
                ch = "^"
            ElseIf direction = DIR_DOWN Then
                ch = "v"
            Else
                ch = "C"
            End If
        Else
            ch = "O"  ' Closed mouth
        End If

        PRINT ch;

        Viper.Terminal.SetColor(7, 0)
    End Sub

    ' Clear Pacman from current position (redraw cell)
    Sub Clear(maze As Maze)
        maze.DrawCell(x, y)
    End Sub
End Class
