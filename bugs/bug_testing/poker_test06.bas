REM Poker Game - Test 06: ANSI colors for card suits
REM Testing ANSI escape codes and string concatenation

REM ANSI Color codes
DIM ANSI_RED AS String
DIM ANSI_BLACK AS String
DIM ANSI_RESET AS String
DIM ANSI_BOLD AS String

LET ANSI_RED = CHR$(27) + "[31m"
LET ANSI_BLACK = CHR$(27) + "[30m"
LET ANSI_RESET = CHR$(27) + "[0m"
LET ANSI_BOLD = CHR$(27) + "[1m"

Class Card
    Public suit AS String
    Public rank AS String
    Public value AS Integer
    Public suitSymbol AS String

    Sub New(s AS String, r AS String, v AS Integer)
        LET ME.suit = s
        LET ME.rank = r
        LET ME.value = v

        REM Set suit symbols
        IF s = "Hearts" THEN
            LET ME.suitSymbol = CHR$(3)
        ELSEIF s = "Diamonds" THEN
            LET ME.suitSymbol = CHR$(4)
        ELSEIF s = "Clubs" THEN
            LET ME.suitSymbol = CHR$(5)
        ELSEIF s = "Spades" THEN
            LET ME.suitSymbol = CHR$(6)
        END IF
    End Sub

    Function GetColor() AS String
        IF ME.suit = "Hearts" OR ME.suit = "Diamonds" THEN
            LET GetColor = ANSI_RED
        ELSE
            LET GetColor = ANSI_BLACK
        END IF
    End Function

    Function ToString() AS String
        LET ToString = ME.GetColor() + ME.rank + ME.suitSymbol + ANSI_RESET
    End Function

    Function ToStringFull() AS String
        LET ToStringFull = ME.GetColor() + ME.rank + " of " + ME.suit + ANSI_RESET
    End Function
End Class

REM Test colored output
DIM c1 AS Card
DIM c2 AS Card
DIM c3 AS Card
DIM c4 AS Card

LET c1 = NEW Card("Hearts", "A", 14)
LET c2 = NEW Card("Spades", "K", 13)
LET c3 = NEW Card("Diamonds", "Q", 12)
LET c4 = NEW Card("Clubs", "J", 11)

PRINT "Testing colored card display:"
PRINT c1.ToStringFull()
PRINT c2.ToStringFull()
PRINT c3.ToStringFull()
PRINT c4.ToStringFull()

PRINT
PRINT "Short form: "; c1.ToString(); " "; c2.ToString(); " "; c3.ToString(); " "; c4.ToString()
