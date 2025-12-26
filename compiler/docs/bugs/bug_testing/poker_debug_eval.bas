REM Debug hand evaluation step by step
AddFile "poker_card.bas"
AddFile "poker_hand.bas"

PRINT "=== Debug Hand Evaluation ==="
PRINT

DIM h AS Hand
LET h = NEW Hand()

PRINT "Adding 5 cards to hand..."
h.AddCard(NEW Card("Hearts", "5", 5))
PRINT "  Card 1 added"
h.AddCard(NEW Card("Diamonds", "5", 5))
PRINT "  Card 2 added"
h.AddCard(NEW Card("Clubs", "8", 8))
PRINT "  Card 3 added"
h.AddCard(NEW Card("Spades", "King", 13))
PRINT "  Card 4 added"
h.AddCard(NEW Card("Hearts", "Ace", 14))
PRINT "  Card 5 added"
PRINT "Hand count: "; h.count
PRINT

PRINT "Testing CountValue..."
DIM val AS Integer
LET val = h.CountValue(5)
PRINT "Count of 5s: "; val
PRINT

PRINT "Testing SortByValue..."
h.SortByValue()
PRINT "Sorted successfully"
PRINT

PRINT "Testing IsFlush..."
DIM isF AS Boolean
LET isF = h.IsFlush()
PRINT "IsFlush: "; isF
PRINT

PRINT "Testing IsStraight..."
DIM isS AS Boolean
LET isS = h.IsStraight()
PRINT "IsStraight: "; isS
PRINT

PRINT "Testing CountPairs..."
DIM numP AS Integer
LET numP = h.CountPairs()
PRINT "CountPairs: "; numP
PRINT

PRINT "Testing HasThreeOfKind..."
DIM has3 AS Boolean
LET has3 = h.HasThreeOfKind()
PRINT "HasThreeOfKind: "; has3
PRINT

PRINT "All individual tests passed!"
PRINT "Now testing full Evaluate..."
h.Evaluate()
PRINT "Evaluate completed!"
PRINT "Hand: "; h.handName
