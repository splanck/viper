REM BUG-109 Test: Nested objects in arrays
REM This should not crash when accessing nested object fields

CLASS Card
    Public suit AS String
    Public rank AS String

    Sub New(s AS String, r AS String)
        LET ME.suit = s
        LET ME.rank = r
    End Sub
END CLASS

CLASS Hand
    DIM cards(5) AS Card
    Public count AS Integer

    Sub New()
        LET ME.count = 0
    End Sub

    Sub AddCard(c AS Card)
        IF ME.count < 5 THEN
            LET ME.cards(ME.count) = c
            LET ME.count = ME.count + 1
        END IF
    End Sub

    Sub ShowCards()
        DIM i AS Integer
        FOR i = 0 TO ME.count - 1
            PRINT ME.cards(i).rank; " of "; ME.cards(i).suit
        NEXT i
    End Sub
END CLASS

CLASS Player
    Public name AS String
    Public hand AS Hand

    Sub New(pname AS String)
        LET ME.name = pname
        LET ME.hand = NEW Hand()
    End Sub
END CLASS

REM Test: Array of players with nested Hand objects
DIM players(2) AS Player
LET players(0) = NEW Player("Alice")
LET players(1) = NEW Player("Bob")

REM Add cards to player hands
DIM c1 AS Card
DIM c2 AS Card
LET c1 = NEW Card("Hearts", "Ace")
LET c2 = NEW Card("Spades", "King")

PRINT "Adding cards to Alice's hand..."
players(0).hand.AddCard(c1)
players(0).hand.AddCard(c2)

PRINT "Alice's cards:"
players(0).hand.ShowCards()

PRINT "Test completed successfully!"
