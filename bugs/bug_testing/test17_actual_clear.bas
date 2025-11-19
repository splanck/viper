' Test 17: Actually clear a line and verify floor survives

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

Print "Test: Clear a complete line, then verify floor"
Print ""

' Fill row 15 completely (middle of board)
Print "Filling row 15 completely..."
Dim c As Integer
For c = 1 To 10
    b.Grid(15, c) = 1
    b.GridColor(15, c) = 3
Next c

Print "Checking lines before clear..."
Dim linesBefore As Integer
linesBefore = b.CheckLines()
Print "Lines cleared: "; linesBefore
Print ""

' Verify floor
Print "Floor check after line clear:"
Dim floorOk As Integer
floorOk = 1
For c = 0 To 11
    If b.Grid(20, c) <> 9 Then
        Print "  Column "; c; " = "; b.Grid(20, c); " (should be 9!)"
        floorOk = 0
    End If
Next c

If floorOk = 1 Then
    Print "  ✓ Floor still intact (all = 9)"
Else
    Print "  ✗ Floor DESTROYED!"
End If
Print ""

' Test collision at bottom
Print "Testing bottom collision after clear:"
Dim p As Piece
p = New Piece(0)
p.PosX = 4
p.PosY = 16
Print "  Can place at Y=16? "; b.CanPlace(p)
p.PosY = 17
Print "  Can place at Y=17? "; b.CanPlace(p)
Print ""

' Draw result
CLS
b.DrawBoard()

LOCATE 24, 1
If floorOk = 1 Then
    Print "✓✓✓ SUCCESS: Floor survives line clearing! BUG-112 FIXED ✓✓✓"
Else
    Print "✗✗✗ FAILURE: Floor destroyed by line clear! BUG-112 NOT FIXED ✗✗✗"
End If
