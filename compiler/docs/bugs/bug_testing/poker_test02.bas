REM Poker Game - Test 02: Card with values + Deck class
REM Testing arrays of objects

Class Card
    Public suit AS String
    Public rank AS String
    Public value AS Integer

    Sub New(s AS String, r AS String, v AS Integer)
        LET ME.suit = s
        LET ME.rank = r
        LET ME.value = v
    End Sub

    Function ToString() AS String
        LET ToString = ME.rank + " of " + ME.suit
    End Function
End Class

Class Deck
    DIM cards(52) AS Card
    Public count AS Integer

    Sub New()
        LET ME.count = 0
        DIM suits(4) AS String
        LET suits(0) = "Hearts"
        LET suits(1) = "Diamonds"
        LET suits(2) = "Clubs"
        LET suits(3) = "Spades"

        DIM ranks(13) AS String
        LET ranks(0) = "2"
        LET ranks(1) = "3"
        LET ranks(2) = "4"
        LET ranks(3) = "5"
        LET ranks(4) = "6"
        LET ranks(5) = "7"
        LET ranks(6) = "8"
        LET ranks(7) = "9"
        LET ranks(8) = "10"
        LET ranks(9) = "Jack"
        LET ranks(10) = "Queen"
        LET ranks(11) = "King"
        LET ranks(12) = "Ace"

        DIM i AS Integer
        DIM j AS Integer
        FOR i = 0 TO 3
            FOR j = 0 TO 12
                LET ME.cards(ME.count) = NEW Card(suits(i), ranks(j), j + 2)
                LET ME.count = ME.count + 1
            NEXT j
        NEXT i
    End Sub

    Function GetCard(index AS Integer) AS Card
        LET GetCard = ME.cards(index)
    End Function
End Class

REM Test creating a full deck
DIM deck AS Deck
LET deck = NEW Deck()
PRINT "Deck has "; deck.count; " cards"
PRINT "First card: "; deck.GetCard(0).ToString()
PRINT "Last card: "; deck.GetCard(51).ToString()
