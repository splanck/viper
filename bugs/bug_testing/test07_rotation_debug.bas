' Test 07: Debug rotation issue

Class SimplePiece
    Dim S(4, 4) As Integer
    
    Sub New()
        ' Create a simple L shape
        Me.S(0, 0) = 1
        Me.S(1, 0) = 1
        Me.S(2, 0) = 1
        Me.S(2, 1) = 1
    End Sub
    
    Sub Display()
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                If Me.S(i, j) = 1 Then
                    Print "█";
                Else
                    Print ".";
                End If
            Next j
            Print
        Next i
    End Sub
    
    Sub Rotate()
        Print "Before rotation:"
        Me.Display()
        
        ' Create temp array
        Dim temp(4, 4) As Integer
        Dim i As Integer, j As Integer
        
        ' Copy to temp
        For i = 0 To 3
            For j = 0 To 3
                temp(i, j) = Me.S(i, j)
            Next j
        Next i
        
        Print "Temp array copied, clearing original"
        
        ' Clear original
        For i = 0 To 3
            For j = 0 To 3
                Me.S(i, j) = 0
            Next j
        Next i
        
        Print "Original cleared, rotating"
        
        ' Rotate
        For i = 0 To 3
            For j = 0 To 3
                Me.S(i, j) = temp(3 - j, i)
            Next j
        Next i
        
        Print "After rotation:"
        Me.Display()
    End Sub
End Class

Dim p As SimplePiece
p = New SimplePiece()
Print "Initial shape:"
p.Display()
Print
p.Rotate()
