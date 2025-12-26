REM Poker Game - Test 03: Deck shuffle using Fisher-Yates
REM Testing RND and array manipulation

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

    Sub Shuffle()
        DIM i AS Integer
        DIM j AS Integer
        DIM temp AS Card

        FOR i = 51 TO 1 STEP -1
            LET j = INT(RND() * (i + 1))
            REM Swap cards(i) and cards(j)
            LET temp = ME.cards(i)
            LET ME.cards(i) = ME.cards(j)
            LET ME.cards(j) = temp
        NEXT i
    End Sub

    Function GetCard(index AS Integer) AS Card
        LET GetCard = ME.cards(index)
    End Function
End Class

REM Test shuffle
RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
PRINT "Before shuffle:"
PRINT "  First 5: "; deck.GetCard(0).ToString(); ", "; deck.GetCard(1).ToString(); ", "; deck.GetCard(2).ToString()
deck.Shuffle()
PRINT "After shuffle:"
PRINT "  First 5: "; deck.GetCard(0).ToString(); ", "; deck.GetCard(1).ToString(); ", "; deck.GetCard(2).ToString()
