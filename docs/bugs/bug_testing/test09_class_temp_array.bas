' Test 09: Try class-level temp array instead of local

Class TestArray
    Dim A(4, 4) As Integer
    Dim Temp(4, 4) As Integer  ' Class-level instead of local
    
    Sub New()
        Me.A(0, 0) = 5
        Me.A(1, 2) = 7
        Me.A(3, 1) = 9
    End Sub
    
    Sub TestCopy()
        Dim i As Integer, j As Integer
        
        ' Copy using class-level temp
        For i = 0 To 3
            For j = 0 To 3
                Me.Temp(i, j) = Me.A(i, j)
            Next j
        Next i
        
        Print "After copy to Me.Temp:"
        Print "Temp(0,0) = "; Me.Temp(0, 0)
        Print "Temp(1,2) = "; Me.Temp(1, 2)
        Print "Temp(3,1) = "; Me.Temp(3, 1)
    End Sub
End Class

Dim t As TestArray
t = New TestArray()
t.TestCopy()
