' Bug test: Method calls on array-copied objects inside FOR loop
Class Ghost
    Dim X As Integer

    Sub New()
        Me.X = 10
    End Sub

    Sub ResetGhost()
        Me.X = 0
    End Sub
End Class

Dim Ghosts(4) As Ghost
Dim i As Integer
For i = 0 To 3
    Ghosts(i) = New Ghost()
Next i

' Original failing pattern from Pacman
Sub ResetPositions()
    Dim i As Integer
    Dim g As Ghost
    For i = 0 To 3
        g = Ghosts(i)
        g.ResetGhost()
        Ghosts(i) = g
    Next i
End Sub

ResetPositions()
Print "After reset: "; Ghosts(0).X
