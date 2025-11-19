' shapes_lib.bas - Shared shape definitions
' This file will be included via AddFile

Class ShapeData
    Dim ShapeType As Integer
    Dim ShapeName As String

    Sub New(t As Integer, n As String)
        Me.ShapeType = t
        Me.ShapeName = n
    End Sub

    Function GetInfo() As String
        Return "Shape " + Str$(Me.ShapeType) + ": " + Me.ShapeName
    End Function
End Class
