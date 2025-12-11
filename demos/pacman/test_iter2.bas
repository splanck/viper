' ============================================================================
' PACMAN Test Iteration 2 - OOP Class Testing
' ============================================================================

' === Simple Position Class ===
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

    Sub SetXY(px As Integer, py As Integer)
        x = px
        y = py
    End Sub

    Sub MoveBy(dx As Integer, dy As Integer)
        x = x + dx
        y = y + dy
    End Sub
End Class

' === Test Position Class ===
Dim pos As Position
Dim testsPassed As Integer

testsPassed = 0

PRINT "=== OOP Class Tests ==="
PRINT ""

' Test 1: Create object with NEW
PRINT "Test 1: NEW Position()"
pos = New Position()
PRINT "PASS: Object created"
testsPassed = testsPassed + 1

' Test 2: Call Init method
PRINT "Test 2: pos.Init(10, 20)"
pos.Init(10, 20)
PRINT "PASS: Init called"
testsPassed = testsPassed + 1

' Test 3: Read field via getter
PRINT "Test 3: pos.GetX() = "
PRINT pos.GetX()
If pos.GetX() = 10 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL: Expected 10"
End If

' Test 4: Read Y field
PRINT "Test 4: pos.GetY() = "
PRINT pos.GetY()
If pos.GetY() = 20 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL: Expected 20"
End If

' Test 5: MoveBy
PRINT "Test 5: pos.MoveBy(5, -3)"
pos.MoveBy(5, -3)
PRINT "After MoveBy: X="
PRINT pos.GetX()
PRINT " Y="
PRINT pos.GetY()
If pos.GetX() = 15 And pos.GetY() = 17 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL: Expected X=15, Y=17"
End If

' Test 6: SetXY
PRINT "Test 6: pos.SetXY(100, 200)"
pos.SetXY(100, 200)
If pos.GetX() = 100 And pos.GetY() = 200 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

PRINT ""
PRINT "Tests passed: "
PRINT testsPassed
PRINT " / 6"
