' Test 26: Verify clean board rendering with no artifacts

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"

Dim b As Board
b = New Board()

' Add some pieces for visual testing
Dim p As Piece
p = New Piece(0)
p.PosX = 2
p.PosY = 18
b.PlacePiece(p)

p = New Piece(2)
p.PosX = 5
p.PosY = 17
b.PlacePiece(p)

p = New Piece(4)
p.PosX = 8
p.PosY = 16
b.PlacePiece(p)

CLS
b.DrawBoard()

' Draw test UI
LOCATE 2, 26
COLOR 14, 0
Print "╔══════════════════╗"
LOCATE 3, 26
Print "║ ";
COLOR 15, 0
Print "TEST UI";
COLOR 14, 0
Print "         ║"
LOCATE 4, 26
Print "╚══════════════════╝"

LOCATE 24, 1
COLOR 11, 0
Print "Check: No border artifacts (║) should appear between"
LOCATE 25, 1
Print "       the game board and the UI panel."
COLOR 7, 0
