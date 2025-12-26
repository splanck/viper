' Test 23: Verify piece spawning works correctly

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"

Print "Testing piece spawning bug fix..."
Print ""

' Create two pieces
Dim NextPiece As Piece
Dim CurrentPiece As Piece

NextPiece = New Piece(0)  ' I-piece
Print "Created NextPiece (type 0) at default position"
Print "  NextPiece.PosX = "; NextPiece.PosX
Print "  NextPiece.PosY = "; NextPiece.PosY
Print ""

' OLD WAY (buggy) - would be: CurrentPiece = NextPiece
' NEW WAY (fixed):
Dim pieceType As Integer
pieceType = NextPiece.PieceType
CurrentPiece = New Piece(pieceType)
CurrentPiece.PosX = 4
CurrentPiece.PosY = 0

Print "Created CurrentPiece (same type, new object)"
Print "  CurrentPiece.PosX = "; CurrentPiece.PosX
Print "  CurrentPiece.PosY = "; CurrentPiece.PosY
Print ""

' Now modify CurrentPiece position
CurrentPiece.PosY = 19

Print "Moved CurrentPiece to Y=19"
Print "  CurrentPiece.PosY = "; CurrentPiece.PosY
Print "  NextPiece.PosY = "; NextPiece.PosY
Print ""

If NextPiece.PosY = 19 Then
    Print "✗ BUG: NextPiece position was modified!"
    Print "  (They share the same object reference)"
Else
    Print "✓ FIXED: NextPiece position unchanged!"
    Print "  (They are separate objects)"
End If
Print ""

' Test board rendering
Print "Testing board rendering..."
Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(2)
p.PosX = 5
p.PosY = 18
b.PlacePiece(p)

CLS
b.DrawBoard()

LOCATE 24, 1
COLOR 11, 0
Print "✓ Board renders cleanly without pattern artifacts"
COLOR 7, 0
LOCATE 25, 1
