REM Poker Game - Non-interactive test
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

REM Test player class and betting
PRINT "=== Testing Player Class ==="
PRINT

DIM p1 AS Player
LET p1 = NEW Player("Alice", 1000)

PRINT "Created player: "; p1.name
PRINT "Starting chips: "; p1.chips
PRINT

PRINT "Placing bet of $50..."
p1.PlaceBet(50)
PRINT "Chips after bet: "; p1.chips
PRINT "Current bet: "; p1.currentBet
PRINT

PRINT "Can bet $2000? "; p1.CanBet(2000)
PRINT "Can bet $500? "; p1.CanBet(500)
PRINT

PRINT "Winning pot of $200..."
p1.WinPot(200)
PRINT "Chips after win: "; p1.chips
PRINT

REM Test dealing to player
PRINT "=== Testing Deal to Player ==="
RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

DIM i AS Integer
FOR i = 1 TO 5
    p1.hand.AddCard(deck.Deal())
NEXT i

p1.hand.Evaluate()
PRINT "Player's hand:"
p1.ShowStatus()
