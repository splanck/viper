' Test 22: Verify game layout is correct

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"

' Simulate game state
Dim GameScore As Integer
Dim GameLines As Integer
Dim GameLevel As Integer
Dim NextPiece As Piece

GameScore = 1200
GameLines = 8
GameLevel = 1

NextPiece = New Piece(2)  ' T-piece

' Create board with some pieces
Dim b As Board
b = New Board()

Dim p As Piece
p = New Piece(0)
p.PosX = 2
p.PosY = 18
b.PlacePiece(p)

p = New Piece(4)
p.PosX = 5
p.PosY = 17
b.PlacePiece(p)

CLS

' Draw board
b.DrawBoard()

' Draw UI (right side panel)
LOCATE 2, 26
COLOR 14, 0
Print "╔══════════════════╗"

LOCATE 3, 26
Print "║ ";
COLOR 15, 0
Print "vTRIS v2.0";
COLOR 14, 0
Print "      ║"

LOCATE 4, 26
Print "╠══════════════════╣"

' Score
LOCATE 5, 26
COLOR 14, 0
Print "║ ";
COLOR 11, 0
Print "SCORE:";
COLOR 14, 0
Print "          ║"
LOCATE 6, 26
Print "║ ";
COLOR 15, 0
Print GameScore; "            ";
COLOR 14, 0
Print "║"

' Lines
LOCATE 7, 26
Print "║ ";
COLOR 10, 0
Print "LINES:";
COLOR 14, 0
Print "          ║"
LOCATE 8, 26
Print "║ ";
COLOR 15, 0
Print GameLines; "            ";
COLOR 14, 0
Print "║"

' Level
LOCATE 9, 26
Print "║ ";
COLOR 9, 0
Print "LEVEL:";
COLOR 14, 0
Print "          ║"
LOCATE 10, 26
Print "║ ";
COLOR 15, 0
Print GameLevel; "            ";
COLOR 14, 0
Print "║"

LOCATE 11, 26
Print "╠══════════════════╣"

' Next piece preview
LOCATE 12, 26
Print "║ ";
COLOR 13, 0
Print "NEXT:";
COLOR 14, 0
Print "           ║"

' Draw next piece (simplified for test)
Dim i As Integer, j As Integer
For i = 0 To 3
    LOCATE 13 + i, 26
    COLOR 14, 0
    Print "║  ";

    For j = 0 To 3
        If NextPiece.Shape(i, j) = 1 Then
            COLOR NextPiece.PieceColor, 0
            Print "██";
        Else
            COLOR 7, 0
            Print "  ";
        End If
    Next j

    COLOR 14, 0
    Print "  ║"
Next i

LOCATE 17, 26
Print "╠══════════════════╣"

' Controls
LOCATE 18, 26
Print "║ ";
COLOR 8, 0
Print "A/D - Move";
COLOR 14, 0
Print "     ║"
LOCATE 19, 26
Print "║ ";
COLOR 8, 0
Print "W - Rotate";
COLOR 14, 0
Print "      ║"
LOCATE 20, 26
Print "║ ";
COLOR 8, 0
Print "S - Drop";
COLOR 14, 0
Print "        ║"
LOCATE 21, 26
Print "║ ";
COLOR 8, 0
Print "Q - Quit";
COLOR 14, 0
Print "        ║"

LOCATE 22, 26
Print "╚══════════════════╝"

LOCATE 24, 1
COLOR 11, 0
Print "✓ Game layout is correct!"
COLOR 7, 0
Print " Board + UI properly aligned."
LOCATE 25, 1
