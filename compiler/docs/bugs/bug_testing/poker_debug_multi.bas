REM Debug multi-player arrays
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

PRINT "=== Debug: Array of Players ==="
PRINT

DIM numPlayers AS Integer
LET numPlayers = 4

REM Create array of players
DIM players(4) AS Player
DIM i AS Integer

PRINT "Creating players..."
FOR i = 0 TO 3
    PRINT "Creating player "; i
    DIM tempName AS String
    IF i = 0 THEN
        LET tempName = "Alice"
    ELSEIF i = 1 THEN
        LET tempName = "Bob"
    ELSEIF i = 2 THEN
        LET tempName = "Charlie"
    ELSE
        LET tempName = "Diana"
    END IF

    LET players(i) = NEW Player(tempName, 1000)
    PRINT "  Created: "; players(i).name; " with "; players(i).chips; " chips"
NEXT i

PRINT
PRINT "Verifying all players..."
FOR i = 0 TO 3
    PRINT "Player "; i; ": "; players(i).name; " - $"; players(i).chips
NEXT i

PRINT
PRINT "Creating deck and dealing..."
RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

REM Deal cards
DIM cardIdx AS Integer
FOR cardIdx = 1 TO 5
    PRINT "Dealing card "; cardIdx
    FOR i = 0 TO 3
        DIM c AS Card
        LET c = deck.Deal()
        players(i).hand.AddCard(c)
        PRINT "  -> "; players(i).name; " got "; c.ToString()
    NEXT i
NEXT cardIdx

PRINT
PRINT "Evaluating hands..."
FOR i = 0 TO 3
    players(i).hand.Evaluate()
    PRINT players(i).name; " has: "; players(i).hand.handName
NEXT i
