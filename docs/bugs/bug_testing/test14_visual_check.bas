' Test 14: Visual verification of bottom rows

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

' Place I-piece at absolute bottom (rows 16-19)
Dim p1 As Piece
p1 = New Piece(0)  ' I-piece (vertical)
p1.PosX = 2
p1.PosY = 16
b.PlacePiece(p1)

' Place another I-piece next to it
Dim p2 As Piece
p2 = New Piece(0)
p2.PosX = 5
p2.PosY = 16
b.PlacePiece(p2)

' Place one more
Dim p3 As Piece
p3 = New Piece(0)
p3.PosX = 8
p3.PosY = 16
b.PlacePiece(p3)

' Draw the board
CLS
b.DrawBoard()

' Add labels to show row numbers
LOCATE 18, 26
Print "← Row 16 (bottom-4)"
LOCATE 19, 26
Print "← Row 17 (bottom-3)"
LOCATE 20, 26
Print "← Row 18 (bottom-2)"
LOCATE 21, 26
Print "← Row 19 (bottom-1)"
LOCATE 22, 26
Print "← Floor border"

LOCATE 24, 1
Print "Three I-pieces at rows 16-19. ALL blocks should be visible."
Print "If any blocks are missing, the floor is too high."
