REM ╔════════════════════════════════════════════════════════╗
REM ║     TEXAS HOLD'EM POKER - Table with Community Cards  ║
REM ╚════════════════════════════════════════════════════════╝
REM
REM Workaround for BUG-074: Classes in reverse dependency order
REM Workaround for BUG-067: No array fields in classes

CLASS Table
    REM Community cards (5 total)
    flop1Suit AS INTEGER
    flop1Rank AS INTEGER
    flop2Suit AS INTEGER
    flop2Rank AS INTEGER
    flop3Suit AS INTEGER
    flop3Rank AS INTEGER
    turnSuit AS INTEGER
    turnRank AS INTEGER
    riverSuit AS INTEGER
    riverRank AS INTEGER
    pot AS INTEGER

    SUB Init()
        ME.pot = 0
        ME.flop1Rank = -1
        ME.flop2Rank = -1
        ME.flop3Rank = -1
        ME.turnRank = -1
        ME.riverRank = -1
    END SUB

    SUB AddToPot(amount AS INTEGER)
        ME.pot = ME.pot + amount
    END SUB

    SUB DealFlop(s1 AS INTEGER, r1 AS INTEGER, s2 AS INTEGER, r2 AS INTEGER, s3 AS INTEGER, r3 AS INTEGER)
        ME.flop1Suit = s1
        ME.flop1Rank = r1
        ME.flop2Suit = s2
        ME.flop2Rank = r2
        ME.flop3Suit = s3
        ME.flop3Rank = r3
    END SUB

    SUB DealTurn(s AS INTEGER, r AS INTEGER)
        ME.turnSuit = s
        ME.turnRank = r
    END SUB

    SUB DealRiver(s AS INTEGER, r AS INTEGER)
        ME.riverSuit = s
        ME.riverRank = r
    END SUB

    SUB ShowCommunityCards()
        COLOR 14, 0
        PRINT "╔════════════════════════════════════════════════════════╗"
        PRINT "║ COMMUNITY CARDS                        POT: "; ME.pot; "       "
        PRINT "╠════════════════════════════════════════════════════════╣"
        COLOR 15, 0
        PRINT "║ ";

        IF ME.flop1Rank >= 0 THEN
            DIM c1 AS Card
            c1 = NEW Card()
            c1.Init(ME.flop1Suit, ME.flop1Rank)
            c1.Display()
            PRINT " ";
        END IF

        IF ME.flop2Rank >= 0 THEN
            DIM c2 AS Card
            c2 = NEW Card()
            c2.Init(ME.flop2Suit, ME.flop2Rank)
            c2.Display()
            PRINT " ";
        END IF

        IF ME.flop3Rank >= 0 THEN
            DIM c3 AS Card
            c3 = NEW Card()
            c3.Init(ME.flop3Suit, ME.flop3Rank)
            c3.Display()
            PRINT " ";
        END IF

        IF ME.turnRank >= 0 THEN
            DIM c4 AS Card
            c4 = NEW Card()
            c4.Init(ME.turnSuit, ME.turnRank)
            c4.Display()
            PRINT " ";
        END IF

        IF ME.riverRank >= 0 THEN
            DIM c5 AS Card
            c5 = NEW Card()
            c5.Init(ME.riverSuit, ME.riverRank)
            c5.Display()
        END IF

        PRINT
        COLOR 14, 0
        PRINT "╚════════════════════════════════════════════════════════╝"
        COLOR 15, 0
    END SUB

    SUB ClearTable()
        ME.pot = 0
        ME.flop1Rank = -1
        ME.flop2Rank = -1
        ME.flop3Rank = -1
        ME.turnRank = -1
        ME.riverRank = -1
    END SUB
END CLASS

CLASS Player
    card1Suit AS INTEGER
    card1Rank AS INTEGER
    card2Suit AS INTEGER
    card2Rank AS INTEGER
    chips AS INTEGER
    name AS STRING

    SUB Init(playerName AS STRING, startingChips AS INTEGER)
        ME.name = playerName
        ME.chips = startingChips
        ME.card1Suit = -1
        ME.card1Rank = -1
        ME.card2Suit = -1
        ME.card2Rank = -1
    END SUB

    SUB GiveCard(cardNum AS INTEGER, suit AS INTEGER, rank AS INTEGER)
        IF cardNum = 1 THEN
            ME.card1Suit = suit
            ME.card1Rank = rank
        ELSEIF cardNum = 2 THEN
            ME.card2Suit = suit
            ME.card2Rank = rank
        END IF
    END SUB

    SUB Bet(amount AS INTEGER)
        IF amount <= ME.chips THEN
            ME.chips = ME.chips - amount
        END IF
    END SUB

    SUB ShowHand()
        COLOR 11, 0
        PRINT "╔════════════════════════════════════════════════════════╗"
        PRINT "║ "; ME.name;
        PRINT "                    CHIPS: "; ME.chips; "       "
        PRINT "╠════════════════════════════════════════════════════════╣"
        COLOR 15, 0
        PRINT "║ HAND: ";

        IF ME.card1Rank >= 0 THEN
            DIM c1 AS Card
            c1 = NEW Card()
            c1.Init(ME.card1Suit, ME.card1Rank)
            c1.Display()
            PRINT " ";
        END IF

        IF ME.card2Rank >= 0 THEN
            DIM c2 AS Card
            c2 = NEW Card()
            c2.Init(ME.card2Suit, ME.card2Rank)
            c2.Display()
        END IF

        PRINT
        COLOR 11, 0
        PRINT "╚════════════════════════════════════════════════════════╝"
        COLOR 15, 0
    END SUB
END CLASS

CLASS Deck
    currentCard AS INTEGER

    SUB Init()
        ME.currentCard = 0
    END SUB

    FUNCTION GetCardSuit(index AS INTEGER) AS INTEGER
        RETURN index \ 13
    END FUNCTION

    FUNCTION GetCardRank(index AS INTEGER) AS INTEGER
        DIM rankOffset AS INTEGER
        rankOffset = index - ((index \ 13) * 13)
        RETURN rankOffset + 2
    END FUNCTION

    FUNCTION DealCardSuit() AS INTEGER
        DIM suit AS INTEGER
        suit = ME.GetCardSuit(ME.currentCard)
        ME.currentCard = ME.currentCard + 1
        RETURN suit
    END FUNCTION

    FUNCTION DealCardRank() AS INTEGER
        DIM rank AS INTEGER
        rank = ME.GetCardRank(ME.currentCard - 1)
        RETURN rank
    END FUNCTION
END CLASS

CLASS Card
    suit AS INTEGER
    rank AS INTEGER

    SUB Init(cardSuit AS INTEGER, cardRank AS INTEGER)
        ME.suit = cardSuit
        ME.rank = cardRank
    END SUB

    FUNCTION GetSuitName() AS STRING
        IF ME.suit = 0 THEN
            RETURN "♠"
        ELSEIF ME.suit = 1 THEN
            RETURN "♥"
        ELSEIF ME.suit = 2 THEN
            RETURN "♦"
        ELSEIF ME.suit = 3 THEN
            RETURN "♣"
        END IF
        RETURN "?"
    END FUNCTION

    FUNCTION GetRankName() AS STRING
        IF ME.rank = 14 THEN
            RETURN "A"
        ELSEIF ME.rank = 13 THEN
            RETURN "K"
        ELSEIF ME.rank = 12 THEN
            RETURN "Q"
        ELSEIF ME.rank = 11 THEN
            RETURN "J"
        ELSEIF ME.rank = 10 THEN
            RETURN "10"
        ELSEIF ME.rank = 9 THEN
            RETURN "9"
        ELSEIF ME.rank = 8 THEN
            RETURN "8"
        ELSEIF ME.rank = 7 THEN
            RETURN "7"
        ELSEIF ME.rank = 6 THEN
            RETURN "6"
        ELSEIF ME.rank = 5 THEN
            RETURN "5"
        ELSEIF ME.rank = 4 THEN
            RETURN "4"
        ELSEIF ME.rank = 3 THEN
            RETURN "3"
        ELSEIF ME.rank = 2 THEN
            RETURN "2"
        END IF
        RETURN "?"
    END FUNCTION

    SUB Display()
        IF ME.suit = 1 OR ME.suit = 2 THEN
            COLOR 12, 0
        ELSE
            COLOR 15, 0
        END IF

        DIM suitName AS STRING
        DIM rankName AS STRING
        suitName = ME.GetSuitName()
        rankName = ME.GetRankName()

        PRINT rankName; suitName;
        COLOR 15, 0
    END SUB
END CLASS

REM ═══ TEST POKER TABLE ═══

COLOR 10, 0
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║         TEXAS HOLD'EM POKER - TABLE TEST (v7)         ║"
PRINT "╚════════════════════════════════════════════════════════╝"
COLOR 15, 0
PRINT

REM Initialize game
DIM deck AS Deck
DIM table AS Table
DIM player1 AS Player
DIM player2 AS Player

deck = NEW Deck()
table = NEW Table()
player1 = NEW Player()
player2 = NEW Player()

deck.Init()
table.Init()
player1.Init("HUMAN PLAYER", 2000)
player2.Init("AI OPPONENT", 2000)

REM Deal hole cards
DIM s AS INTEGER
DIM r AS INTEGER

s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(1, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player2.GiveCard(1, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player1.GiveCard(2, s, r)

s = deck.DealCardSuit()
r = deck.DealCardRank()
player2.GiveCard(2, s, r)

REM Pre-flop betting
PRINT "PRE-FLOP BETTING"
PRINT "────────────────────────────────────────────────────────"
player1.Bet(50)
player2.Bet(50)
table.AddToPot(100)
PRINT "Both players bet 50 chips"
PRINT

REM Deal flop
DIM f1s AS INTEGER
DIM f1r AS INTEGER
DIM f2s AS INTEGER
DIM f2r AS INTEGER
DIM f3s AS INTEGER
DIM f3r AS INTEGER

f1s = deck.DealCardSuit()
f1r = deck.DealCardRank()
f2s = deck.DealCardSuit()
f2r = deck.DealCardRank()
f3s = deck.DealCardSuit()
f3r = deck.DealCardRank()

table.DealFlop(f1s, f1r, f2s, f2r, f3s, f3r)

PRINT "THE FLOP"
PRINT "────────────────────────────────────────────────────────"
table.ShowCommunityCards()
PRINT

REM Flop betting
player1.Bet(100)
player2.Bet(100)
table.AddToPot(200)
PRINT "Both players bet 100 chips"
PRINT

REM Deal turn
s = deck.DealCardSuit()
r = deck.DealCardRank()
table.DealTurn(s, r)

PRINT "THE TURN"
PRINT "────────────────────────────────────────────────────────"
table.ShowCommunityCards()
PRINT

REM Deal river
s = deck.DealCardSuit()
r = deck.DealCardRank()
table.DealRiver(s, r)

PRINT "THE RIVER"
PRINT "────────────────────────────────────────────────────────"
table.ShowCommunityCards()
PRINT

PRINT "SHOWDOWN"
PRINT "────────────────────────────────────────────────────────"
player1.ShowHand()
PRINT
player2.ShowHand()
PRINT

COLOR 10, 0
PRINT "╔════════════════════════════════════════════════════════╗"
PRINT "║  TABLE TEST COMPLETE!                                  ║"
PRINT "╚════════════════════════════════════════════════════════╝"
COLOR 15, 0
