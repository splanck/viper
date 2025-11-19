REM poker_card.bas - Card class definition

REM ANSI Color codes
DIM ANSI_RED AS String
DIM ANSI_BLACK AS String
DIM ANSI_RESET AS String

LET ANSI_RED = CHR$(27) + "[31m"
LET ANSI_BLACK = CHR$(27) + "[30m"
LET ANSI_RESET = CHR$(27) + "[0m"

Class Card
    Public suit AS String
    Public rank AS String
    Public value AS Integer

    Sub New(s AS String, r AS String, v AS Integer)
        LET ME.suit = s
        LET ME.rank = r
        LET ME.value = v
    End Sub

    Function GetColor() AS String
        IF ME.suit = "Hearts" OR ME.suit = "Diamonds" THEN
            LET GetColor = ANSI_RED
        ELSE
            LET GetColor = ANSI_BLACK
        END IF
    End Function

    Function ToString() AS String
        LET ToString = ME.GetColor() + ME.rank + " of " + ME.suit + ANSI_RESET
    End Function
End Class
