REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Reversed Class Order Test   ║
REM ╚════════════════════════════════════════════════════════╝

REM Define Deck class BEFORE Card class

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

CLASS Card
    suit AS INTEGER
    rank AS INTEGER

    SUB Init(cardSuit AS INTEGER, cardRank AS INTEGER)
        ME.suit = cardSuit
        ME.rank = cardRank
    END SUB

    SUB Display()
        PRINT "Card";
    END SUB
END CLASS

REM ═══ TEST ═══

PRINT "Creating deck..."
DIM deck AS Deck
deck = NEW Deck()
deck.Init()

PRINT "Show test card from deck: ";
deck.ShowTestCard()
PRINT

PRINT "Creating standalone card..."
DIM card AS Card
card = NEW Card()
card.Init(0, 14)
card.Display()

PRINT
PRINT "Done!"
