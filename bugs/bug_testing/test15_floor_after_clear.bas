' Test 15: Verify floor survives line clearing (BUG-112)

AddFile "../../examples/vTris/pieces.bas"
AddFile "../../examples/vTris/board.bas"

Dim b As Board
b = New Board()

Print "BUG-112: Floor destroyed when clearing lines"
Print ""
Print "Setting up a line to clear at row 10..."

' Manually fill row 10 completely
Dim c As Integer
For c = 1 To 10
    b.Grid(10, c) = 1
    b.GridColor(10, c) = 7
Next c

Print "Checking for lines..."
Dim linesCleared As Integer
linesCleared = b.CheckLines()
Print "Lines cleared: "; linesCleared
Print ""

' Now test if floor still works
Print "Testing floor after line clear:"
Dim p As Piece
p = New Piece(0)  ' I-piece
p.PosX = 4

' Try placing at bottom
p.PosY = 16
Print "  Can place at Y=16 (rows 16-19)? "; b.CanPlace(p)
p.PosY = 17
Print "  Can place at Y=17 (rows 17-20)? "; b.CanPlace(p)

' Check if floor is still intact
Print ""
Print "Floor integrity check (row 20):"
Dim floorIntact As Integer
floorIntact = 1
For c = 1 To 10
    If b.Grid(20, c) <> 9 Then
        floorIntact = 0
        Print "  Column "; c; " = "; b.Grid(20, c); " (should be 9!)"
    End If
Next c

If floorIntact = 1 Then
    Print "  Floor is intact! All columns = 9"
    Print ""
    Print "BUG-112: FIXED"
Else
    Print ""
    Print "BUG-112: STILL BROKEN - Floor was destroyed!"
End If
