' board.bas - Game board management
' Handles the playfield, collision detection, line clearing

Class Board
    Dim Grid(22, 12) As Integer  ' 20 rows + 2 buffer, 10 cols + 2 walls
    Dim GridColor(22, 12) As Integer
    Dim BoardWidth As Integer
    Dim BoardHeight As Integer
    Dim LinesCleared As Integer

    Sub New()
        Me.BoardWidth = 10
        Me.BoardHeight = 20
        Me.LinesCleared = 0
        
        ' Initialize grid
        Dim i As Integer, j As Integer
        For i = 0 To 21
            For j = 0 To 11
                Me.Grid(i, j) = 0
                Me.GridColor(i, j) = 0
            Next j
        Next i
        
        ' Set walls (column 0 and 11)
        For i = 0 To 21
            Me.Grid(i, 0) = 9   ' Left wall
            Me.Grid(i, 11) = 9  ' Right wall
        Next i

        ' Set floor (row 20) - playable area is rows 0-19
        For j = 0 To 11
            Me.Grid(20, j) = 9  ' Floor
        Next j
    End Sub

    Function CanPlace(p As Piece) As Integer
        ' Check if piece can be placed at current position
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer
        
        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i
                    
                    ' Check bounds
                    If bx < 1 Or bx > 10 Then Return 0
                    If by < 0 Or by > 20 Then Return 0
                    
                    ' Check collision with placed blocks
                    If Me.Grid(by, bx) <> 0 Then Return 0
                End If
            Next j
        Next i
        
        Return 1  ' Can place
    End Function

    Sub PlacePiece(p As Piece)
        ' Place piece permanently on board
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer
        
        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i
                    Me.Grid(by, bx) = 1
                    Me.GridColor(by, bx) = p.PieceColor
                End If
            Next j
        Next i
    End Sub

    Function CheckLines() As Integer
        ' Check for completed lines and return count
        Dim count As Integer
        Dim row As Integer, col As Integer
        Dim full As Integer
        
        count = 0
        For row = 0 To 19
            full = 1
            For col = 1 To 10
                If Me.Grid(row, col) = 0 Then
                    full = 0
                End If
            Next col
            
            If full = 1 Then
                count = count + 1
                Me.ClearLine(row)
            End If
        Next row
        
        Me.LinesCleared = Me.LinesCleared + count
        Return count
    End Function

    Sub ClearLine(row As Integer)
        ' Clear a line and move everything down
        Dim r As Integer, c As Integer
        
        ' Move all rows above down by one
        For r = row To 1 Step -1
            For c = 1 To 10
                Me.Grid(r, c) = Me.Grid(r - 1, c)
                Me.GridColor(r, c) = Me.GridColor(r - 1, c)
            Next c
        Next r
        
        ' Clear top row
        For c = 1 To 10
            Me.Grid(0, c) = 0
            Me.GridColor(0, c) = 0
        Next c
    End Sub

    Sub DrawBoard()
        ' Draw the board with ANSI colors and enhanced visuals
        Dim row As Integer, col As Integer

        ' Top border with gradient effect
        LOCATE 1, 1
        COLOR 14, 0  ' Yellow border
        Print "╔════════════════════╗"

        For row = 0 To 19
            LOCATE row + 2, 1

            ' Border color gradient (yellow to blue)
            If row < 5 Then
                COLOR 14, 0  ' Yellow
            ElseIf row < 10 Then
                COLOR 11, 0  ' Cyan
            ElseIf row < 15 Then
                COLOR 10, 0  ' Green
            Else
                COLOR 9, 0   ' Blue
            End If
            Print "║";

            ' Draw cells
            For col = 1 To 10
                If Me.Grid(row, col) = 0 Then
                    ' Empty cell
                    COLOR 0, 0
                    Print "  ";
                Else
                    ' Colored blocks
                    COLOR Me.GridColor(row, col), 0
                    Print "██";
                End If
            Next col

            ' Right border (matching gradient)
            If row < 5 Then
                COLOR 14, 0
            ElseIf row < 10 Then
                COLOR 11, 0
            ElseIf row < 15 Then
                COLOR 10, 0
            Else
                COLOR 9, 0
            End If
            Print "║"
        Next row

        ' Bottom border
        LOCATE 22, 1
        COLOR 9, 0  ' Blue bottom
        Print "╚════════════════════╝"

        COLOR 7, 0  ' Reset
    End Sub

    Sub DrawPiece(p As Piece)
        ' Draw piece at current position (not permanently placed)
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer
        Dim screenRow As Integer, screenCol As Integer
        
        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i
                    
                    If by >= 0 And by < 20 And bx >= 1 And bx <= 10 Then
                        screenRow = by + 2
                        screenCol = (bx - 1) * 2 + 2
                        LOCATE screenRow, screenCol
                        COLOR p.PieceColor, 0
                        Print "██";
                        COLOR 7, 0
                    End If
                End If
            Next j
        Next i
    End Sub
End Class
