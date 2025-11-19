' Test 08: Debug array access in rotation

Class TestArray
    Dim A(4, 4) As Integer
    
    Sub New()
        Me.A(0, 0) = 5
        Me.A(1, 2) = 7
        Me.A(3, 1) = 9
    End Sub
    
    Sub TestAccess()
        Print "Testing direct access:"
        Print "A(0,0) = "; Me.A(0, 0)
        Print "A(1,2) = "; Me.A(1, 2)
        Print "A(3,1) = "; Me.A(3, 1)
        
        ' Test calculated index
        Dim i As Integer, j As Integer
        i = 0
        j = 0
        Print "A(3-j, i) where i=0,j=0 = A("; 3-j; ","; i; ") = "; Me.A(3-j, i)
        
        i = 1
        j = 2
        Print "A(3-j, i) where i=1,j=2 = A("; 3-j; ","; i; ") = "; Me.A(3-j, i)
    End Sub
    
    Sub TestCopy()
        Dim temp(4, 4) As Integer
        Dim i As Integer, j As Integer
        
        ' Copy array
        For i = 0 To 3
            For j = 0 To 3
                temp(i, j) = Me.A(i, j)
            Next j
        Next i
        
        Print "After copy to temp:"
        Print "temp(0,0) = "; temp(0, 0)
        Print "temp(1,2) = "; temp(1, 2)
        Print "temp(3,1) = "; temp(3, 1)
        
        ' Test reverse lookup
        i = 0
        j = 0
        Print "Reading temp(3-j,i) where i=0,j=0: temp(3,0) = "; temp(3-j, i)
    End Sub
End Class

Dim t As TestArray
t = New TestArray()
t.TestAccess()
Print
t.TestCopy()
