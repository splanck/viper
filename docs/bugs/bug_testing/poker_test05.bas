REM Poker Game - Test 05: Hand evaluation (Part 1 - Pairs and sorting)
REM Testing complex algorithms and array sorting

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

Class Hand
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

    Sub SortByValue()
        REM Bubble sort by card value
        DIM i AS Integer
        DIM j AS Integer
        DIM temp AS Card

        FOR i = 0 TO ME.count - 1
            FOR j = 0 TO ME.count - i - 2
                IF ME.cards(j).value > ME.cards(j + 1).value THEN
                    LET temp = ME.cards(j)
                    LET ME.cards(j) = ME.cards(j + 1)
                    LET ME.cards(j + 1) = temp
                END IF
            NEXT j
        NEXT i
    End Sub

    Function CountValue(val AS Integer) AS Integer
        DIM i AS Integer
        DIM cnt AS Integer
        LET cnt = 0
        FOR i = 0 TO ME.count - 1
            IF ME.cards(i).value = val THEN
                LET cnt = cnt + 1
            END IF
        NEXT i
        LET CountValue = cnt
    End Function

    Function HasPair() AS Boolean
        DIM i AS Integer
        LET HasPair = FALSE
        FOR i = 0 TO ME.count - 1
            IF ME.CountValue(ME.cards(i).value) = 2 THEN
                LET HasPair = TRUE
            END IF
        NEXT i
    End Function

    Function HasThreeOfKind() AS Boolean
        DIM i AS Integer
        LET HasThreeOfKind = FALSE
        FOR i = 0 TO ME.count - 1
            IF ME.CountValue(ME.cards(i).value) = 3 THEN
                LET HasThreeOfKind = TRUE
            END IF
        NEXT i
    End Function

    Function HasFourOfKind() AS Boolean
        DIM i AS Integer
        LET HasFourOfKind = FALSE
        FOR i = 0 TO ME.count - 1
            IF ME.CountValue(ME.cards(i).value) = 4 THEN
                LET HasFourOfKind = TRUE
            END IF
        NEXT i
    End Function

    Sub ShowHand()
        DIM i AS Integer
        FOR i = 0 TO ME.count - 1
            PRINT "  "; ME.cards(i).ToString()
        NEXT i
    End Sub
End Class

REM Test hand evaluation
DIM hand AS Hand
LET hand = NEW Hand()

REM Create a pair of 8s
hand.AddCard(NEW Card("Hearts", "8", 8))
hand.AddCard(NEW Card("Spades", "8", 8))
hand.AddCard(NEW Card("Clubs", "3", 3))
hand.AddCard(NEW Card("Diamonds", "King", 13))
hand.AddCard(NEW Card("Hearts", "Ace", 14))

PRINT "Test hand:"
hand.ShowHand()
hand.SortByValue()
PRINT "After sorting:"
hand.ShowHand()
PRINT "Has pair: "; hand.HasPair()
PRINT "Has three of kind: "; hand.HasThreeOfKind()
