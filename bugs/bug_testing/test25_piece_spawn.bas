' Test 25: Test piece spawning sequence (simulating game)

AddFile "../../demos/vTris/pieces.bas"

Print "Simulating game piece spawning..."
Print ""

Randomize

' Simulate InitGame
Dim CurrentPiece As Piece
Dim NextPiece As Piece
Dim pieceType As Integer

Print "=== InitGame ==="
pieceType = Int(Rnd() * 7)
CurrentPiece = New Piece(pieceType)
Print "CurrentPiece type: "; CurrentPiece.PieceType

pieceType = Int(Rnd() * 7)
NextPiece = New Piece(pieceType)
Print "NextPiece type: "; NextPiece.PieceType
Print ""

' Simulate 10 SpawnPiece calls
Dim i As Integer
For i = 1 To 10
    Print "=== Spawn "; i; " ==="

    ' This is what SpawnPiece does
    pieceType = NextPiece.PieceType
    CurrentPiece = New Piece(pieceType)
    Print "CurrentPiece type: "; CurrentPiece.PieceType

    pieceType = Int(Rnd() * 7)
    NextPiece = New Piece(pieceType)
    Print "NextPiece type: "; NextPiece.PieceType
    Print ""
Next i

Print "If all pieces are the same type, there's a bug!"
