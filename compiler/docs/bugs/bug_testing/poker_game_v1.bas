REM Poker Game v1 - Basic game with 2 players
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

REM ANSI codes for UI
DIM ANSI_CLEAR AS String
DIM ANSI_GREEN AS String
DIM ANSI_YELLOW AS String
DIM ANSI_CYAN AS String

LET ANSI_CLEAR = CHR$(27) + "[2J" + CHR$(27) + "[H"
LET ANSI_GREEN = CHR$(27) + "[32m"
LET ANSI_YELLOW = CHR$(27) + "[33m"
LET ANSI_CYAN = CHR$(27) + "[36m"

Sub DrawBanner()
    PRINT ANSI_GREEN
    PRINT "╔════════════════════════════════════════╗"
    PRINT "║        VIPER BASIC POKER GAME          ║"
    PRINT "╚════════════════════════════════════════╝"
    PRINT ANSI_RESET
    PRINT
End Sub

Sub DrawLine()
    PRINT ANSI_YELLOW; "────────────────────────────────────────"; ANSI_RESET
End Sub

REM Main game
RANDOMIZE
PRINT ANSI_CLEAR
DrawBanner()

REM Create players
DIM p1 AS Player
DIM p2 AS Player
LET p1 = NEW Player("Alice", 1000)
LET p2 = NEW Player("Bob", 1000)

REM Create and shuffle deck
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

PRINT ANSI_CYAN; "Dealing cards..."; ANSI_RESET
PRINT

REM Deal 5 cards to each player
DIM i AS Integer
FOR i = 1 TO 5
    p1.hand.AddCard(deck.Deal())
    p2.hand.AddCard(deck.Deal())
NEXT i

REM Evaluate hands
p1.hand.Evaluate()
p2.hand.Evaluate()

REM Show player 1
PRINT ANSI_CYAN; "Player 1:"; ANSI_RESET
p1.ShowStatus()
PRINT

DrawLine()

REM Show player 2
PRINT ANSI_CYAN; "Player 2:"; ANSI_RESET
p2.ShowStatus()
PRINT

DrawLine()

REM Determine winner
PRINT
PRINT ANSI_GREEN; "RESULT:"; ANSI_RESET
IF p1.hand.handRank > p2.hand.handRank THEN
    PRINT p1.name; " wins with "; p1.hand.handName; "!"
    PRINT p2.name; " had "; p2.hand.handName
ELSEIF p2.hand.handRank > p1.hand.handRank THEN
    PRINT p2.name; " wins with "; p2.hand.handName; "!"
    PRINT p1.name; " had "; p1.hand.handName
ELSE
    PRINT "It's a tie! Both have "; p1.hand.handName
END IF
