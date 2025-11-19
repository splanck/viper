' Test 13: Exact simulation of game drop sequence

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(0)  ' I-piece
p.PosX = 4
p.PosY = 15  ' Start just above bottom

Print "Simulating falling I-piece..."
Print ""

' Simulate frame-by-frame drops
Dim frame As Integer
Dim locked As Integer
locked = 0

For frame = 1 To 10
    If locked = 1 Then Exit For

    Print "Frame "; frame; ": PosY="; p.PosY

    ' Try to move down (like game does)
    p.MoveDown()
    Print "  After MoveDown: PosY="; p.PosY
    Print "  CanPlace? "; b.CanPlace(p)

    If b.CanPlace(p) = 0 Then
        ' Revert and lock (like game does)
        p.PosY = p.PosY - 1
        Print "  Reverted to: PosY="; p.PosY
        Print "  Locking piece..."

        ' Show which rows the piece occupies
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    Print "    Block at grid row: "; (p.PosY + i)
                End If
            Next j
        Next i

        b.PlacePiece(p)
        locked = 1
    End If
    Print ""
Next frame

' Draw the result
CLS
b.DrawBoard()

LOCATE 24, 1
Print "I-piece locked at bottom. All 4 blocks should be visible above floor."
