' Regression: object parameters are borrowed by the callee.  Passing an object
' with an array field through a method must not release the caller's object.

Class Board
    Dim Cells(2) As Integer

    Sub Init()
        Cells(0) = 7
        Cells(1) = 11
    End Sub

    Function ReadFirst() As Integer
        ReadFirst = Cells(0)
    End Function

    Function ReadSecond() As Integer
        ReadSecond = Cells(1)
    End Function
End Class

Class Probe
    Function Touch(board As Board) As Integer
        Touch = board.ReadFirst()
    End Function
End Class

Dim SharedBoard As Board
Dim Runner As Probe

SharedBoard = New Board()
SharedBoard.Init()
Runner = New Probe()

Print Runner.Touch(SharedBoard)
Print SharedBoard.ReadFirst()
Print SharedBoard.ReadSecond()
