' ============================================================================
' PACMAN Test Iteration 3 - Arrays and Complex OOP
' ============================================================================

' === Maze Cell constants ===
Dim CELL_WALL As Integer
Dim CELL_EMPTY As Integer
Dim CELL_DOT As Integer
Dim CELL_POWER As Integer

CELL_WALL = 0
CELL_EMPTY = 1
CELL_DOT = 2
CELL_POWER = 3

' === Simple Maze Class with array ===
Class Maze
    Dim width As Integer
    Dim height As Integer
    Dim cells(400) As Integer   ' 20x20 max

    Sub Init(w As Integer, h As Integer)
        Dim i As Integer
        width = w
        height = h
        ' Initialize all cells as walls
        For i = 0 To (w * h) - 1
            cells(i) = 0   ' WALL
        Next i
    End Sub

    Function GetCell(x As Integer, y As Integer) As Integer
        If x < 0 Or x >= width Or y < 0 Or y >= height Then
            GetCell = 0  ' Out of bounds = wall
        Else
            GetCell = cells(y * width + x)
        End If
    End Function

    Sub SetCell(x As Integer, y As Integer, value As Integer)
        If x >= 0 And x < width And y >= 0 And y < height Then
            cells(y * width + x) = value
        End If
    End Sub

    Function GetWidth() As Integer
        GetWidth = width
    End Function

    Function GetHeight() As Integer
        GetHeight = height
    End Function
End Class

' === Position Class ===
Class Position
    Dim x As Integer
    Dim y As Integer

    Sub Init(px As Integer, py As Integer)
        x = px
        y = py
    End Sub

    Function GetX() As Integer
        GetX = x
    End Function

    Function GetY() As Integer
        GetY = y
    End Function
End Class

' === Test Code ===
Dim maze As Maze
Dim testsPassed As Integer

testsPassed = 0

PRINT "=== Array and Complex OOP Tests ==="
PRINT ""

' Test 1: Create Maze with array
PRINT "Test 1: NEW Maze()"
maze = New Maze()
PRINT "PASS: Maze created"
testsPassed = testsPassed + 1

' Test 2: Init maze
PRINT "Test 2: maze.Init(5, 5)"
maze.Init(5, 5)
If maze.GetWidth() = 5 And maze.GetHeight() = 5 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 3: Get cell (should be 0/wall after init)
PRINT "Test 3: maze.GetCell(2, 2) = "
PRINT maze.GetCell(2, 2)
If maze.GetCell(2, 2) = 0 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL: Expected 0"
End If

' Test 4: Set cell
PRINT "Test 4: maze.SetCell(2, 2, 2)"
maze.SetCell(2, 2, 2)  ' Set to DOT
If maze.GetCell(2, 2) = 2 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 5: Out of bounds returns wall
PRINT "Test 5: maze.GetCell(-1, 0) = "
PRINT maze.GetCell(-1, 0)
If maze.GetCell(-1, 0) = 0 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL: Expected 0 (wall)"
End If

' Test 6: Array of objects
PRINT "Test 6: Array of Position objects"
Dim positions(3) As Position
Dim i As Integer

For i = 0 To 2
    positions(i) = New Position()
    positions(i).Init(i * 10, i * 20)
Next i

PRINT "positions(0): X="
PRINT positions(0).GetX()
PRINT " Y="
PRINT positions(0).GetY()

PRINT "positions(1): X="
PRINT positions(1).GetX()
PRINT " Y="
PRINT positions(1).GetY()

PRINT "positions(2): X="
PRINT positions(2).GetX()
PRINT " Y="
PRINT positions(2).GetY()

If positions(0).GetX() = 0 And positions(1).GetX() = 10 And positions(2).GetX() = 20 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

PRINT ""
PRINT "Tests passed: "
PRINT testsPassed
PRINT " / 6"
