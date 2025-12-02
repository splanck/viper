' card.bas - Chance and Community Chest cards for Monopoly

CLASS Card
    DIM cardText AS STRING
    DIM cardAction AS INTEGER
    DIM cardValue AS INTEGER
    DIM cardDest AS INTEGER       ' Destination position for MOVE_TO cards
    DIM cardValue2 AS INTEGER     ' Second value (e.g., hotel cost for repairs)

    SUB Init(txt AS STRING, act AS INTEGER, val AS INTEGER, dest AS INTEGER, val2 AS INTEGER)
        cardText = txt
        cardAction = act
        cardValue = val
        cardDest = dest
        cardValue2 = val2
    END SUB

    FUNCTION GetText() AS STRING
        GetText = cardText
    END FUNCTION

    FUNCTION GetAction() AS INTEGER
        GetAction = cardAction
    END FUNCTION

    FUNCTION GetValue() AS INTEGER
        GetValue = cardValue
    END FUNCTION

    FUNCTION GetDestination() AS INTEGER
        GetDestination = cardDest
    END FUNCTION

    FUNCTION GetValue2() AS INTEGER
        GetValue2 = cardValue2
    END FUNCTION
END CLASS

' Card deck manager
CLASS CardDeck
    DIM deckType AS INTEGER       ' 0 = Chance, 1 = Community Chest
    DIM cardCount AS INTEGER
    DIM currentIndex AS INTEGER

    ' Store card data as parallel arrays (workaround for List limitations)
    DIM cardTexts(15) AS STRING
    DIM cardActions(15) AS INTEGER
    DIM cardValues(15) AS INTEGER
    DIM cardDests(15) AS INTEGER
    DIM cardValues2(15) AS INTEGER
    DIM shuffleOrder(15) AS INTEGER

    SUB InitChance()
        deckType = 0
        cardCount = 16
        currentIndex = 0

        ' Chance cards
        cardTexts(0) = "Advance to GO (Collect $200)"
        cardActions(0) = CARD_MOVE_TO
        cardValues(0) = 200
        cardDests(0) = 0
        cardValues2(0) = 0

        cardTexts(1) = "Advance to Illinois Ave"
        cardActions(1) = CARD_MOVE_TO
        cardValues(1) = 0
        cardDests(1) = 24
        cardValues2(1) = 0

        cardTexts(2) = "Advance to St. Charles Place"
        cardActions(2) = CARD_MOVE_TO
        cardValues(2) = 0
        cardDests(2) = 11
        cardValues2(2) = 0

        cardTexts(3) = "Advance to nearest Utility"
        cardActions(3) = CARD_NEAREST_UTIL
        cardValues(3) = 0
        cardDests(3) = 0
        cardValues2(3) = 0

        cardTexts(4) = "Advance to nearest Railroad"
        cardActions(4) = CARD_NEAREST_RR
        cardValues(4) = 0
        cardDests(4) = 0
        cardValues2(4) = 0

        cardTexts(5) = "Advance to nearest Railroad"
        cardActions(5) = CARD_NEAREST_RR
        cardValues(5) = 0
        cardDests(5) = 0
        cardValues2(5) = 0

        cardTexts(6) = "Bank pays you dividend of $50"
        cardActions(6) = CARD_COLLECT
        cardValues(6) = 50
        cardDests(6) = 0
        cardValues2(6) = 0

        cardTexts(7) = "Get Out of Jail Free"
        cardActions(7) = CARD_JAIL_FREE
        cardValues(7) = 0
        cardDests(7) = 0
        cardValues2(7) = 0

        cardTexts(8) = "Go Back 3 Spaces"
        cardActions(8) = CARD_MOVE_BACK
        cardValues(8) = 3
        cardDests(8) = 0
        cardValues2(8) = 0

        cardTexts(9) = "Go to Jail"
        cardActions(9) = CARD_JAIL
        cardValues(9) = 0
        cardDests(9) = 0
        cardValues2(9) = 0

        cardTexts(10) = "Repairs: $25/house, $100/hotel"
        cardActions(10) = CARD_REPAIRS
        cardValues(10) = 25
        cardDests(10) = 0
        cardValues2(10) = 100

        cardTexts(11) = "Pay poor tax of $15"
        cardActions(11) = CARD_PAY
        cardValues(11) = 15
        cardDests(11) = 0
        cardValues2(11) = 0

        cardTexts(12) = "Take trip to Reading Railroad"
        cardActions(12) = CARD_MOVE_TO
        cardValues(12) = 0
        cardDests(12) = 5
        cardValues2(12) = 0

        cardTexts(13) = "Take a walk on the Boardwalk"
        cardActions(13) = CARD_MOVE_TO
        cardValues(13) = 0
        cardDests(13) = 39
        cardValues2(13) = 0

        cardTexts(14) = "Chairman of board: pay $50 each"
        cardActions(14) = CARD_PAY_EACH
        cardValues(14) = 50
        cardDests(14) = 0
        cardValues2(14) = 0

        cardTexts(15) = "Building loan matures: $150"
        cardActions(15) = CARD_COLLECT
        cardValues(15) = 150
        cardDests(15) = 0
        cardValues2(15) = 0

        Me.InitShuffle()
    END SUB

    SUB InitCommunityChest()
        deckType = 1
        cardCount = 16
        currentIndex = 0

        ' Community Chest cards
        cardTexts(0) = "Advance to GO (Collect $200)"
        cardActions(0) = CARD_MOVE_TO
        cardValues(0) = 200
        cardDests(0) = 0
        cardValues2(0) = 0

        cardTexts(1) = "Bank error in your favor: $200"
        cardActions(1) = CARD_COLLECT
        cardValues(1) = 200
        cardDests(1) = 0
        cardValues2(1) = 0

        cardTexts(2) = "Doctor's fee: pay $50"
        cardActions(2) = CARD_PAY
        cardValues(2) = 50
        cardDests(2) = 0
        cardValues2(2) = 0

        cardTexts(3) = "From sale of stock: $50"
        cardActions(3) = CARD_COLLECT
        cardValues(3) = 50
        cardDests(3) = 0
        cardValues2(3) = 0

        cardTexts(4) = "Get Out of Jail Free"
        cardActions(4) = CARD_JAIL_FREE
        cardValues(4) = 0
        cardDests(4) = 0
        cardValues2(4) = 0

        cardTexts(5) = "Go to Jail"
        cardActions(5) = CARD_JAIL
        cardValues(5) = 0
        cardDests(5) = 0
        cardValues2(5) = 0

        cardTexts(6) = "Grand Opera: collect $50 each"
        cardActions(6) = CARD_COLLECT_EACH
        cardValues(6) = 50
        cardDests(6) = 0
        cardValues2(6) = 0

        cardTexts(7) = "Holiday fund matures: $100"
        cardActions(7) = CARD_COLLECT
        cardValues(7) = 100
        cardDests(7) = 0
        cardValues2(7) = 0

        cardTexts(8) = "Income tax refund: $20"
        cardActions(8) = CARD_COLLECT
        cardValues(8) = 20
        cardDests(8) = 0
        cardValues2(8) = 0

        cardTexts(9) = "Birthday: collect $10 each"
        cardActions(9) = CARD_COLLECT_EACH
        cardValues(9) = 10
        cardDests(9) = 0
        cardValues2(9) = 0

        cardTexts(10) = "Life insurance matures: $100"
        cardActions(10) = CARD_COLLECT
        cardValues(10) = 100
        cardDests(10) = 0
        cardValues2(10) = 0

        cardTexts(11) = "Hospital fees: pay $100"
        cardActions(11) = CARD_PAY
        cardValues(11) = 100
        cardDests(11) = 0
        cardValues2(11) = 0

        cardTexts(12) = "School fees: pay $50"
        cardActions(12) = CARD_PAY
        cardValues(12) = 50
        cardDests(12) = 0
        cardValues2(12) = 0

        cardTexts(13) = "Consultancy fee: $25"
        cardActions(13) = CARD_COLLECT
        cardValues(13) = 25
        cardDests(13) = 0
        cardValues2(13) = 0

        cardTexts(14) = "Repairs: $40/house, $115/hotel"
        cardActions(14) = CARD_REPAIRS
        cardValues(14) = 40
        cardDests(14) = 0
        cardValues2(14) = 115

        cardTexts(15) = "You inherit $100"
        cardActions(15) = CARD_COLLECT
        cardValues(15) = 100
        cardDests(15) = 0
        cardValues2(15) = 0

        Me.InitShuffle()
    END SUB

    SUB InitShuffle()
        DIM i AS INTEGER
        FOR i = 0 TO 15
            shuffleOrder(i) = i
        NEXT i
    END SUB

    SUB Shuffle()
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM tmp AS INTEGER
        DIM randVal AS SINGLE

        FOR i = 15 TO 1 STEP -1
            randVal = RND()
            j = INT(randVal * (i + 1))
            tmp = shuffleOrder(i)
            shuffleOrder(i) = shuffleOrder(j)
            shuffleOrder(j) = tmp
        NEXT i
        currentIndex = 0
    END SUB

    FUNCTION DrawCard() AS INTEGER
        DIM cardIdx AS INTEGER
        cardIdx = shuffleOrder(currentIndex)
        currentIndex = currentIndex + 1
        IF currentIndex >= cardCount THEN
            currentIndex = 0
        END IF
        DrawCard = cardIdx
    END FUNCTION

    FUNCTION GetCardText(idx AS INTEGER) AS STRING
        GetCardText = cardTexts(idx)
    END FUNCTION

    FUNCTION GetCardAction(idx AS INTEGER) AS INTEGER
        GetCardAction = cardActions(idx)
    END FUNCTION

    FUNCTION GetCardValue(idx AS INTEGER) AS INTEGER
        GetCardValue = cardValues(idx)
    END FUNCTION

    FUNCTION GetCardDest(idx AS INTEGER) AS INTEGER
        GetCardDest = cardDests(idx)
    END FUNCTION

    FUNCTION GetCardValue2(idx AS INTEGER) AS INTEGER
        GetCardValue2 = cardValues2(idx)
    END FUNCTION
END CLASS

