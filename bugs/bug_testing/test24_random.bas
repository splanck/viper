' Test 24: Test random number generation

Print "Testing random number generation..."
Print ""

Randomize

Print "Generating 20 random piece types (0-6):"
Print ""

Dim i As Integer
Dim pieceType As Integer
Dim counts(7) As Integer

' Initialize counts
For i = 0 To 6
    counts(i) = 0
Next i

' Generate 20 random pieces
For i = 1 To 20
    pieceType = Int(Rnd() * 7)
    Print "  "; i; ": Type "; pieceType
    counts(pieceType) = counts(pieceType) + 1
Next i

Print ""
Print "Distribution:"
For i = 0 To 6
    Print "  Type "; i; ": "; counts(i); " times"
Next i
Print ""

If counts(0) = 20 Then
    Print "✗ BUG: Always getting type 0!"
ElseIf counts(0) > 15 Then
    Print "⚠ WARNING: Type 0 appears too often"
Else
    Print "✓ Random generation working"
End If
