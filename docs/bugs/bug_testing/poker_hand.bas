REM poker_hand.bas - Hand class with full evaluation

Class Hand
    DIM cards(5) AS Card
    Public count AS Integer
    Public handRank AS Integer
    Public handName AS String

    Sub New()
        LET ME.count = 0
        LET ME.handRank = 0
        LET ME.handName = "Unknown"
    End Sub

    Sub AddCard(c AS Card)
        IF ME.count < 5 THEN
            LET ME.cards(ME.count) = c
            LET ME.count = ME.count + 1
        END IF
    End Sub

    Sub SortByValue()
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

    Function IsFlush() AS Boolean
        DIM i AS Integer
        DIM firstSuit AS String
        LET firstSuit = ME.cards(0).suit
        LET IsFlush = TRUE
        FOR i = 1 TO ME.count - 1
            IF ME.cards(i).suit <> firstSuit THEN
                LET IsFlush = FALSE
            END IF
        NEXT i
    End Function

    Function IsStraight() AS Boolean
        ME.SortByValue()
        DIM i AS Integer
        LET IsStraight = TRUE
        FOR i = 1 TO ME.count - 1
            IF ME.cards(i).value <> ME.cards(i - 1).value + 1 THEN
                LET IsStraight = FALSE
            END IF
        NEXT i
    End Function

    Function CountPairs() AS Integer
        DIM i AS Integer
        DIM j AS Integer
        DIM pairs AS Integer
        DIM alreadyCounted AS Boolean
        LET pairs = 0

        FOR i = 0 TO ME.count - 1
            REM Check if this value is a pair and hasn't been counted yet
            IF ME.CountValue(ME.cards(i).value) = 2 THEN
                REM Check if we already counted this pair
                LET alreadyCounted = FALSE
                FOR j = 0 TO i - 1
                    IF ME.cards(j).value = ME.cards(i).value THEN
                        LET alreadyCounted = TRUE
                    END IF
                NEXT j

                IF NOT alreadyCounted THEN
                    LET pairs = pairs + 1
                END IF
            END IF
        NEXT i
        LET CountPairs = pairs
    End Function

    Function HasThreeOfKind() AS Boolean
        DIM i AS Integer
        DIM found AS Boolean
        LET found = FALSE
        FOR i = 0 TO ME.count - 1
            IF ME.CountValue(ME.cards(i).value) = 3 THEN
                LET found = TRUE
            END IF
        NEXT i
        LET HasThreeOfKind = found
    End Function

    Function HasFourOfKind() AS Boolean
        DIM i AS Integer
        DIM found AS Boolean
        LET found = FALSE
        FOR i = 0 TO ME.count - 1
            IF ME.CountValue(ME.cards(i).value) = 4 THEN
                LET found = TRUE
            END IF
        NEXT i
        LET HasFourOfKind = found
    End Function

    Function IsFullHouse() AS Boolean
        LET IsFullHouse = ME.HasThreeOfKind() AND ME.CountPairs() = 1
    End Function

    Sub Evaluate()
        ME.SortByValue()

        REM Check from highest to lowest rank
        IF ME.IsStraight() AND ME.IsFlush() THEN
            LET ME.handRank = 8
            LET ME.handName = "Straight Flush"
        ELSEIF ME.HasFourOfKind() THEN
            LET ME.handRank = 7
            LET ME.handName = "Four of a Kind"
        ELSEIF ME.IsFullHouse() THEN
            LET ME.handRank = 6
            LET ME.handName = "Full House"
        ELSEIF ME.IsFlush() THEN
            LET ME.handRank = 5
            LET ME.handName = "Flush"
        ELSEIF ME.IsStraight() THEN
            LET ME.handRank = 4
            LET ME.handName = "Straight"
        ELSEIF ME.HasThreeOfKind() THEN
            LET ME.handRank = 3
            LET ME.handName = "Three of a Kind"
        ELSEIF ME.CountPairs() = 2 THEN
            LET ME.handRank = 2
            LET ME.handName = "Two Pair"
        ELSEIF ME.CountPairs() = 1 THEN
            LET ME.handRank = 1
            LET ME.handName = "One Pair"
        ELSE
            LET ME.handRank = 0
            LET ME.handName = "High Card"
        END IF
    End Sub

    Sub ShowHand()
        DIM i AS Integer
        FOR i = 0 TO ME.count - 1
            PRINT "  "; ME.cards(i).ToString()
        NEXT i
        PRINT "  Hand: "; ME.handName
    End Sub
End Class
