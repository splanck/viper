REM Debug card storage in hand
AddFile "poker_card.bas"
AddFile "poker_hand.bas"

PRINT "=== Debug Card Storage ==="
PRINT

DIM h AS Hand
LET h = NEW Hand()

PRINT "Creating cards..."
DIM c1 AS Card
DIM c2 AS Card
LET c1 = NEW Card("Hearts", "5", 5)
LET c2 = NEW Card("Diamonds", "5", 5)

PRINT "Card 1: "; c1.ToString(); " value="; c1.value
PRINT "Card 2: "; c2.ToString(); " value="; c2.value
PRINT

PRINT "Adding cards to hand..."
h.AddCard(c1)
PRINT "Added card 1, hand count="; h.count
h.AddCard(c2)
PRINT "Added card 2, hand count="; h.count
PRINT

PRINT "Reading cards back from hand..."
DIM i AS Integer
FOR i = 0 TO h.count - 1
    PRINT "Card "; i; ": "; h.cards(i).ToString(); " value="; h.cards(i).value
NEXT i
PRINT

PRINT "Testing CountValue(5)..."
DIM cnt AS Integer
LET cnt = h.CountValue(5)
PRINT "Result: "; cnt
