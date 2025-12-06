' Test 06: Full Piece class with rotations

AddFile "../../examples/vTris/pieces.bas"

' Test creating each piece type individually
Print "Testing all 7 Tetris pieces:"
Print

' I-Piece (0)
Dim p0 As Piece
p0 = New Piece(0)
Print "I-Piece (Color "; p0.PieceColor; "):"
Dim row As Integer, col As Integer
For row = 0 To 3
    For col = 0 To 3
        If p0.Shape(row, col) = 1 Then
            Print "█";
        Else
            Print ".";
        End If
    Next col
    Print
Next row
Print

' O-Piece (1)
Dim p1 As Piece
p1 = New Piece(1)
Print "O-Piece (Color "; p1.PieceColor; "):"
For row = 0 To 3
    For col = 0 To 3
        If p1.Shape(row, col) = 1 Then
            Print "█";
        Else
            Print ".";
        End If
    Next col
    Print
Next row
Print

' T-Piece (2)
Dim p2 As Piece
p2 = New Piece(2)
Print "T-Piece (Color "; p2.PieceColor; "):"
For row = 0 To 3
    For col = 0 To 3
        If p2.Shape(row, col) = 1 Then
            Print "█";
        Else
            Print ".";
        End If
    Next col
    Print
Next row
Print

Print "Testing rotation of T-piece:"
Dim r As Integer
For r = 0 To 3
    Print "Rotation "; r; ":"
    For row = 0 To 3
        For col = 0 To 3
            If p2.Shape(row, col) = 1 Then
                Print "█";
            Else
                Print ".";
            End If
        Next col
        Print
    Next row
    Print
    p2.RotateClockwise()
Next r
