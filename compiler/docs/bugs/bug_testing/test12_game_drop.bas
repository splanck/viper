' Test 12: Simulate game drop behavior for all piece types

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

' Test each piece type dropping to bottom
Dim pieceType As Integer
For pieceType = 0 To 6
    Print "Testing piece type "; pieceType

    Dim p As Piece
    p = New Piece(pieceType)
    p.PosX = 4
    p.PosY = 0

    ' Simulate dropping until can't place
    Dim lastGoodY As Integer
    lastGoodY = 0

    While b.CanPlace(p) = 1
        lastGoodY = p.PosY
        p.PosY = p.PosY + 1
    Wend

    ' Revert to last good position (like game does)
    p.PosY = lastGoodY

    Print "  Last valid Y position: "; p.PosY
    Print "  Piece occupies rows: "; p.PosY; " to "; (p.PosY + 3)

    ' Place it
    b.PlacePiece(p)
Next pieceType

' Draw final board
CLS
b.DrawBoard()

LOCATE 24, 1
Print "All piece types placed at bottom. Check if visible."
