REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Card Class Test             ║
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

REM ═══ TEST CARD CLASS ═══

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         CARD CLASS STRESS TEST                         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
PRINT

PRINT "Creating test cards..."
DIM aceSpades AS Card
DIM kingHearts AS Card
DIM queenDiamonds AS Card
DIM tenClubs AS Card

aceSpades = NEW Card()
kingHearts = NEW Card()
queenDiamonds = NEW Card()
tenClubs = NEW Card()

aceSpades.Init(0, 14)
kingHearts.Init(1, 13)
queenDiamonds.Init(2, 12)
tenClubs.Init(3, 10)

PRINT "Test hand: ";
aceSpades.Display()
PRINT " ";
kingHearts.Display()
PRINT " ";
queenDiamonds.Display()
PRINT " ";
tenClubs.Display()
PRINT
PRINT

PRINT "Testing all suits:"
DIM testCard AS Card
testCard = NEW Card()

DIM suit AS INTEGER
FOR suit = 0 TO 3
    testCard.Init(suit, 14)
    PRINT "  Suit "; suit; ": ";
    testCard.Display()
    PRINT
NEXT suit
PRINT

PRINT "Testing all ranks:"
FOR suit = 14 TO 2 STEP -1
    testCard.Init(0, suit)
    testCard.Display()
    PRINT " ";
NEXT suit
PRINT
PRINT

PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  CARD CLASS TEST COMPLETE!                             ║"
PRINT "╚════════════════════════════════════════════════════════╝"
