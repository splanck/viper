REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Working Deck v5             ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Workaround for BUG-074: Define Deck BEFORE Card

CLASS Deck
    SUB Init()
        REM Deck represents all 52 cards (calculated from index)
    END SUB

    FUNCTION GetCardSuit(index AS INTEGER) AS INTEGER
        REM Calculate suit from index (0-51): 0=Spades, 1=Hearts, 2=Diamonds, 3=Clubs
        RETURN index \ 13
    END FUNCTION

    FUNCTION GetCardRank(index AS INTEGER) AS INTEGER
        REM Calculate rank from index (0-51): 2-14 (14=Ace)
        DIM rankOffset AS INTEGER
        rankOffset = index - ((index \ 13) * 13)
        RETURN rankOffset + 2
    END FUNCTION

    SUB ShowCard(index AS INTEGER)
        REM Create temporary card to display
        DIM c AS Card
        c = NEW Card()

        DIM s AS INTEGER
        DIM r AS INTEGER
        s = ME.GetCardSuit(index)
        r = ME.GetCardRank(index)

        c.Init(s, r)
        c.Display()
    END SUB

    SUB ShowAllCards()
        DIM i AS INTEGER
        FOR i = 0 TO 51
            ME.ShowCard(i)
            PRINT " ";
            REM New line after each suit (13 cards)
            IF (i + 1) - ((i + 1) \ 13) * 13 = 0 THEN
                PRINT
            END IF
        NEXT i
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
        REM Show card with color based on suit
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

REM ═══ TEST DECK CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         WORKING DECK CLASS TEST (v5)                   ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Creating deck..."
DIM deck AS Deck
deck = NEW Deck()
deck.Init()
PRINT

PRINT "Showing all 52 cards:"
PRINT "────────────────────────────────────────────────────────"
deck.ShowAllCards()
PRINT "────────────────────────────────────────────────────────"
PRINT

PRINT "Testing specific cards:"
PRINT "Card 0 (2♠): ";
deck.ShowCard(0)
PRINT
PRINT "Card 12 (A♠): ";
deck.ShowCard(12)
PRINT
PRINT "Card 25 (K♥): ";
deck.ShowCard(25)
PRINT
PRINT "Card 51 (A♣): ";
deck.ShowCard(51)
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  DECK CLASS TEST COMPLETE!                             ║"
PRINT "╚════════════════════════════════════════════════════════╝"
