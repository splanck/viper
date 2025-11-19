' Test 02: Tetromino shapes with arrays in classes
' Testing: arrays as class fields, array access in methods

Class Tetromino
    Dim Shape(4, 4) As Integer  ' 4x4 grid for piece
    Dim PieceColor As Integer
    Dim PieceSize As Integer

    Sub New(c As Integer)
        Me.PieceColor = c
        Me.PieceSize = 4
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = 0
            Next j
        Next i
    End Sub

    Sub SetBlock(row As Integer, col As Integer, val As Integer)
        Me.Shape(row, col) = val
    End Sub

    Function GetBlock(row As Integer, col As Integer) As Integer
        Return Me.Shape(row, col)
    End Function

    Sub Display()
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                If Me.Shape(i, j) = 1 Then
                    Print "█";
                Else
                    Print ".";
                End If
            Next j
            Print
        Next i
    End Sub
End Class

' Create an I-piece
Dim piece As Tetromino
piece = New Tetromino(1)

' Set up I-shape (vertical)
piece.SetBlock(0, 1, 1)
piece.SetBlock(1, 1, 1)
piece.SetBlock(2, 1, 1)
piece.SetBlock(3, 1, 1)

Print "I-Piece (PieceColor: "; piece.PieceColor; "):"
piece.Display()

Print
Print "Testing GetBlock: "; piece.GetBlock(0, 1)
