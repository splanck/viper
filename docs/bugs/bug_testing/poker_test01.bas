REM Poker Game - Test 01: Card Class
REM Testing basic OOP with Card class

Class Card
    Public suit As String
    Public rank As String

    Sub New(s As String, r As String)
        suit = s
        rank = r
    End Sub

    Function ToString() As String
        ToString = rank + " of " + suit
    End Function
End Class

REM Test the Card class
DIM c AS Card
LET c = NEW Card("Hearts", "Ace")
PRINT c.ToString()
