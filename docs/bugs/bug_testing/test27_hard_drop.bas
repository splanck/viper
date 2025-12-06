' Test 27: Test spacebar hard drop feature

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"

Print "Testing hard drop feature..."
Print ""

Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(0)  ' I-piece
p.PosX = 4
p.PosY = 0

Print "Initial piece position: Y = "; p.PosY

' Simulate hard drop - move down while can place
While b.CanPlace(p) = 1
    p.MoveDown()
Wend
p.PosY = p.PosY - 1

Print "After hard drop: Y = "; p.PosY
Print ""

If p.PosY >= 16 Then
    Print "✓ Hard drop working - piece at bottom (Y >= 16)"
Else
    Print "✗ BUG: Piece not at expected bottom position"
End If
