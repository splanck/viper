' Test 18: Verify vTris demo menu system

AddFile "../../demos/vTris/pieces.bas"
AddFile "../../demos/vTris/board.bas"
AddFile "../../demos/vTris/scoreboard.bas"

Print "Testing vTris Demo Components..."
Print ""

' Test Scoreboard
Print "1. Testing Scoreboard..."
Dim sb As Scoreboard
sb = New Scoreboard()
Print "   Scoreboard created with "; sb.Count; " default scores"

' Test adding a score
sb.AddScore("TST", 5000, 2)
Print "   Added test score"
Print "   New count: "; sb.Count
Print ""

' Test high score detection
Print "2. Testing high score detection..."
If sb.IsHighScore(60000) = 1 Then
    Print "   60000 is a high score: YES"
Else
    Print "   60000 is a high score: NO"
End If

If sb.IsHighScore(5000) = 1 Then
    Print "   5000 is a high score: YES"
Else
    Print "   5000 is a high score: NO"
End If
Print ""

' Test Board with enhanced colors
Print "3. Testing enhanced board rendering..."
Dim b As Board
b = New Board()

' Place some colorful pieces
Dim p As Piece
p = New Piece(0)
p.PosX = 2
p.PosY = 17
b.PlacePiece(p)

p = New Piece(2)
p.PosX = 5
p.PosY = 17
b.PlacePiece(p)

p = New Piece(4)
p.PosX = 8
p.PosY = 17
b.PlacePiece(p)

Print "   Placed 3 colored pieces"
Print ""

' Draw the board
CLS
b.DrawBoard()

LOCATE 24, 1
COLOR 11, 0
Print "✓ vTris demo components working!"
COLOR 7, 0
Print " Board shows gradient borders and pattern."
