' Test 01: Basic OOP - Point class
' Testing: class definition, constructor, fields, methods

Class Point
    Dim X As Integer
    Dim Y As Integer

    Sub New(x As Integer, y As Integer)
        Me.X = x
        Me.Y = y
    End Sub

    Sub Move(dx As Integer, dy As Integer)
        Me.X = Me.X + dx
        Me.Y = Me.Y + dy
    End Sub

    Function ToString() As String
        Return "(" + Str$(Me.X) + "," + Str$(Me.Y) + ")"
    End Function
End Class

' Test the Point class
Dim p As Point
p = New Point(5, 10)
Print "Initial point: "; p.ToString()

p.Move(3, -2)
Print "After Move(3, -2): "; p.ToString()

Print "X="; p.X; " Y="; p.Y
