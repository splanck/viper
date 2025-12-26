REM Test all poker hand types
AddFile "poker_card.bas"
AddFile "poker_hand.bas"

PRINT "=== Testing Poker Hand Evaluation ==="
PRINT

REM Test 1: High Card
DIM h1 AS Hand
LET h1 = NEW Hand()
h1.AddCard(NEW Card("Hearts", "2", 2))
h1.AddCard(NEW Card("Spades", "5", 5))
h1.AddCard(NEW Card("Clubs", "7", 7))
h1.AddCard(NEW Card("Diamonds", "9", 9))
h1.AddCard(NEW Card("Hearts", "King", 13))
h1.Evaluate()
PRINT "Test 1 - High Card:"
h1.ShowHand()
PRINT

REM Test 2: One Pair
DIM h2 AS Hand
LET h2 = NEW Hand()
h2.AddCard(NEW Card("Hearts", "8", 8))
h2.AddCard(NEW Card("Spades", "8", 8))
h2.AddCard(NEW Card("Clubs", "3", 3))
h2.AddCard(NEW Card("Diamonds", "King", 13))
h2.AddCard(NEW Card("Hearts", "Ace", 14))
h2.Evaluate()
PRINT "Test 2 - One Pair:"
h2.ShowHand()
PRINT

REM Test 3: Two Pair
DIM h3 AS Hand
LET h3 = NEW Hand()
h3.AddCard(NEW Card("Hearts", "8", 8))
h3.AddCard(NEW Card("Spades", "8", 8))
h3.AddCard(NEW Card("Clubs", "3", 3))
h3.AddCard(NEW Card("Diamonds", "3", 3))
h3.AddCard(NEW Card("Hearts", "Ace", 14))
h3.Evaluate()
PRINT "Test 3 - Two Pair:"
h3.ShowHand()
PRINT

REM Test 4: Three of a Kind
DIM h4 AS Hand
LET h4 = NEW Hand()
h4.AddCard(NEW Card("Hearts", "7", 7))
h4.AddCard(NEW Card("Spades", "7", 7))
h4.AddCard(NEW Card("Clubs", "7", 7))
h4.AddCard(NEW Card("Diamonds", "King", 13))
h4.AddCard(NEW Card("Hearts", "2", 2))
h4.Evaluate()
PRINT "Test 4 - Three of a Kind:"
h4.ShowHand()
PRINT

REM Test 5: Straight
DIM h5 AS Hand
LET h5 = NEW Hand()
h5.AddCard(NEW Card("Hearts", "5", 5))
h5.AddCard(NEW Card("Spades", "6", 6))
h5.AddCard(NEW Card("Clubs", "7", 7))
h5.AddCard(NEW Card("Diamonds", "8", 8))
h5.AddCard(NEW Card("Hearts", "9", 9))
h5.Evaluate()
PRINT "Test 5 - Straight:"
h5.ShowHand()
PRINT

REM Test 6: Flush
DIM h6 AS Hand
LET h6 = NEW Hand()
h6.AddCard(NEW Card("Hearts", "2", 2))
h6.AddCard(NEW Card("Hearts", "5", 5))
h6.AddCard(NEW Card("Hearts", "7", 7))
h6.AddCard(NEW Card("Hearts", "9", 9))
h6.AddCard(NEW Card("Hearts", "King", 13))
h6.Evaluate()
PRINT "Test 6 - Flush:"
h6.ShowHand()
PRINT

REM Test 7: Full House
DIM h7 AS Hand
LET h7 = NEW Hand()
h7.AddCard(NEW Card("Hearts", "8", 8))
h7.AddCard(NEW Card("Spades", "8", 8))
h7.AddCard(NEW Card("Clubs", "8", 8))
h7.AddCard(NEW Card("Diamonds", "3", 3))
h7.AddCard(NEW Card("Hearts", "3", 3))
h7.Evaluate()
PRINT "Test 7 - Full House:"
h7.ShowHand()
PRINT

REM Test 8: Four of a Kind
DIM h8 AS Hand
LET h8 = NEW Hand()
h8.AddCard(NEW Card("Hearts", "Queen", 12))
h8.AddCard(NEW Card("Spades", "Queen", 12))
h8.AddCard(NEW Card("Clubs", "Queen", 12))
h8.AddCard(NEW Card("Diamonds", "Queen", 12))
h8.AddCard(NEW Card("Hearts", "5", 5))
h8.Evaluate()
PRINT "Test 8 - Four of a Kind:"
h8.ShowHand()
PRINT

REM Test 9: Straight Flush
DIM h9 AS Hand
LET h9 = NEW Hand()
h9.AddCard(NEW Card("Spades", "5", 5))
h9.AddCard(NEW Card("Spades", "6", 6))
h9.AddCard(NEW Card("Spades", "7", 7))
h9.AddCard(NEW Card("Spades", "8", 8))
h9.AddCard(NEW Card("Spades", "9", 9))
h9.Evaluate()
PRINT "Test 9 - Straight Flush:"
h9.ShowHand()
