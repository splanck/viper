REM Poker Game v2 - Interactive with betting
AddFile "poker_card.bas"
AddFile "poker_deck.bas"
AddFile "poker_hand.bas"
AddFile "poker_player.bas"

REM ANSI codes
DIM ANSI_CLEAR AS String
DIM ANSI_GREEN AS String
DIM ANSI_YELLOW AS String
DIM ANSI_CYAN AS String
DIM ANSI_RED AS String

LET ANSI_CLEAR = CHR$(27) + "[2J" + CHR$(27) + "[H"
LET ANSI_GREEN = CHR$(27) + "[32m"
LET ANSI_YELLOW = CHR$(27) + "[33m"
LET ANSI_CYAN = CHR$(27) + "[36m"
LET ANSI_RED = CHR$(27) + "[31m"

Sub DrawBanner()
    PRINT ANSI_GREEN
    PRINT "╔════════════════════════════════════════╗"
    PRINT "║        VIPER BASIC POKER GAME          ║"
    PRINT "╚════════════════════════════════════════╝"
    PRINT ANSI_RESET
    PRINT
End Sub

REM Game state
DIM pot AS Integer
DIM currentBet AS Integer

Sub ShowPot()
    PRINT ANSI_YELLOW; "Pot: $"; pot; ANSI_RESET
    IF currentBet > 0 THEN
        PRINT ANSI_YELLOW; "Current bet: $"; currentBet; ANSI_RESET
    END IF
    PRINT
End Sub

Function GetPlayerAction(p AS Player) AS Integer
    PRINT ANSI_CYAN; p.name; "'s turn:"; ANSI_RESET
    PRINT "Your hand:"
    p.hand.ShowHand()
    PRINT
    PRINT "Your chips: $"; p.chips
    PRINT
    PRINT "1) Check/Call"
    PRINT "2) Raise"
    PRINT "3) Fold"
    PRINT

    DIM choice AS Integer
    INPUT "Your choice (1-3): ", choice
    LET GetPlayerAction = choice
End Function

Function GetCPUAction(p AS Player) AS Integer
    REM Simple AI: fold if high card, call otherwise
    DIM action AS Integer

    IF p.hand.handRank = 0 THEN
        REM High card - 50% chance to fold
        IF RND() < 0.5 THEN
            LET action = 3
        ELSE
            LET action = 1
        END IF
    ELSEIF p.hand.handRank >= 3 THEN
        REM Good hand - raise
        LET action = 2
    ELSE
        REM Decent hand - call
        LET action = 1
    END IF

    LET GetCPUAction = action
End Function

REM Main game
RANDOMIZE
PRINT ANSI_CLEAR
DrawBanner()

REM Create players
DIM player AS Player
DIM cpu AS Player
LET player = NEW Player("You", 1000)
LET cpu = NEW Player("CPU", 1000)

REM Initialize pot and bets
LET pot = 0
LET currentBet = 0

REM Antes
DIM ante AS Integer
LET ante = 10
player.PlaceBet(ante)
cpu.PlaceBet(ante)
LET pot = ante * 2

PRINT "Antes posted ($"; ante; " each)"
PRINT

REM Create and shuffle deck
DIM deck AS Deck
LET deck = NEW Deck()
deck.Shuffle()

REM Deal 5 cards to each player
DIM i AS Integer
FOR i = 1 TO 5
    player.hand.AddCard(deck.Deal())
    cpu.hand.AddCard(deck.Deal())
NEXT i

REM Evaluate hands
player.hand.Evaluate()
cpu.hand.Evaluate()

PRINT ANSI_CYAN; "Cards dealt!"; ANSI_RESET
PRINT
ShowPot()

REM Betting round
DIM playerAction AS Integer
DIM cpuAction AS Integer

LET playerAction = GetPlayerAction(player)

IF playerAction = 3 THEN
    PRINT ANSI_RED; "You folded!"; ANSI_RESET
    PRINT "CPU wins $"; pot
    cpu.WinPot(pot)
ELSE
    IF playerAction = 2 THEN
        DIM raiseAmount AS Integer
        INPUT "Raise amount: $", raiseAmount
        player.PlaceBet(raiseAmount)
        LET currentBet = raiseAmount
        LET pot = pot + raiseAmount
        PRINT "You raised $"; raiseAmount
        PRINT
    ELSEIF playerAction = 1 THEN
        PRINT "You checked"
        PRINT
    END IF

    REM CPU's turn
    PRINT ANSI_CYAN; "CPU's turn..."; ANSI_RESET
    LET cpuAction = GetCPUAction(cpu)

    IF cpuAction = 3 THEN
        PRINT ANSI_GREEN; "CPU folded!"; ANSI_RESET
        PRINT "You win $"; pot
        player.WinPot(pot)
    ELSEIF cpuAction = 2 THEN
        DIM cpuRaise AS Integer
        LET cpuRaise = INT(RND() * 50) + 20
        cpu.PlaceBet(cpuRaise)
        LET pot = pot + cpuRaise
        PRINT "CPU raised $"; cpuRaise
        PRINT
    ELSE
        IF currentBet > 0 THEN
            cpu.PlaceBet(currentBet)
            LET pot = pot + currentBet
            PRINT "CPU called"
        ELSE
            PRINT "CPU checked"
        END IF
        PRINT
    END IF

    REM Showdown if both still in
    IF cpuAction <> 3 THEN
        PRINT
        PRINT ANSI_YELLOW; "═══════════ SHOWDOWN ═══════════"; ANSI_RESET
        PRINT
        PRINT ANSI_CYAN; "Your hand:"; ANSI_RESET
        player.hand.ShowHand()
        PRINT
        PRINT ANSI_CYAN; "CPU's hand:"; ANSI_RESET
        cpu.hand.ShowHand()
        PRINT

        PRINT ANSI_YELLOW; "═════════════════════════════════"; ANSI_RESET
        PRINT

        IF player.hand.handRank > cpu.hand.handRank THEN
            PRINT ANSI_GREEN; "YOU WIN $"; pot; "!"; ANSI_RESET
            PRINT "You had "; player.hand.handName
            PRINT "CPU had "; cpu.hand.handName
            player.WinPot(pot)
        ELSEIF cpu.hand.handRank > player.hand.handRank THEN
            PRINT ANSI_RED; "CPU WINS $"; pot; ANSI_RESET
            PRINT "CPU had "; cpu.hand.handName
            PRINT "You had "; player.hand.handName
            cpu.WinPot(pot)
        ELSE
            PRINT ANSI_YELLOW; "TIE!"; ANSI_RESET
            PRINT "Both had "; player.hand.handName
            DIM split AS Integer
            LET split = pot / 2
            player.WinPot(split)
            cpu.WinPot(split)
        END IF
    END IF
END IF

PRINT
PRINT ANSI_CYAN; "Final chip counts:"; ANSI_RESET
PRINT "You: $"; player.chips
PRINT "CPU: $"; cpu.chips
