' Test 16: Automated gameplay test with line clearing

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

Print "Automated vTris Gameplay Test"
Print "Testing BUG-112 fix: Floor should survive line clears"
Print ""

' Manually create a near-full line at row 19 (bottom visible row)
Dim c As Integer
For c = 1 To 7
    b.Grid(19, c) = 1
    b.GridColor(19, c) = 6
Next c

' Drop an I-piece to complete the line
Dim p As Piece
p = New Piece(0)  ' I-piece (vertical)
p.PosX = 8        ' Will fill columns 8-8 (plus one block wide)
p.PosY = 16       ' Bottom position

Print "Placing I-piece at bottom right to complete row 19..."
b.PlacePiece(p)

' Check lines
Print "Checking for completed lines..."
Dim cleared As Integer
cleared = b.CheckLines()
Print "Lines cleared: "; cleared
Print ""

' Verify floor is still intact
Print "Floor integrity check:"
Dim floorOk As Integer
floorOk = 1
For c = 0 To 11
    If b.Grid(20, c) <> 9 Then
        Print "  ERROR: Grid(20, "; c; ") = "; b.Grid(20, c); " (expected 9)"
        floorOk = 0
    End If
Next c

If floorOk = 1 Then
    Print "  ✓ Floor intact! All cells still = 9"
End If
Print ""

' Try placing another piece at bottom
Dim p2 As Piece
p2 = New Piece(0)
p2.PosX = 2
p2.PosY = 17

If b.CanPlace(p2) = 0 Then
    Print "✓ Collision detection still works (can't place at Y=17)"
    p2.PosY = 16
    If b.CanPlace(p2) = 1 Then
        Print "✓ Can still place pieces at Y=16 (bottom row)"
    Else
        Print "✗ ERROR: Can't place at Y=16!"
    End If
Else
    Print "✗ ERROR: Could place piece through floor!"
End If
Print ""

' Draw final state
CLS
b.DrawBoard()

LOCATE 24, 1
If floorOk = 1 Then
    Print "✓ BUG-112 FIX VERIFIED: Game playable after line clear!"
Else
    Print "✗ BUG-112 STILL BROKEN!"
End If
