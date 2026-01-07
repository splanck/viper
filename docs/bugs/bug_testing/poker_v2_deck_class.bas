REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Deck Class Test             ║
REM ╚════════════════════════════════════════════════════════╝

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
        ELSE
            IF ME.rank = 9 THEN
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

REM ═══ DECK CLASS ═══
REM Testing arrays of objects - this is a known bug area (BUG-067)
REM Workaround: Calculate suit/rank from index, avoid object fields due to BUG-069

CLASS Deck
    REM No fields needed - deck is virtual, cards calculated from index

    SUB Init()
        REM Deck has 52 cards, no initialization needed
    END SUB

    FUNCTION GetCardSuit(index AS INTEGER) AS INTEGER
        REM Calculate suit from index (0-51)
        RETURN index \ 13
    END FUNCTION

    FUNCTION GetCardRank(index AS INTEGER) AS INTEGER
        REM Calculate rank from index (0-51)
        DIM rankOffset AS INTEGER
        rankOffset = index - ((index \ 13) * 13)
        RETURN rankOffset + 2
    END FUNCTION

    SUB ShowCard(index AS INTEGER)
        REM Create temporary card to display
        DIM tempCard AS Card
        tempCard = NEW Card()

        DIM s AS INTEGER
        DIM r AS INTEGER
        s = ME.GetCardSuit(index)
        r = ME.GetCardRank(index)

        tempCard.Init(s, r)
        tempCard.Display()
    END SUB

    SUB ShowAllCards()
        DIM i AS INTEGER
        FOR i = 0 TO 51
            ME.ShowCard(i)
            PRINT " ";
            IF (i + 1) - ((i + 1) \ 13) * 13 = 0 THEN
                PRINT
            END IF
        NEXT i
    END SUB
END CLASS

REM ═══ TEST DECK CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         DECK CLASS STRESS TEST                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Creating deck..."
DIM deck AS Deck
deck = NEW Deck()
deck.Init()

PRINT "Showing all 52 cards:"
PRINT
deck.ShowAllCards()
PRINT

PRINT "Testing specific cards:"
PRINT "Card 0 (2♠): ";
deck.ShowCard(0)
PRINT
PRINT "Card 12 (A♠): ";
deck.ShowCard(12)
PRINT
PRINT "Card 13 (2♥): ";
deck.ShowCard(13)
PRINT
PRINT "Card 51 (A♣): ";
deck.ShowCard(51)
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  DECK CLASS TEST COMPLETE!                             ║"
PRINT "╚════════════════════════════════════════════════════════╝"
