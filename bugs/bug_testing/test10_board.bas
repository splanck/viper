' Test 10: Board class with piece placement

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

' Create board and piece
Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(2)  ' T-piece
p.PosX = 4
p.PosY = 0

' Test collision detection
Print "Can place piece at (4,0)? "; b.CanPlace(p)

' Move piece down and place it
p.PosY = 18
Print "Can place piece at (4,18)? "; b.CanPlace(p)

b.PlacePiece(p)
Print "Piece placed!"

' Check for lines
Print "Lines cleared: "; b.CheckLines()

' Draw the board
CLS
b.DrawBoard()

LOCATE 24, 1
Print "Test complete. Board displayed above."
