REM Debug dealing directly from deck to hand
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"

PRINT "=== Debug Deck->Hand Transfer ==="
PRINT

RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

PRINT "Deck created and shuffled"
PRINT

DIM h AS Hand
LET h = NEW Hand()

PRINT "Method 1: Deal with intermediate variable"
DIM c AS Card
LET c = deck.Deal()
PRINT "Dealt card: "; c.ToString(); " value="; c.value
h.AddCard(c)
PRINT "Added to hand, count="; h.count
PRINT "Card in hand: "; h.cards(0).ToString(); " value="; h.cards(0).value
PRINT

PRINT "Method 2: Deal directly"
h.AddCard(deck.Deal())
PRINT "Added to hand, count="; h.count
PRINT "Card in hand: "; h.cards(1).ToString(); " value="; h.cards(1).value
PRINT

PRINT "Method 3: Deal in loop"
DIM i AS Integer
FOR i = 1 TO 3
    h.AddCard(deck.Deal())
    PRINT "Card "; h.count; " added"
NEXT i
PRINT "Hand count after loop: "; h.count
PRINT

PRINT "Showing all cards..."
FOR i = 0 TO h.count - 1
    PRINT "Card "; i; ": "; h.cards(i).ToString(); " value="; h.cards(i).value
NEXT i
