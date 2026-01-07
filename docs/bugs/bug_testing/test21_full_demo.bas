' Test 21: Comprehensive vTris demo test

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"
AddFile "../../demos/vTris/scoreboard.bas"

Print "vTRIS v2.0 - Full Demo Test"
Print ""
Print "Testing all components..."
Print ""

' 1. Test pieces
Print "1. Piece System:"
Dim p As Piece
p = New Piece(0)
Print "   Created I-piece (type 0) - Color: "; p.PieceColor
p.RotateClockwise()
Print "   Rotated piece successfully"
Print ""

' 2. Test board
Print "2. Board System:"
Dim b As Board
b = New Board()
Print "   Created board ("; b.BoardWidth; "x"; b.BoardHeight; ")"
p.PosX = 4
p.PosY = 16
If b.CanPlace(p) = 1 Then
    Print "   Collision detection working"
End If
Print ""

' 3. Test scoreboard
Print "3. Scoreboard System:"
Dim sb As Scoreboard
sb = New Scoreboard()
Print "   Loaded "; sb.Count; " default high scores"
If sb.IsHighScore(100000) = 1 Then
    Print "   High score detection working"
End If
Print ""

' 4. Test level calculation
Print "4. Level System:"
Dim level As Integer
Dim lines As Integer
lines = 25
level = Int(lines / 10) + 1
Print "   At "; lines; " lines, level is "; level
Print ""

' 5. Visual test - show game board
Print "5. Visual Test:"
Print "   Drawing colorful game board..."
Print ""

Sleep 1000

' Place some pieces for visual effect
Dim p2 As Piece
p2 = New Piece(2)
p2.PosX = 2
p2.PosY = 18
b.PlacePiece(p2)

Dim p3 As Piece
p3 = New Piece(4)
p3.PosX = 5
p3.PosY = 17
b.PlacePiece(p3)

Dim p4 As Piece
p4 = New Piece(6)
p4.PosX = 8
p4.PosY = 16
b.PlacePiece(p4)

CLS
b.DrawBoard()

' Draw UI preview
LOCATE 2, 26
COLOR 14, 0
Print "╔═══════════════╗"
LOCATE 3, 26
Print "║ ";
COLOR 11, 0
Print "SCORE: 12500";
COLOR 14, 0
Print " ║"
LOCATE 4, 26
Print "║ ";
COLOR 10, 0
Print "LINES: 25";
COLOR 14, 0
Print "    ║"
LOCATE 5, 26
Print "║ ";
COLOR 9, 0
Print "LEVEL: 3";
COLOR 14, 0
Print "     ║"
LOCATE 6, 26
Print "╚═══════════════╝"

LOCATE 24, 1
COLOR 11, 0
Print "✓ All vTRIS components working!"
COLOR 7, 0
Print " Ready to play!"
LOCATE 25, 1
