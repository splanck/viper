REM poker_main.bas - Main poker game using AddFile
REM This tests the AddFile keyword

AddFile "poker_card.bas"
AddFile "poker_deck.bas"

REM Main program
PRINT "=== Poker Game - AddFile Test ==="
PRINT

RANDOMIZE
DIM deck AS Deck
LET deck = NEW Deck()

PRINT "Created deck with "; deck.count; " cards"
deck.Shuffle()
PRINT "Deck shuffled!"
PRINT
PRINT "Dealing 5 cards:"

DIM i AS Integer
FOR i = 1 TO 5
    DIM card AS Card
    LET card = deck.Deal()
    PRINT "  Card "; i; ": "; card.ToString()
NEXT i
