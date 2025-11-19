' Test 11: Verify floor collision at correct position

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(0)  ' I-piece
p.PosX = 4

' Test floor collision
p.PosY = 18
Print "Can place at row 18? "; b.CanPlace(p)

p.PosY = 19  
Print "Can place at row 19? "; b.CanPlace(p)

p.PosY = 20
Print "Can place at row 20 (floor)? "; b.CanPlace(p)

' Place piece at bottom visible row
p.PosY = 16
b.PlacePiece(p)

CLS
b.DrawBoard()
LOCATE 24, 1
Print "I-piece should be visible at bottom (rows 16-19)"
