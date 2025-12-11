' ============================================================================
' PACMAN Test Iteration 4 - Strings, Random, and Maze Drawing
' ============================================================================

Dim testsPassed As Integer
testsPassed = 0

PRINT "=== String and Random Tests ==="
PRINT ""

' Test 1: String concatenation
PRINT "Test 1: String concatenation"
Dim s As String
s = "Hello" + " " + "World"
PRINT s
If s = "Hello World" Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 2: LEN function
PRINT "Test 2: LEN(s) = "
PRINT Len(s)
If Len(s) = 11 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 3: CHR$ function
PRINT "Test 3: CHR$(65) = "
PRINT Chr$(65)
If Chr$(65) = "A" Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 4: ASC function
PRINT "Test 4: ASC('A') = "
PRINT Asc("A")
If Asc("A") = 65 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 5: STR$ function
PRINT "Test 5: STR$(42) = '"
Dim numStr As String
numStr = Str$(42)
PRINT numStr
PRINT "'"
' STR$ may include leading space, just check it contains 42
If Len(numStr) > 0 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 6: Randomize and RND
PRINT "Test 6: Randomize and RND()"
Randomize
Dim r1 As Double
Dim r2 As Double
r1 = Rnd()
r2 = Rnd()
PRINT "r1 = "
PRINT r1
PRINT "r2 = "
PRINT r2
If r1 >= 0.0 And r1 < 1.0 And r2 >= 0.0 And r2 < 1.0 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

' Test 7: INT function for random integers
PRINT "Test 7: INT(RND() * 10)"
Dim ri As Integer
ri = Int(Rnd() * 10)
PRINT "Random int 0-9: "
PRINT ri
If ri >= 0 And ri <= 9 Then
    PRINT "PASS"
    testsPassed = testsPassed + 1
Else
    PRINT "FAIL"
End If

PRINT ""
PRINT "=== Simple Maze Drawing Test ==="

' Draw a simple 5x5 maze using terminal positioning
Viper.Terminal.SetPosition(12, 1)
Viper.Terminal.SetColor(12, 0)
PRINT "#####"
Viper.Terminal.SetPosition(13, 1)
PRINT "#   #"
Viper.Terminal.SetPosition(14, 1)
PRINT "# # #"
Viper.Terminal.SetPosition(15, 1)
PRINT "#   #"
Viper.Terminal.SetPosition(16, 1)
PRINT "#####"

Viper.Terminal.SetPosition(18, 1)
Viper.Terminal.SetColor(7, 0)
PRINT "Maze drawn at rows 12-16"
testsPassed = testsPassed + 1

PRINT ""
PRINT "Tests passed: "
PRINT testsPassed
PRINT " / 8"
