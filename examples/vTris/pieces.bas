' pieces.bas - Tetromino piece definitions and rotations
' Defines all 7 standard Tetris pieces

Class Piece
    Dim Shape(4, 4) As Integer
    Dim TempShape(4, 4) As Integer  ' For rotation (workaround for local array bug)
    Dim PieceColor As Integer
    Dim PieceType As Integer  ' 0=I, 1=O, 2=T, 3=S, 4=Z, 5=J, 6=L
    Dim Rotation As Integer   ' 0-3
    Dim PosX As Integer
    Dim PosY As Integer

    Sub New(pieceType As Integer)
        Me.PieceType = pieceType
        Me.Rotation = 0
        Me.PosX = 3  ' Start in middle of board
        Me.PosY = 0
        
        ' Initialize shape grid
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = 0
            Next j
        Next i
        
        ' Set color based on piece type
        If pieceType = 0 Then Me.PieceColor = 6      ' I = Cyan
        If pieceType = 1 Then Me.PieceColor = 3      ' O = Yellow
        If pieceType = 2 Then Me.PieceColor = 5      ' T = Magenta
        If pieceType = 3 Then Me.PieceColor = 2      ' S = Green
        If pieceType = 4 Then Me.PieceColor = 1      ' Z = Red
        If pieceType = 5 Then Me.PieceColor = 4      ' J = Blue
        If pieceType = 6 Then Me.PieceColor = 7      ' L = White
        
        ' Initialize the shape
        Me.InitShape()
    End Sub

    Sub InitShape()
        ' I-piece (type 0)
        If Me.PieceType = 0 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(2, 1) = 1
            Me.Shape(3, 1) = 1
        End If
        
        ' O-piece (type 1)
        If Me.PieceType = 1 Then
            Me.Shape(0, 1) = 1
            Me.Shape(0, 2) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If
        
        ' T-piece (type 2)
        If Me.PieceType = 2 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If
        
        ' S-piece (type 3)
        If Me.PieceType = 3 Then
            Me.Shape(0, 1) = 1
            Me.Shape(0, 2) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(1, 1) = 1
        End If
        
        ' Z-piece (type 4)
        If Me.PieceType = 4 Then
            Me.Shape(0, 0) = 1
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If
        
        ' J-piece (type 5)
        If Me.PieceType = 5 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(2, 0) = 1
            Me.Shape(2, 1) = 1
        End If
        
        ' L-piece (type 6)
        If Me.PieceType = 6 Then
            Me.Shape(0, 0) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(2, 0) = 1
            Me.Shape(2, 1) = 1
        End If
    End Sub

    Sub RotateClockwise()
        ' Use class-level temp array (workaround for local array bug)
        Dim i As Integer, j As Integer

        ' Copy current shape to TempShape
        For i = 0 To 3
            For j = 0 To 3
                Me.TempShape(i, j) = Me.Shape(i, j)
            Next j
        Next i

        ' Clear current shape
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = 0
            Next j
        Next i

        ' Perform rotation: new[i][j] = old[3-j][i]
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = Me.TempShape(3 - j, i)
            Next j
        Next i

        ' Update rotation state
        Me.Rotation = (Me.Rotation + 1) And 3  ' Keep 0-3
    End Sub

    Sub MoveLeft()
        Me.PosX = Me.PosX - 1
    End Sub

    Sub MoveRight()
        Me.PosX = Me.PosX + 1
    End Sub

    Sub MoveDown()
        Me.PosY = Me.PosY + 1
    End Sub
End Class
