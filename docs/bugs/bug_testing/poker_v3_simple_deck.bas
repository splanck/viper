REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Simple Deck Test            ║
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

REM ═══ SIMPLE DECK CLASS ═══

CLASS Deck
    SUB Init()
        PRINT "Deck initialized"
    END SUB

    SUB ShowTestCard()
        DIM c AS Card
        c = NEW Card()
        c.Init(0, 14)
        c.Display()
    END SUB
END CLASS

REM ═══ TEST ═══

PRINT "Creating deck..."
DIM deck AS Deck
deck = NEW Deck()
deck.Init()

PRINT "Show test card: ";
deck.ShowTestCard()
PRINT

PRINT "Done!"
