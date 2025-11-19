REM Debug arrays of players with hands
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

PRINT "=== Debug Array of Players + Hands ==="
PRINT

REM Create 2 players
DIM players(2) AS Player
LET players(0) = NEW Player("Alice", 1000)
LET players(1) = NEW Player("Bob", 1000)

PRINT "Created 2 players"
PRINT "Alice hand count: "; players(0).hand.count
PRINT "Bob hand count: "; players(1).hand.count
PRINT

REM Create deck
RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

PRINT "Dealing 5 cards to each..."
DIM cardNum AS Integer
DIM i AS Integer

FOR cardNum = 1 TO 5
    PRINT "Dealing card "; cardNum
    FOR i = 0 TO 1
        DIM c AS Card
        LET c = deck.Deal()
        PRINT "  Player "; i; " gets "; c.ToString()
        players(i).hand.AddCard(c)
        PRINT "    Hand count now: "; players(i).hand.count
    NEXT i
NEXT cardNum

PRINT
PRINT "Verifying hands..."
PRINT "Alice has "; players(0).hand.count; " cards"
PRINT "Bob has "; players(1).hand.count; " cards"
PRINT

PRINT "Evaluating Alice's hand..."
players(0).hand.Evaluate()
PRINT "Alice: "; players(0).hand.handName

PRINT "Evaluating Bob's hand..."
players(1).hand.Evaluate()
PRINT "Bob: "; players(1).hand.handName
