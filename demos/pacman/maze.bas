' ============================================================================
' PACMAN - Maze Module
' ============================================================================
' Contains maze data, rendering, and collision detection
' ============================================================================

' Maze cell types
Dim CELL_WALL As Integer
Dim CELL_EMPTY As Integer
Dim CELL_DOT As Integer
Dim CELL_POWER As Integer
Dim CELL_GHOST_DOOR As Integer

' Screen positioning
Dim MAZE_TOP As Integer
Dim MAZE_LEFT As Integer

' Colors
Dim COLOR_WALL As Integer
Dim COLOR_DOT As Integer
Dim COLOR_POWER As Integer
Dim COLOR_EMPTY As Integer

Sub InitMazeConstants()
    CELL_WALL = 0
    CELL_EMPTY = 1
    CELL_DOT = 2
    CELL_POWER = 3
    CELL_GHOST_DOOR = 4

    MAZE_TOP = 2
    MAZE_LEFT = 5

    COLOR_WALL = 12      ' Red
    COLOR_DOT = 15       ' White
    COLOR_POWER = 14     ' Yellow
    COLOR_EMPTY = 0      ' Black
End Sub

' ============================================================================
' Maze Class - Holds the game board
' ============================================================================
Class Maze
    ' 28x31 classic Pacman maze (stored as 1D array)
    Dim cells(868) As Integer   ' 28 * 31 = 868
    Dim width As Integer
    Dim height As Integer
    Dim totalDots As Integer
    Dim dotsRemaining As Integer

    Sub Init()
        width = 28
        height = 31
        totalDots = 0
        dotsRemaining = 0
        Me.LoadClassicMaze()
    End Sub

    ' Load the classic Pacman maze layout
    Sub LoadClassicMaze()
        Dim row As Integer
        Dim col As Integer
        Dim mazeStr As String
        Dim ch As String
        Dim cellType As Integer

        ' Classic Pacman maze layout (28 wide x 31 tall)
        ' # = wall, . = dot, o = power pellet, - = ghost door, space = empty

        ' Row 0
        Me.SetRow(0, "############################")
        ' Row 1
        Me.SetRow(1, "#............##............#")
        ' Row 2
        Me.SetRow(2, "#.####.#####.##.#####.####.#")
        ' Row 3
        Me.SetRow(3, "#o####.#####.##.#####.####o#")
        ' Row 4
        Me.SetRow(4, "#.####.#####.##.#####.####.#")
        ' Row 5
        Me.SetRow(5, "#..........................#")
        ' Row 6
        Me.SetRow(6, "#.####.##.########.##.####.#")
        ' Row 7
        Me.SetRow(7, "#.####.##.########.##.####.#")
        ' Row 8
        Me.SetRow(8, "#......##....##....##......#")
        ' Row 9
        Me.SetRow(9, "######.##### ## #####.######")
        ' Row 10
        Me.SetRow(10, "     #.##### ## #####.#     ")
        ' Row 11
        Me.SetRow(11, "     #.##          ##.#     ")
        ' Row 12
        Me.SetRow(12, "     #.## ###--### ##.#     ")
        ' Row 13
        Me.SetRow(13, "######.## #      # ##.######")
        ' Row 14
        Me.SetRow(14, "      .   #      #   .      ")
        ' Row 15
        Me.SetRow(15, "######.## #      # ##.######")
        ' Row 16
        Me.SetRow(16, "     #.## ######## ##.#     ")
        ' Row 17
        Me.SetRow(17, "     #.##          ##.#     ")
        ' Row 18
        Me.SetRow(18, "     #.## ######## ##.#     ")
        ' Row 19
        Me.SetRow(19, "######.## ######## ##.######")
        ' Row 20
        Me.SetRow(20, "#............##............#")
        ' Row 21
        Me.SetRow(21, "#.####.#####.##.#####.####.#")
        ' Row 22
        Me.SetRow(22, "#.####.#####.##.#####.####.#")
        ' Row 23
        Me.SetRow(23, "#o..##................##..o#")
        ' Row 24
        Me.SetRow(24, "###.##.##.########.##.##.###")
        ' Row 25
        Me.SetRow(25, "###.##.##.########.##.##.###")
        ' Row 26
        Me.SetRow(26, "#......##....##....##......#")
        ' Row 27
        Me.SetRow(27, "#.##########.##.##########.#")
        ' Row 28
        Me.SetRow(28, "#.##########.##.##########.#")
        ' Row 29
        Me.SetRow(29, "#..........................#")
        ' Row 30
        Me.SetRow(30, "############################")
    End Sub

    ' Parse a row string and set cells
    Sub SetRow(row As Integer, rowStr As String)
        Dim col As Integer
        Dim ch As String
        Dim cellType As Integer

        For col = 0 To 27
            If col < Len(rowStr) Then
                ch = Mid$(rowStr, col + 1, 1)
            Else
                ch = "#"
            End If

            If ch = "#" Then
                cellType = CELL_WALL
            ElseIf ch = "." Then
                cellType = CELL_DOT
                totalDots = totalDots + 1
                dotsRemaining = dotsRemaining + 1
            ElseIf ch = "o" Then
                cellType = CELL_POWER
                totalDots = totalDots + 1
                dotsRemaining = dotsRemaining + 1
            ElseIf ch = "-" Then
                cellType = CELL_GHOST_DOOR
            Else
                cellType = CELL_EMPTY
            End If

            cells(row * width + col) = cellType
        Next col
    End Sub

    Function GetCell(x As Integer, y As Integer) As Integer
        If x < 0 Or x >= width Or y < 0 Or y >= height Then
            GetCell = CELL_WALL
        Else
            GetCell = cells(y * width + x)
        End If
    End Function

    Sub SetCell(x As Integer, y As Integer, value As Integer)
        If x >= 0 And x < width And y >= 0 And y < height Then
            cells(y * width + x) = value
        End If
    End Sub

    Function IsWalkable(x As Integer, y As Integer) As Integer
        Dim cell As Integer
        cell = Me.GetCell(x, y)
        If cell = CELL_WALL Then
            IsWalkable = 0
        ElseIf cell = CELL_GHOST_DOOR Then
            IsWalkable = 0  ' Pacman can't go through ghost door
        Else
            IsWalkable = 1
        End If
    End Function

    Function EatDot(x As Integer, y As Integer) As Integer
        Dim cell As Integer
        cell = Me.GetCell(x, y)

        If cell = CELL_DOT Then
            Me.SetCell(x, y, CELL_EMPTY)
            dotsRemaining = dotsRemaining - 1
            EatDot = 10  ' Points for dot
        ElseIf cell = CELL_POWER Then
            Me.SetCell(x, y, CELL_EMPTY)
            dotsRemaining = dotsRemaining - 1
            EatDot = 50  ' Points for power pellet
        Else
            EatDot = 0
        End If
    End Function

    Function GetDotsRemaining() As Integer
        GetDotsRemaining = dotsRemaining
    End Function

    Function GetWidth() As Integer
        GetWidth = width
    End Function

    Function GetHeight() As Integer
        GetHeight = height
    End Function

    ' Draw the entire maze
    Sub Draw()
        Dim x As Integer
        Dim y As Integer
        Dim cell As Integer
        Dim screenX As Integer
        Dim screenY As Integer

        For y = 0 To height - 1
            For x = 0 To width - 1
                cell = Me.GetCell(x, y)
                screenX = MAZE_LEFT + x
                screenY = MAZE_TOP + y

                Viper.Terminal.SetPosition(screenY, screenX)

                If cell = CELL_WALL Then
                    Viper.Terminal.SetColor(COLOR_WALL, 0)
                    PRINT "#";
                ElseIf cell = CELL_DOT Then
                    Viper.Terminal.SetColor(COLOR_DOT, 0)
                    PRINT ".";
                ElseIf cell = CELL_POWER Then
                    Viper.Terminal.SetColor(COLOR_POWER, 0)
                    PRINT "o";
                ElseIf cell = CELL_GHOST_DOOR Then
                    Viper.Terminal.SetColor(COLOR_WALL, 0)
                    PRINT "-";
                Else
                    Viper.Terminal.SetColor(COLOR_EMPTY, 0)
                    PRINT " ";
                End If
            Next x
        Next y

        Viper.Terminal.SetColor(7, 0)
    End Sub

    ' Draw a single cell (for updates)
    Sub DrawCell(x As Integer, y As Integer)
        Dim cell As Integer
        Dim screenX As Integer
        Dim screenY As Integer

        cell = Me.GetCell(x, y)
        screenX = MAZE_LEFT + x
        screenY = MAZE_TOP + y

        Viper.Terminal.SetPosition(screenY, screenX)

        If cell = CELL_WALL Then
            Viper.Terminal.SetColor(COLOR_WALL, 0)
            PRINT "#";
        ElseIf cell = CELL_DOT Then
            Viper.Terminal.SetColor(COLOR_DOT, 0)
            PRINT ".";
        ElseIf cell = CELL_POWER Then
            Viper.Terminal.SetColor(COLOR_POWER, 0)
            PRINT "o";
        ElseIf cell = CELL_GHOST_DOOR Then
            Viper.Terminal.SetColor(COLOR_WALL, 0)
            PRINT "-";
        Else
            Viper.Terminal.SetColor(COLOR_EMPTY, 0)
            PRINT " ";
        End If

        Viper.Terminal.SetColor(7, 0)
    End Sub
End Class
