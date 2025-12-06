' Test 04: Arrays of objects
' Testing: object arrays, loops with objects

Class Block
    Dim X As Integer
    Dim Y As Integer
    Dim Active As Integer  ' Boolean as integer

    Sub New(x As Integer, y As Integer)
        Me.X = x
        Me.Y = y
        Me.Active = 1
    End Sub

    Function ToString() As String
        Return "[" + Str$(Me.X) + "," + Str$(Me.Y) + "]"
    End Function
End Class

' Test array of objects
Dim blocks(5) As Block
Dim i As Integer

' Create objects in array
For i = 0 To 4
    blocks(i) = New Block(i, i * 2)
Next i

' Access and print
Print "Block array test:"
For i = 0 To 4
    Print "Block "; i; ": "; blocks(i).ToString(); " Active: "; blocks(i).Active
Next i

' Modify objects
blocks(2).X = 99
blocks(2).Active = 0

Print
Print "After modification:"
Print "Block 2: "; blocks(2).ToString(); " Active: "; blocks(2).Active
