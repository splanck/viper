REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Player Class v6             ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Workaround for BUG-074: Define classes in reverse dependency order
REM Workaround for BUG-067: Cannot use array fields in classes

CLASS Player
    REM Player hand storage (workaround for BUG-067: no array fields)
    card1Suit AS INTEGER
    card1Rank AS INTEGER
    card2Suit AS INTEGER
    card2Rank AS INTEGER
    chips AS INTEGER
    name AS STRING

    SUB Init(playerName AS STRING, startingChips AS INTEGER)
        ME.name = playerName
        ME.chips = startingChips
        ME.card1Suit = -1
        ME.card1Rank = -1
        ME.card2Suit = -1
        ME.card2Rank = -1
    END SUB

    SUB GiveCard(cardNum AS INTEGER, suit AS INTEGER, rank AS INTEGER)
        IF cardNum = 1 THEN
            ME.card1Suit = suit
            ME.card1Rank = rank
        ELSEIF cardNum = 2 THEN
            ME.card2Suit = suit
            ME.card2Rank = rank
        END IF
    END SUB

    SUB ShowHand()
        PRINT ME.name; " ("; ME.chips; " chips): ";

        REM Show first card
        IF ME.card1Rank >= 0 THEN
            DIM c1 AS Card
            c1 = NEW Card()
            c1.Init(ME.card1Suit, ME.card1Rank)
            c1.Display()
            PRINT " ";
        END IF

        REM Show second card
        IF ME.card2Rank >= 0 THEN
            DIM c2 AS Card
            c2 = NEW Card()
            c2.Init(ME.card2Suit, ME.card2Rank)
            c2.Display()
        END IF
    END SUB

    SUB ClearHand()
        ME.card1Suit = -1
        ME.card1Rank = -1
        ME.card2Suit = -1
        ME.card2Rank = -1
    END SUB
END CLASS

CLASS Deck
    currentCard AS INTEGER

    SUB Init()
        ME.currentCard = 0
    END SUB

    FUNCTION GetCardSuit(index AS INTEGER) AS INTEGER
        RETURN index \ 13
    END FUNCTION

    FUNCTION GetCardRank(index AS INTEGER) AS INTEGER
        DIM rankOffset AS INTEGER
        rankOffset = index - ((index \ 13) * 13)
        RETURN rankOffset + 2
    END FUNCTION

    FUNCTION DealCardSuit() AS INTEGER
        DIM suit AS INTEGER
        suit = ME.GetCardSuit(ME.currentCard)
        ME.currentCard = ME.currentCard + 1
        RETURN suit
    END FUNCTION

    FUNCTION DealCardRank() AS INTEGER
        DIM rank AS INTEGER
        rank = ME.GetCardRank(ME.currentCard - 1)
        RETURN rank
    END FUNCTION

    SUB Reset()
        ME.currentCard = 0
    END SUB
END CLASS

CLASS Card
    suit AS INTEGER
    rank AS INTEGER

    SUB Init(cardSuit AS INTEGER, cardRank AS INTEGER)
        ME.suit = cardSuit
        ME.rank = cardRank
    END SUB

    FUNCTION GetSuitName() AS STRING
        IF ME.suit = 0 THEN
            RETURN "♠"
        ELSEIF ME.suit = 1 THEN
            RETURN "♥"
        ELSEIF ME.suit = 2 THEN
            RETURN "♦"
        ELSEIF ME.suit = 3 THEN
            RETURN "♣"
        END IF
        RETURN "?"
    END FUNCTION

    FUNCTION GetRankName() AS STRING
        IF ME.rank = 14 THEN
            RETURN "A"
        ELSEIF ME.rank = 13 THEN
            RETURN "K"
        ELSEIF ME.rank = 12 THEN
            RETURN "Q"
        ELSEIF ME.rank = 11 THEN
            RETURN "J"
        ELSEIF ME.rank = 10 THEN
            RETURN "10"
        ELSEIF ME.rank = 9 THEN
            RETURN "9"
        ELSEIF ME.rank = 8 THEN
            RETURN "8"
        ELSEIF ME.rank = 7 THEN
            RETURN "7"
        ELSEIF ME.rank = 6 THEN
            RETURN "6"
        ELSEIF ME.rank = 5 THEN
            RETURN "5"
        ELSEIF ME.rank = 4 THEN
            RETURN "4"
        ELSEIF ME.rank = 3 THEN
            RETURN "3"
        ELSEIF ME.rank = 2 THEN
            RETURN "2"
        END IF
        RETURN "?"
    END FUNCTION

    SUB Display()
        IF ME.suit = 1 OR ME.suit = 2 THEN
            COLOR 12, 0
        ELSE
            COLOR 15, 0
        END IF

        DIM suitName AS STRING
        DIM rankName AS STRING
        suitName = ME.GetSuitName()
        rankName = ME.GetRankName()

        PRINT rankName; suitName;
        COLOR 15, 0
    END SUB
END CLASS

REM ═══ TEST PLAYER CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         PLAYER CLASS TEST (v6)                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

REM Create deck
DIM deck AS Deck
deck = NEW Deck()
deck.Init()

REM Create players
DIM player1 AS Player
DIM player2 AS Player

player1 = NEW Player()
player2 = NEW Player()

player1.Init("Alice", 1000)
player2.Init("Bob", 1000)

PRINT "Dealing hands..."
PRINT

REM Deal to player 1
DIM s AS INTEGER
DIM r AS INTEGER
s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(1, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(2, s, r)

REM Deal to player 2
s = deck.DealCardSuit()
r = deck.DealCardRank()
player2.GiveCard(1, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player2.GiveCard(2, s, r)

REM Show hands
player1.ShowHand()
PRINT
player2.ShowHand()
PRINT
PRINT

PRINT "Clearing hands and dealing again..."
player1.ClearHand()
player2.ClearHand()

deck.Reset()

s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(1, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(2, s, r)

PRINT
player1.ShowHand()
PRINT

PRINT
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  PLAYER CLASS TEST COMPLETE!                           ║"
PRINT "╚════════════════════════════════════════════════════════╝"
