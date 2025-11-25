' Bug test: Nested If inside class functions
Class Ghost
    Dim PowerMode As Integer
    Dim FlashFrame As Integer

    Sub New()
        Me.PowerMode = 1
        Me.FlashFrame = 1
    End Sub

    Function GetChar() As String
        If Me.PowerMode < 50 And Me.FlashFrame < 2 Then
            Return "W"
        Else
            Return "M"
        End If
    End Function
End Class

Dim g As Ghost
g = New Ghost()
Print g.GetChar()
