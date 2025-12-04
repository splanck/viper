' =============================================================================
' VIPER CARD GAME COLLECTION - Simplified Blackjack
' =============================================================================
' Demonstrates: Classes, Viper.Collections.List, Viper.Random, Viper.Time
' Workarounds applied for BUG-CARDS-009, BUG-CARDS-010, BUG-CARDS-012
' =============================================================================

' =============================================================================
' CARD CLASS
' =============================================================================

CLASS Card
    PUBLIC suit AS INTEGER
    PUBLIC rank AS INTEGER
    PUBLIC faceUp AS INTEGER

    SUB New()
        suit = 0
        rank = 0
        faceUp = 1
    END SUB

    SUB Init(s AS INTEGER, r AS INTEGER)
        suit = s
        rank = r
        faceUp = 1
    END SUB

    SUB SetFaceUp(f AS INTEGER)
        faceUp = f
    END SUB

    FUNCTION GetValue() AS INTEGER
        IF rank >= 10 THEN
            RETURN 10
        ELSEIF rank = 1 THEN
            RETURN 11
        ELSE
            RETURN rank
        END IF
    END FUNCTION
END CLASS

' =============================================================================
' GLOBAL GAME STATE
' =============================================================================

DIM g_deck AS Viper.Collections.List
DIM g_playerHand AS Viper.Collections.List
DIM g_dealerHand AS Viper.Collections.List
DIM g_chips AS INTEGER
DIM g_bet AS INTEGER

SUB InitDeck()
    DIM s AS INTEGER
    DIM r AS INTEGER
    DIM c AS Card

    g_deck = NEW Viper.Collections.List()

    FOR s = 0 TO 3
        FOR r = 1 TO 13
            c = NEW Card()
            c.Init(s, r)
            g_deck.Add(c)
        NEXT r
    NEXT s
END SUB

SUB ShuffleDeck()
    DIM n AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM randVal AS DOUBLE
    DIM cardI AS Card
    DIM cardJ AS Card

    n = g_deck.Count

    FOR i = n - 1 TO 1 STEP -1
        randVal = Viper.Random.Next()
        j = INT(randVal * (i + 1))
        IF j > i THEN
            j = i
        END IF
        IF j < 0 THEN
            j = 0
        END IF

        cardI = g_deck.get_Item(i)
        cardJ = g_deck.get_Item(j)
        g_deck.set_Item(i, cardJ)
        g_deck.set_Item(j, cardI)
    NEXT i
END SUB

SUB DrawCard(hand AS Viper.Collections.List)
    DIM c AS Card
    DIM lastIdx AS INTEGER

    lastIdx = g_deck.Count - 1
    c = g_deck.get_Item(lastIdx)
    g_deck.RemoveAt(lastIdx)
    hand.Add(c)
END SUB

SUB DrawCardFaceDown(hand AS Viper.Collections.List)
    DIM c AS Card
    DIM lastIdx AS INTEGER

    lastIdx = g_deck.Count - 1
    c = g_deck.get_Item(lastIdx)
    c.SetFaceUp(0)
    g_deck.RemoveAt(lastIdx)
    hand.Add(c)
END SUB

FUNCTION GetHandValue(hand AS Viper.Collections.List) AS INTEGER
    DIM total AS INTEGER
    DIM aces AS INTEGER
    DIM i AS INTEGER
    DIM c AS Card
    DIM val AS INTEGER

    total = 0
    aces = 0

    FOR i = 0 TO hand.Count - 1
        c = hand.get_Item(i)
        val = c.GetValue()
        total = total + val
        IF c.rank = 1 THEN
            aces = aces + 1
        END IF
    NEXT i

    DO WHILE total > 21 AND aces > 0
        total = total - 10
        aces = aces - 1
    LOOP

    RETURN total
END FUNCTION

SUB PrintCard(c AS Card)
    DIM rankStr AS STRING
    DIM suitStr AS STRING

    SELECT CASE c.rank
        CASE 1
            rankStr = "A"
        CASE 11
            rankStr = "J"
        CASE 12
            rankStr = "Q"
        CASE 13
            rankStr = "K"
        CASE ELSE
            rankStr = LTRIM$(STR$(c.rank))
    END SELECT

    SELECT CASE c.suit
        CASE 0
            suitStr = "H"
        CASE 1
            suitStr = "D"
        CASE 2
            suitStr = "C"
        CASE 3
            suitStr = "S"
        CASE ELSE
            suitStr = "?"
    END SELECT

    IF c.faceUp = 0 THEN
        PRINT "[??]";
    ELSE
        PRINT "["; rankStr; suitStr; "]";
    END IF
END SUB

SUB PrintHand(hand AS Viper.Collections.List)
    DIM i AS INTEGER
    DIM c AS Card

    FOR i = 0 TO hand.Count - 1
        c = hand.get_Item(i)
        PrintCard(c)
        PRINT " ";
    NEXT i
END SUB

SUB DisplayTable()
    PRINT ""
    PRINT "===== BLACKJACK ====="
    PRINT ""

    PRINT "Dealer: ";
    PrintHand(g_dealerHand)
    PRINT ""

    PRINT "You: ";
    PrintHand(g_playerHand)
    PRINT "= "; GetHandValue(g_playerHand)

    PRINT ""
    PRINT "Chips: "; g_chips; "  Bet: "; g_bet
    PRINT ""
END SUB

SUB DealInitialCards()
    g_playerHand = NEW Viper.Collections.List()
    g_dealerHand = NEW Viper.Collections.List()

    DrawCard(g_playerHand)
    DrawCardFaceDown(g_dealerHand)
    DrawCard(g_playerHand)
    DrawCard(g_dealerHand)
END SUB

SUB RevealDealerHole()
    DIM c AS Card
    c = g_dealerHand.get_Item(0)
    c.SetFaceUp(1)
END SUB

SUB DealerTurn()
    DIM handVal AS INTEGER

    RevealDealerHole()
    PRINT "Dealer reveals: ";
    PrintHand(g_dealerHand)
    PRINT "= "; GetHandValue(g_dealerHand)

    handVal = GetHandValue(g_dealerHand)

    DO WHILE handVal < 17
        Viper.Time.SleepMs(500)
        DrawCard(g_dealerHand)
        handVal = GetHandValue(g_dealerHand)
        PRINT "Dealer hits: ";
        PrintHand(g_dealerHand)
        PRINT "= "; handVal
    LOOP

    IF handVal > 21 THEN
        PRINT "Dealer BUSTS!"
    ELSE
        PRINT "Dealer stands with "; handVal
    END IF
END SUB

SUB SettleBet()
    DIM playerVal AS INTEGER
    DIM dealerVal AS INTEGER

    playerVal = GetHandValue(g_playerHand)
    dealerVal = GetHandValue(g_dealerHand)

    PRINT ""
    PRINT "=== RESULTS ==="

    IF playerVal > 21 THEN
        PRINT "You BUST! Lost "; g_bet
        g_chips = g_chips - g_bet
    ELSEIF dealerVal > 21 THEN
        PRINT "Dealer busts! You win "; g_bet
        g_chips = g_chips + g_bet
    ELSEIF playerVal > dealerVal THEN
        PRINT "You win "; g_bet
        g_chips = g_chips + g_bet
    ELSEIF dealerVal > playerVal THEN
        PRINT "Dealer wins. You lose "; g_bet
        g_chips = g_chips - g_bet
    ELSE
        PRINT "Push - bet returned"
    END IF

    PRINT "Chips: "; g_chips
END SUB

SUB PlayRound()
    DIM userInput AS STRING
    DIM choice AS INTEGER
    DIM handVal AS INTEGER
    DIM playing AS INTEGER

    ' Place bet
    PRINT "You have "; g_chips; " chips."
    PRINT "Enter bet (10-"; g_chips; "): ";
    userInput = Viper.Console.ReadLine()
    g_bet = Viper.Convert.ToInt(userInput)
    IF g_bet < 10 THEN
        g_bet = 10
    END IF
    IF g_bet > g_chips THEN
        g_bet = g_chips
    END IF

    ' Deal
    DealInitialCards()
    DisplayTable()

    ' Check for blackjack
    handVal = GetHandValue(g_playerHand)
    IF handVal = 21 THEN
        PRINT "BLACKJACK!"
        RevealDealerHole()
        IF GetHandValue(g_dealerHand) = 21 THEN
            PRINT "Dealer also has blackjack - Push"
        ELSE
            PRINT "You win "; (g_bet * 3) / 2
            g_chips = g_chips + (g_bet * 3) / 2
        END IF
        RETURN
    END IF

    ' Player turn
    playing = 1
    DO WHILE playing = 1
        PRINT "1) Hit  2) Stand"
        PRINT "Choice: ";
        userInput = Viper.Console.ReadLine()
        choice = Viper.Convert.ToInt(userInput)

        SELECT CASE choice
            CASE 1
                DrawCard(g_playerHand)
                DisplayTable()
                handVal = GetHandValue(g_playerHand)
                IF handVal > 21 THEN
                    PRINT "BUST!"
                    playing = 0
                ELSEIF handVal = 21 THEN
                    PRINT "21!"
                    playing = 0
                END IF

            CASE 2
                PRINT "You stand with "; GetHandValue(g_playerHand)
                playing = 0
        END SELECT
    LOOP

    ' Dealer turn (only if player didn't bust)
    handVal = GetHandValue(g_playerHand)
    IF handVal <= 21 THEN
        DealerTurn()
    END IF

    SettleBet()
END SUB

SUB PlayBlackjack()
    DIM userInput AS STRING
    DIM continuePlaying AS INTEGER

    PRINT ""
    PRINT "===== BLACKJACK ====="
    PRINT "Beat the dealer without going over 21!"
    PRINT ""

    g_chips = 1000

    Viper.Random.Seed(Viper.Time.GetTickCount())
    InitDeck()
    ShuffleDeck()

    continuePlaying = 1
    DO WHILE continuePlaying = 1
        IF g_deck.Count < 20 THEN
            PRINT "Reshuffling deck..."
            InitDeck()
            ShuffleDeck()
        END IF

        PlayRound()

        IF g_chips <= 0 THEN
            PRINT "You're out of chips! Game over."
            continuePlaying = 0
        ELSE
            PRINT ""
            PRINT "Play again? (Y/N): ";
            userInput = Viper.Console.ReadLine()
            IF Viper.String.ToUpper(userInput) <> "Y" THEN
                continuePlaying = 0
            END IF
        END IF
    LOOP

    PRINT ""
    PRINT "Final chips: "; g_chips
    PRINT "Thanks for playing!"
END SUB

' =============================================================================
' MAIN
' =============================================================================

SUB ShowMenu()
    PRINT ""
    PRINT "================================"
    PRINT "   VIPER CARD GAME COLLECTION"
    PRINT "================================"
    PRINT ""
    PRINT "1. Blackjack"
    PRINT "2. Exit"
    PRINT ""
END SUB

SUB Main()
    DIM userInput AS STRING
    DIM choice AS INTEGER
    DIM running AS INTEGER

    running = 1

    DO WHILE running = 1
        ShowMenu()
        PRINT "Select: ";
        userInput = Viper.Console.ReadLine()
        choice = Viper.Convert.ToInt(userInput)

        SELECT CASE choice
            CASE 1
                PlayBlackjack()
            CASE 2
                running = 0
            CASE ELSE
                PRINT "Invalid choice"
        END SELECT
    LOOP

    PRINT ""
    PRINT "Goodbye!"
END SUB

Main()
