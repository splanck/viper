REM Poker - Multi-player test with arrays of objects
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

PRINT "=== Multi-Player Poker Test ==="
PRINT

DIM numPlayers AS Integer
LET numPlayers = 4

REM Create array of players
DIM players(4) AS Player

REM Initialize players
DIM i AS Integer
DIM names(4) AS String
LET names(0) = "Alice"
LET names(1) = "Bob"
LET names(2) = "Charlie"
LET names(3) = "Diana"

FOR i = 0 TO numPlayers - 1
    LET players(i) = NEW Player(names(i), 1000)
    PRINT "Created player: "; players(i).name
NEXT i

PRINT

REM Create and shuffle deck
RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

PRINT "Dealing 5 cards to each player..."
PRINT

REM Deal 5 cards to each player
DIM cardNum AS Integer
FOR cardNum = 1 TO 5
    FOR i = 0 TO numPlayers - 1
        players(i).hand.AddCard(deck.Deal())
    NEXT i
NEXT cardNum

REM Evaluate all hands
FOR i = 0 TO numPlayers - 1
    players(i).hand.Evaluate()
NEXT i

REM Show all players
FOR i = 0 TO numPlayers - 1
    PRINT "Player "; i + 1; ":"
    players(i).ShowStatus()
    PRINT
NEXT i

REM Find winner
DIM winner AS Integer
DIM bestRank AS Integer
LET winner = 0
LET bestRank = players(0).hand.handRank

FOR i = 1 TO numPlayers - 1
    IF players(i).hand.handRank > bestRank THEN
        LET bestRank = players(i).hand.handRank
        LET winner = i
    END IF
NEXT i

PRINT "═══════════════════════════════════════"
PRINT "WINNER: "; players(winner).name; " with "; players(winner).hand.handName; "!"
PRINT "═══════════════════════════════════════"
