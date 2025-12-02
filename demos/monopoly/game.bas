' game.bas - Main game logic for Monopoly
' Handles turn flow, rent, buying, building, cards, etc.

' Main game controller
CLASS MonopolyGame
    ' Game components
    DIM board AS GameBoard
    DIM chanceDeck AS CardDeck
    DIM chestDeck AS CardDeck
    DIM display AS GameDisplay

    ' Players
    DIM players(3) AS Player
    DIM aiBrains(3) AS AIBrain
    DIM numPlayers AS INTEGER
    DIM currentPlayerIdx AS INTEGER

    ' Game state
    DIM turnNumber AS INTEGER
    DIM gameOver AS INTEGER
    DIM winnerIdx AS INTEGER
    DIM housesAvailable AS INTEGER
    DIM hotelsAvailable AS INTEGER
    DIM freeParkingPot AS INTEGER
    DIM lastDie1 AS INTEGER
    DIM lastDie2 AS INTEGER
    DIM doublesCount AS INTEGER

    ' Statistics
    DIM totalRentPaid AS INTEGER
    DIM totalPropertiesBought AS INTEGER
    DIM totalHousesBuilt AS INTEGER

    SUB Init()
        ' Initialize random number generator with current time
        DIM seed AS INTEGER
        seed = INT(RND() * 10000)
        RANDOMIZE seed

        ' Create game components
        board = NEW GameBoard()
        board.Init()

        chanceDeck = NEW CardDeck()
        chanceDeck.InitChance()
        chanceDeck.Shuffle()

        chestDeck = NEW CardDeck()
        chestDeck.InitCommunityChest()
        chestDeck.Shuffle()

        display = NEW GameDisplay()
        display.Init()

        ' Initialize game state
        numPlayers = 4
        currentPlayerIdx = 0
        turnNumber = 0
        gameOver = 0
        winnerIdx = -1
        housesAvailable = TOTAL_HOUSES
        hotelsAvailable = TOTAL_HOTELS
        freeParkingPot = 0
        doublesCount = 0

        ' Statistics
        totalRentPaid = 0
        totalPropertiesBought = 0
        totalHousesBuilt = 0

        Me.SetupPlayers()
    END SUB

    SUB SetupPlayers()
        DIM i AS INTEGER

        ' Create all player objects
        FOR i = 0 TO 3
            players(i) = NEW Player()
            aiBrains(i) = NEW AIBrain()
        NEXT i

        ' Player 1: Human (Orange)
        players(0).Init("You", "O", TOKEN_ORANGE, 0, 0, 0)
        aiBrains(0).Init(STRATEGY_BALANCED)  ' Not used for human

        ' Player 2: Andy (Aggressive - Green)
        players(1).Init("Andy", "A", TOKEN_GREEN, 1, 1, STRATEGY_AGGRESSIVE)
        aiBrains(1).Init(STRATEGY_AGGRESSIVE)

        ' Player 3: Betty (Conservative - Red)
        players(2).Init("Betty", "B", TOKEN_RED, 2, 1, STRATEGY_CONSERVATIVE)
        aiBrains(2).Init(STRATEGY_CONSERVATIVE)

        ' Player 4: Chip (Balanced - Purple)
        players(3).Init("Chip", "C", TOKEN_PURPLE, 3, 1, STRATEGY_BALANCED)
        aiBrains(3).Init(STRATEGY_BALANCED)
    END SUB

    ' Roll two dice
    SUB RollDice()
        lastDie1 = INT(RND() * 6) + 1
        lastDie2 = INT(RND() * 6) + 1
    END SUB

    ' Get total of last dice roll
    FUNCTION GetDiceTotal() AS INTEGER
        GetDiceTotal = lastDie1 + lastDie2
    END FUNCTION

    ' Check if last roll was doubles
    FUNCTION IsDoubles() AS INTEGER
        IsDoubles = 0
        IF lastDie1 = lastDie2 THEN IsDoubles = 1
    END FUNCTION

    ' Main game loop
    SUB RunGame()
        DIM running AS INTEGER
        DIM key AS STRING

        running = 1

        DO WHILE running = 1
            IF gameOver = 1 THEN
                running = 0
            ELSE
                Me.PlayTurn()
                turnNumber = turnNumber + 1

                ' Check for game over
                IF Me.CountActivePlayers() <= 1 THEN
                    gameOver = 1
                    winnerIdx = Me.GetLastActivePlayer()
                END IF
            END IF
        LOOP

        ' Show game over
        IF winnerIdx >= 0 THEN
            display.ShowGameOver(players(winnerIdx).GetName(), Me.CalculateNetWorth(winnerIdx))
            key = display.WaitForKey()
        END IF
    END SUB

    ' Play a single turn
    SUB PlayTurn()
        DIM p AS Player
        DIM isAI AS INTEGER
        DIM inJail AS INTEGER

        p = players(currentPlayerIdx)

        IF p.IsBankrupt() = 1 THEN
            Me.NextPlayer()
            EXIT SUB
        END IF

        ' Draw the board
        display.DrawBoard(board, players, numPlayers)
        display.DrawPlayerStatus(players, numPlayers, currentPlayerIdx)

        isAI = p.IsAI()
        inJail = p.IsInJail()

        doublesCount = 0

        IF inJail = 1 THEN
            Me.HandleJailTurn(currentPlayerIdx)
        ELSE
            Me.HandleNormalTurn(currentPlayerIdx)
        END IF

        Me.NextPlayer()
    END SUB

    ' Handle a turn when player is in jail
    SUB HandleJailTurn(pIdx AS INTEGER)
        DIM p AS Player
        DIM isAI AS INTEGER
        DIM choice AS STRING
        DIM jailTurns AS INTEGER

        p = players(pIdx)
        isAI = p.IsAI()
        jailTurns = p.GetJailTurns()

        display.ShowMessage(p.GetName() + " is in jail (turn " + STR$(jailTurns) + "/" + STR$(MAX_JAIL_TURNS) + ")")
        display.Pause(500)

        IF isAI = 1 THEN
            ' AI jail decision
            IF p.HasJailCard() = 1 THEN
                ' Use get out of jail free card
                p.SetJailCard(0)
                p.ReleaseFromJail()
                display.ShowAIDecision(p.GetName(), "Uses Get Out of Jail Free card", "")
                display.Pause(1000)
                Me.HandleNormalTurn(pIdx)
            ELSE
                IF aiBrains(pIdx).ShouldPayJailFine(p.GetMoney(), turnNumber, jailTurns) = 1 THEN
                    ' Pay fine
                    p.SubtractMoney(JAIL_FINE)
                    p.ReleaseFromJail()
                    display.ShowAIDecision(p.GetName(), "Pays $50 fine", "")
                    display.Pause(1000)
                    Me.HandleNormalTurn(pIdx)
                ELSE
                    ' Try to roll doubles
                    Me.RollDice()
                    display.DrawDiceRoll(lastDie1, lastDie2)
                    display.Pause(1000)

                    IF Me.IsDoubles() = 1 THEN
                        p.ReleaseFromJail()
                        display.ShowMessage(p.GetName() + " rolled doubles and is free!")
                        display.Pause(1000)
                        Me.MoveAndLand(pIdx, Me.GetDiceTotal())
                    ELSE
                        p.SetJailTurns(jailTurns + 1)
                        IF jailTurns >= MAX_JAIL_TURNS THEN
                            p.SubtractMoney(JAIL_FINE)
                            p.ReleaseFromJail()
                            display.ShowMessage(p.GetName() + " must pay $50 and leave jail")
                            display.Pause(1000)
                        END IF
                    END IF
                END IF
            END IF
        ELSE
            ' Human jail decision
            display.ShowMessage("In jail: [P]ay $50, [R]oll for doubles, [C]ard if you have one")
            choice = UCASE$(display.WaitForKey())

            IF choice = "C" THEN
                IF p.HasJailCard() = 1 THEN
                    p.SetJailCard(0)
                    p.ReleaseFromJail()
                    display.ShowMessage("Used Get Out of Jail Free card!")
                    display.Pause(1000)
                    Me.HandleNormalTurn(pIdx)
                ELSE
                    display.ShowError("You don't have a Get Out of Jail Free card!")
                    display.Pause(1000)
                END IF
            END IF

            IF choice = "P" THEN
                IF p.GetMoney() >= JAIL_FINE THEN
                    p.SubtractMoney(JAIL_FINE)
                    p.ReleaseFromJail()
                    display.ShowMessage("Paid $50 fine")
                    display.Pause(1000)
                    Me.HandleNormalTurn(pIdx)
                ELSE
                    display.ShowError("Not enough money!")
                    display.Pause(1000)
                END IF
            END IF

            IF choice = "R" THEN
                Me.RollDice()
                display.DrawDiceRoll(lastDie1, lastDie2)
                display.Pause(1000)

                IF Me.IsDoubles() = 1 THEN
                    p.ReleaseFromJail()
                    display.ShowMessage("Rolled doubles! You're free!")
                    display.Pause(1000)
                    Me.MoveAndLand(pIdx, Me.GetDiceTotal())
                ELSE
                    p.SetJailTurns(jailTurns + 1)
                    IF jailTurns >= MAX_JAIL_TURNS THEN
                        p.SubtractMoney(JAIL_FINE)
                        p.ReleaseFromJail()
                        display.ShowMessage("Forced to pay $50 and leave jail")
                        display.Pause(1000)
                    ELSE
                        display.ShowMessage("No doubles. Still in jail.")
                        display.Pause(1000)
                    END IF
                END IF
            END IF
        END IF
    END SUB

    ' Handle a normal turn (not in jail)
    SUB HandleNormalTurn(pIdx AS INTEGER)
        DIM p AS Player
        DIM isAI AS INTEGER
        DIM rollAgain AS INTEGER
        DIM key AS STRING

        p = players(pIdx)
        isAI = p.IsAI()
        rollAgain = 1

        DO WHILE rollAgain = 1
            rollAgain = 0

            IF isAI = 1 THEN
                display.ShowAIThinking(p.GetName())
                display.Pause(500)
            ELSE
                display.ShowMessage("Press [R] to roll dice, [S] to save, [Q] to quit")
                key = UCASE$(display.WaitForKey())

                IF key = "Q" THEN
                    gameOver = 1
                    EXIT SUB
                END IF

                IF key = "S" THEN
                    Me.SaveGame("monopoly_save.txt")
                    display.ShowMessage("Game saved!")
                    display.Pause(1000)
                    EXIT SUB
                END IF
            END IF

            ' Roll dice
            Me.RollDice()
            display.DrawDiceRoll(lastDie1, lastDie2)
            display.Pause(800)

            IF Me.IsDoubles() = 1 THEN
                doublesCount = doublesCount + 1
                IF doublesCount >= 3 THEN
                    ' Three doubles = go to jail
                    display.ShowMessage("Three doubles! Go to jail!")
                    display.Pause(1000)
                    p.SendToJail()
                    EXIT SUB
                ELSE
                    display.ShowMessage("Doubles! Roll again after this move.")
                    display.Pause(500)
                    rollAgain = 1
                END IF
            END IF

            ' Move and handle landing
            Me.MoveAndLand(pIdx, Me.GetDiceTotal())

            ' Check bankruptcy
            IF p.IsBankrupt() = 1 THEN
                rollAgain = 0
            END IF

            ' Check if still playing (might have gone to jail)
            IF p.IsInJail() = 1 THEN
                rollAgain = 0
            END IF

            ' Update display
            display.DrawBoard(board, players, numPlayers)
            display.DrawPlayerStatus(players, numPlayers, currentPlayerIdx)
        LOOP

        ' Allow building/trading after turn
        IF isAI = 0 THEN
            IF p.IsBankrupt() = 0 THEN
                Me.HandleHumanActions(pIdx)
            END IF
        ELSE
            Me.HandleAIActions(pIdx)
        END IF
    END SUB

    ' Move player and handle landing
    SUB MoveAndLand(pIdx AS INTEGER, spaces AS INTEGER)
        DIM p AS Player
        DIM passedGo AS INTEGER
        DIM newPos AS INTEGER
        DIM sp AS BoardSpace

        p = players(pIdx)
        passedGo = p.MoveBy(spaces)
        newPos = p.GetPosition()

        IF passedGo = 1 THEN
            p.AddMoney(GO_SALARY)
            display.ShowMessage(p.GetName() + " passed GO! Collect $200")
            display.Pause(800)
        END IF

        sp = board.GetSpace(newPos)
        display.ShowMessage(p.GetName() + " landed on " + sp.GetName())
        display.Pause(500)

        Me.HandleLanding(pIdx, newPos)
    END SUB

    ' Handle what happens when landing on a space
    SUB HandleLanding(pIdx AS INTEGER, pos AS INTEGER)
        DIM sp AS BoardSpace
        DIM spType AS INTEGER

        sp = board.GetSpace(pos)
        spType = sp.GetType()

        IF spType = SPACE_GO THEN
            ' Nothing extra, already collected if passed
        END IF

        IF spType = SPACE_PROPERTY THEN
            Me.HandlePropertyLanding(pIdx, pos)
        END IF

        IF spType = SPACE_RAILROAD THEN
            Me.HandlePropertyLanding(pIdx, pos)
        END IF

        IF spType = SPACE_UTILITY THEN
            Me.HandlePropertyLanding(pIdx, pos)
        END IF

        IF spType = SPACE_CHANCE THEN
            Me.HandleChanceCard(pIdx)
        END IF

        IF spType = SPACE_CHEST THEN
            Me.HandleChestCard(pIdx)
        END IF

        IF spType = SPACE_TAX THEN
            Me.HandleTax(pIdx, pos)
        END IF

        IF spType = SPACE_JAIL THEN
            ' Just visiting, nothing happens
        END IF

        IF spType = SPACE_FREEPARKING THEN
            Me.HandleFreeParking(pIdx)
        END IF

        IF spType = SPACE_GOTOJAIL THEN
            display.ShowMessage("Go to Jail!")
            display.Pause(500)
            players(pIdx).SendToJail()
        END IF
    END SUB

    ' Handle landing on a property
    SUB HandlePropertyLanding(pIdx AS INTEGER, pos AS INTEGER)
        DIM p AS Player
        DIM prop AS GameProperty
        DIM propIdx AS INTEGER
        DIM ownerIdx AS INTEGER

        p = players(pIdx)
        propIdx = board.GetPropertyIndexByPosition(pos)

        IF propIdx < 0 THEN EXIT SUB

        prop = board.GetProperty(propIdx)
        ownerIdx = prop.GetOwner()

        IF ownerIdx < 0 THEN
            ' Unowned - offer to buy
            Me.OfferProperty(pIdx, propIdx)
        ELSE
            IF ownerIdx = pIdx THEN
                ' Own property - nothing to pay
                display.ShowMessage("You own this property")
                display.Pause(500)
            ELSE
                ' Pay rent
                IF prop.IsMortgaged() = 0 THEN
                    Me.PayRent(pIdx, ownerIdx, propIdx)
                ELSE
                    display.ShowMessage("Property is mortgaged - no rent due")
                    display.Pause(500)
                END IF
            END IF
        END IF
    END SUB

    ' Offer property for purchase
    SUB OfferProperty(pIdx AS INTEGER, propIdx AS INTEGER)
        DIM p AS Player
        DIM prop AS GameProperty
        DIM cost AS INTEGER
        DIM isAI AS INTEGER
        DIM choice AS STRING

        p = players(pIdx)
        prop = board.GetProperty(propIdx)
        cost = prop.GetCost()
        isAI = p.IsAI()

        display.ShowPropertyDetails(prop, board, "")

        IF isAI = 1 THEN
            display.ShowAIThinking(p.GetName())
            display.Pause(300)

            IF aiBrains(pIdx).ShouldBuy(prop, p.GetMoney(), turnNumber) = 1 THEN
                Me.BuyProperty(pIdx, propIdx)
                display.ShowAIDecision(p.GetName(), "Buys " + prop.GetName(), aiBrains(pIdx).GetComment(1))
            ELSE
                display.ShowAIDecision(p.GetName(), "Declines " + prop.GetName(), "")
                Me.AuctionProperty(propIdx)
            END IF
        ELSE
            IF p.GetMoney() >= cost THEN
                display.ShowMessage("Buy " + prop.GetName() + " for $" + STR$(cost) + "? [Y/N]")
                choice = UCASE$(display.WaitForKey())

                IF choice = "Y" THEN
                    Me.BuyProperty(pIdx, propIdx)
                ELSE
                    Me.AuctionProperty(propIdx)
                END IF
            ELSE
                display.ShowMessage("Not enough money to buy. Starting auction...")
                display.Pause(1000)
                Me.AuctionProperty(propIdx)
            END IF
        END IF
    END SUB

    ' Buy a property
    SUB BuyProperty(pIdx AS INTEGER, propIdx AS INTEGER)
        DIM p AS Player
        DIM prop AS GameProperty

        p = players(pIdx)
        prop = board.GetProperty(propIdx)

        p.SubtractMoney(prop.GetCost())
        p.AddProperty(propIdx)
        prop.SetOwner(pIdx)

        totalPropertiesBought = totalPropertiesBought + 1

        display.ShowMessage(p.GetName() + " bought " + prop.GetName() + " for $" + STR$(prop.GetCost()))
        display.Pause(800)
    END SUB

    ' Auction a property
    SUB AuctionProperty(propIdx AS INTEGER)
        DIM prop AS GameProperty
        DIM highBid AS INTEGER
        DIM highBidder AS INTEGER
        DIM i AS INTEGER
        DIM bid AS INTEGER
        DIM stillBidding AS INTEGER
        DIM bidders(3) AS INTEGER
        DIM numBidders AS INTEGER
        DIM p AS Player
        DIM bidInput AS STRING

        prop = board.GetProperty(propIdx)
        highBid = 0
        highBidder = -1

        ' Initialize bidders
        numBidders = 0
        FOR i = 0 TO numPlayers - 1
            IF players(i).IsBankrupt() = 0 THEN
                bidders(numBidders) = i
                numBidders = numBidders + 1
            END IF
        NEXT i

        display.ShowMessage("Auction for " + prop.GetName() + "! Starting bid: $1")
        display.Pause(500)

        stillBidding = 1
        DO WHILE stillBidding = 1
            stillBidding = 0

            FOR i = 0 TO numBidders - 1
                p = players(bidders(i))

                IF p.GetMoney() > highBid THEN
                    IF p.IsAI() = 1 THEN
                        bid = aiBrains(bidders(i)).GetMaxBid(prop, p.GetMoney())
                        IF bid > highBid THEN
                            ' AI bids slightly above current high
                            bid = highBid + 10
                            IF bid > aiBrains(bidders(i)).GetMaxBid(prop, p.GetMoney()) THEN
                                bid = 0  ' Pass
                            END IF
                        ELSE
                            bid = 0  ' Pass
                        END IF

                        IF bid > 0 THEN
                            highBid = bid
                            highBidder = bidders(i)
                            stillBidding = 1
                            display.ShowAIDecision(p.GetName(), "bids $" + STR$(bid), "")
                            display.Pause(300)
                        END IF
                    ELSE
                        ' Human bidding
                        display.ShowMessage("Current bid: $" + STR$(highBid) + " - Enter your bid (0 to pass): ")
                        bidInput = display.GetInput("")
                        bid = VAL(bidInput)

                        IF bid > highBid THEN
                            IF bid <= p.GetMoney() THEN
                                highBid = bid
                                highBidder = bidders(i)
                                stillBidding = 1
                            END IF
                        END IF
                    END IF
                END IF
            NEXT i
        LOOP

        IF highBidder >= 0 THEN
            p = players(highBidder)
            p.SubtractMoney(highBid)
            p.AddProperty(propIdx)
            prop.SetOwner(highBidder)
            totalPropertiesBought = totalPropertiesBought + 1
            display.ShowMessage(p.GetName() + " wins auction for $" + STR$(highBid))
        ELSE
            display.ShowMessage("No bidders - property remains unsold")
        END IF
        display.Pause(1000)
    END SUB

    ' Pay rent
    SUB PayRent(pIdx AS INTEGER, ownerIdx AS INTEGER, propIdx AS INTEGER)
        DIM p AS Player
        DIM owner AS Player
        DIM prop AS GameProperty
        DIM rent AS INTEGER
        DIM grp AS INTEGER

        p = players(pIdx)
        owner = players(ownerIdx)
        prop = board.GetProperty(propIdx)
        grp = prop.GetGroup()

        IF grp = GRP_RAILROAD THEN
            ' Railroad rent based on number owned
            rent = Me.CalculateRailroadRent(ownerIdx)
        ELSE
            IF grp = GRP_UTILITY THEN
                ' Utility rent based on dice roll
                rent = Me.CalculateUtilityRent(ownerIdx, Me.GetDiceTotal())
            ELSE
                ' Regular property rent
                rent = prop.GetRent(prop.GetHouses())
                ' Double rent for monopoly with no houses
                IF prop.GetHouses() = 0 THEN
                    IF board.PlayerHasMonopoly(ownerIdx, grp) = 1 THEN
                        rent = rent * 2
                    END IF
                END IF
            END IF
        END IF

        display.ShowMessage(p.GetName() + " owes $" + STR$(rent) + " rent to " + owner.GetName())
        display.Pause(500)

        IF p.GetMoney() < rent THEN
            Me.HandleBankruptcy(pIdx, ownerIdx, rent)
        ELSE
            p.SubtractMoney(rent)
            owner.AddMoney(rent)
            totalRentPaid = totalRentPaid + rent

            IF p.IsAI() = 1 THEN
                display.ShowAIDecision(p.GetName(), "pays rent", aiBrains(pIdx).GetComment(2))
            END IF
        END IF
        display.Pause(500)
    END SUB

    ' Calculate railroad rent
    FUNCTION CalculateRailroadRent(ownerIdx AS INTEGER) AS INTEGER
        DIM count AS INTEGER
        count = board.CountRailroads(ownerIdx)

        IF count = 1 THEN CalculateRailroadRent = 25
        IF count = 2 THEN CalculateRailroadRent = 50
        IF count = 3 THEN CalculateRailroadRent = 100
        IF count = 4 THEN CalculateRailroadRent = 200
    END FUNCTION

    ' Calculate utility rent
    FUNCTION CalculateUtilityRent(ownerIdx AS INTEGER, diceRoll AS INTEGER) AS INTEGER
        DIM count AS INTEGER
        DIM mult AS INTEGER

        count = board.CountUtilities(ownerIdx)

        IF count = 1 THEN mult = 4
        IF count = 2 THEN mult = 10

        CalculateUtilityRent = diceRoll * mult
    END FUNCTION

    ' Handle bankruptcy
    SUB HandleBankruptcy(pIdx AS INTEGER, creditorIdx AS INTEGER, amountOwed AS INTEGER)
        DIM p AS Player
        DIM totalAssets AS INTEGER

        p = players(pIdx)
        totalAssets = Me.CalculateNetWorth(pIdx)

        display.ShowMessage(p.GetName() + " cannot pay $" + STR$(amountOwed) + "!")
        display.Pause(500)

        IF totalAssets < amountOwed THEN
            ' Truly bankrupt
            p.SetBankrupt(1)
            display.ShowMessage(p.GetName() + " is BANKRUPT!")
            display.Pause(1000)

            ' Transfer properties to creditor (or bank if creditorIdx < 0)
            Me.TransferAssets(pIdx, creditorIdx)
        ELSE
            ' Need to mortgage/sell to pay
            display.ShowMessage("Must raise $" + STR$(amountOwed - p.GetMoney()) + " through mortgages/sales")
            display.Pause(1000)
            ' For AI, auto-mortgage; for human, prompt
            IF p.IsAI() = 1 THEN
                Me.AIRaiseFunds(pIdx, amountOwed - p.GetMoney())
            ELSE
                Me.HumanRaiseFunds(pIdx, amountOwed - p.GetMoney())
            END IF
        END IF
    END SUB

    ' Transfer assets from bankrupt player
    SUB TransferAssets(fromIdx AS INTEGER, toIdx AS INTEGER)
        DIM i AS INTEGER
        DIM prop AS GameProperty

        FOR i = 0 TO 27
            prop = board.GetProperty(i)
            IF prop.GetOwner() = fromIdx THEN
                IF toIdx >= 0 THEN
                    prop.SetOwner(toIdx)
                    players(toIdx).AddProperty(i)
                ELSE
                    ' Return to bank
                    prop.SetOwner(-1)
                    prop.SetMortgaged(0)
                    prop.SetHouses(0)
                END IF
            END IF
        NEXT i
    END SUB

    ' AI raises funds through mortgaging
    SUB AIRaiseFunds(pIdx AS INTEGER, needed AS INTEGER)
        DIM p AS Player
        DIM i AS INTEGER
        DIM prop AS GameProperty

        p = players(pIdx)

        FOR i = 0 TO 27
            IF p.GetMoney() >= needed THEN EXIT FOR

            prop = board.GetProperty(i)
            IF prop.GetOwner() = pIdx THEN
                IF aiBrains(pIdx).ShouldMortgage(prop, p.GetMoney(), needed) = 1 THEN
                    prop.SetMortgaged(1)
                    p.AddMoney(prop.GetMortgageValue())
                    display.ShowAIDecision(p.GetName(), "mortgages " + prop.GetName(), "")
                    display.Pause(300)
                END IF
            END IF
        NEXT i
    END SUB

    ' Human raises funds
    SUB HumanRaiseFunds(pIdx AS INTEGER, needed AS INTEGER)
        DIM p AS Player
        DIM choice AS STRING
        DIM propNum AS STRING
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty

        p = players(pIdx)

        DO WHILE p.GetMoney() < needed
            display.ShowMessage("Need $" + STR$(needed) + " more. [M]ortgage property, or [B]ankrupt")
            choice = UCASE$(display.WaitForKey())

            IF choice = "B" THEN
                p.SetBankrupt(1)
                EXIT SUB
            END IF

            IF choice = "M" THEN
                display.ShowMessage("Enter property number to mortgage (0-27): ")
                propNum = display.GetInput("")
                propIdx = VAL(propNum)

                IF propIdx >= 0 THEN
                    IF propIdx <= 27 THEN
                        prop = board.GetProperty(propIdx)
                        IF prop.GetOwner() = pIdx THEN
                            IF prop.IsMortgaged() = 0 THEN
                                IF prop.GetHouses() = 0 THEN
                                    prop.SetMortgaged(1)
                                    p.AddMoney(prop.GetMortgageValue())
                                    display.ShowMessage("Mortgaged " + prop.GetName() + " for $" + STR$(prop.GetMortgageValue()))
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        LOOP
    END SUB

    ' Handle Chance card
    SUB HandleChanceCard(pIdx AS INTEGER)
        DIM cardIdx AS INTEGER
        DIM action AS INTEGER
        DIM value AS INTEGER
        DIM dest AS INTEGER

        cardIdx = chanceDeck.DrawCard()
        display.ShowCard("CHANCE", chanceDeck.GetCardText(cardIdx))
        display.Pause(1500)

        action = chanceDeck.GetCardAction(cardIdx)
        value = chanceDeck.GetCardValue(cardIdx)
        dest = chanceDeck.GetCardDest(cardIdx)

        Me.ExecuteCardAction(pIdx, action, value, dest, chanceDeck.GetCardValue2(cardIdx))
    END SUB

    ' Handle Community Chest card
    SUB HandleChestCard(pIdx AS INTEGER)
        DIM cardIdx AS INTEGER
        DIM action AS INTEGER
        DIM value AS INTEGER
        DIM dest AS INTEGER

        cardIdx = chestDeck.DrawCard()
        display.ShowCard("COMMUNITY CHEST", chestDeck.GetCardText(cardIdx))
        display.Pause(1500)

        action = chestDeck.GetCardAction(cardIdx)
        value = chestDeck.GetCardValue(cardIdx)
        dest = chestDeck.GetCardDest(cardIdx)

        Me.ExecuteCardAction(pIdx, action, value, dest, chestDeck.GetCardValue2(cardIdx))
    END SUB

    ' Execute a card action
    SUB ExecuteCardAction(pIdx AS INTEGER, action AS INTEGER, value AS INTEGER, dest AS INTEGER, value2 AS INTEGER)
        DIM p AS Player
        DIM i AS INTEGER
        DIM passedGo AS INTEGER
        DIM newPos AS INTEGER
        DIM repairCost AS INTEGER

        p = players(pIdx)

        IF action = CARD_MOVE_TO THEN
            passedGo = p.MoveTo(dest, 1)
            IF passedGo = 1 THEN
                p.AddMoney(GO_SALARY)
            END IF
            IF value > 0 THEN
                p.AddMoney(value)
            END IF
            Me.HandleLanding(pIdx, dest)
        END IF

        IF action = CARD_MOVE_BACK THEN
            newPos = p.GetPosition() - value
            IF newPos < 0 THEN newPos = newPos + 40
            p.SetPosition(newPos)
            Me.HandleLanding(pIdx, newPos)
        END IF

        IF action = CARD_COLLECT THEN
            p.AddMoney(value)
            display.ShowMessage(p.GetName() + " collects $" + STR$(value))
            display.Pause(500)
        END IF

        IF action = CARD_PAY THEN
            p.SubtractMoney(value)
            freeParkingPot = freeParkingPot + value
            display.ShowMessage(p.GetName() + " pays $" + STR$(value))
            display.Pause(500)
        END IF

        IF action = CARD_PAY_EACH THEN
            FOR i = 0 TO numPlayers - 1
                IF i <> pIdx THEN
                    IF players(i).IsBankrupt() = 0 THEN
                        p.SubtractMoney(value)
                        players(i).AddMoney(value)
                    END IF
                END IF
            NEXT i
        END IF

        IF action = CARD_COLLECT_EACH THEN
            FOR i = 0 TO numPlayers - 1
                IF i <> pIdx THEN
                    IF players(i).IsBankrupt() = 0 THEN
                        players(i).SubtractMoney(value)
                        p.AddMoney(value)
                    END IF
                END IF
            NEXT i
        END IF

        IF action = CARD_REPAIRS THEN
            repairCost = Me.CalculateRepairs(pIdx, value, value2)
            p.SubtractMoney(repairCost)
            display.ShowMessage(p.GetName() + " pays $" + STR$(repairCost) + " for repairs")
            display.Pause(500)
        END IF

        IF action = CARD_JAIL THEN
            p.SendToJail()
        END IF

        IF action = CARD_JAIL_FREE THEN
            p.SetJailCard(1)
            display.ShowMessage(p.GetName() + " receives Get Out of Jail Free card")
            display.Pause(500)
        END IF

        IF action = CARD_NEAREST_RR THEN
            newPos = board.FindNearestRailroad(p.GetPosition())
            passedGo = p.MoveTo(newPos, 1)
            IF passedGo = 1 THEN p.AddMoney(GO_SALARY)
            Me.HandleLanding(pIdx, newPos)
        END IF

        IF action = CARD_NEAREST_UTIL THEN
            newPos = board.FindNearestUtility(p.GetPosition())
            passedGo = p.MoveTo(newPos, 1)
            IF passedGo = 1 THEN p.AddMoney(GO_SALARY)
            Me.HandleLanding(pIdx, newPos)
        END IF
    END SUB

    ' Calculate repair costs
    FUNCTION CalculateRepairs(pIdx AS INTEGER, houseCost AS INTEGER, hotelCost AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM prop AS GameProperty
        DIM houses AS INTEGER
        DIM total AS INTEGER

        total = 0
        FOR i = 0 TO 27
            prop = board.GetProperty(i)
            IF prop.GetOwner() = pIdx THEN
                houses = prop.GetHouses()
                IF houses = 5 THEN
                    total = total + hotelCost
                ELSE
                    total = total + houses * houseCost
                END IF
            END IF
        NEXT i

        CalculateRepairs = total
    END FUNCTION

    ' Handle tax spaces
    SUB HandleTax(pIdx AS INTEGER, pos AS INTEGER)
        DIM p AS Player
        DIM tax AS INTEGER

        p = players(pIdx)

        IF pos = 4 THEN
            ' Income Tax
            tax = INCOME_TAX
        ELSE
            ' Luxury Tax
            tax = LUXURY_TAX
        END IF

        p.SubtractMoney(tax)
        freeParkingPot = freeParkingPot + tax
        display.ShowMessage(p.GetName() + " pays $" + STR$(tax) + " tax")
        display.Pause(500)
    END SUB

    ' Handle Free Parking
    SUB HandleFreeParking(pIdx AS INTEGER)
        DIM p AS Player

        p = players(pIdx)

        IF freeParkingPot > 0 THEN
            p.AddMoney(freeParkingPot)
            display.ShowMessage(p.GetName() + " collects $" + STR$(freeParkingPot) + " from Free Parking!")
            freeParkingPot = 0
        ELSE
            display.ShowMessage("Free Parking - just resting")
        END IF
        display.Pause(500)
    END SUB

    ' Handle human player actions after rolling
    SUB HandleHumanActions(pIdx AS INTEGER)
        DIM p AS Player
        DIM done AS INTEGER
        DIM key AS STRING

        p = players(pIdx)
        done = 0

        DO WHILE done = 0
            display.DrawBoard(board, players, numPlayers)
            display.DrawPlayerStatus(players, numPlayers, currentPlayerIdx)
            display.ShowMessage("Actions: [H]ouse, [T]rade, [M]ortgage, [U]nmortgage, [D]one")
            key = UCASE$(display.WaitForKey())

            IF key = "D" THEN done = 1

            IF key = "H" THEN
                Me.BuildHouse(pIdx)
            END IF

            IF key = "M" THEN
                Me.MortgageProperty(pIdx)
            END IF

            IF key = "U" THEN
                Me.UnmortgageProperty(pIdx)
            END IF

            IF key = "T" THEN
                Me.InitiateTrade(pIdx)
            END IF
        LOOP
    END SUB

    ' Handle AI actions after rolling
    SUB HandleAIActions(pIdx AS INTEGER)
        DIM p AS Player
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty
        DIM canBuild AS INTEGER
        DIM built AS INTEGER

        p = players(pIdx)

        ' Try to build houses
        built = 1
        DO WHILE built = 1
            built = 0
            propIdx = Me.GetAIBuildTarget(pIdx)
            IF propIdx >= 0 THEN
                prop = board.GetProperty(propIdx)
                IF Me.CanBuildOnProperty(pIdx, propIdx) = 1 THEN
                    IF aiBrains(pIdx).ShouldBuild(prop, p.GetMoney(), board.PlayerHasMonopoly(pIdx, prop.GetGroup())) = 1 THEN
                        Me.DoBuildHouse(pIdx, propIdx)
                        built = 1
                        display.ShowAIDecision(p.GetName(), "builds on " + prop.GetName(), aiBrains(pIdx).GetComment(3))
                        display.Pause(300)
                    END IF
                END IF
            END IF
        LOOP
    END SUB

    ' Get best property for AI to build on
    FUNCTION GetAIBuildTarget(pIdx AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM prop AS GameProperty
        DIM minHouses AS INTEGER
        DIM bestIdx AS INTEGER
        DIM grp AS INTEGER

        bestIdx = -1
        minHouses = 99

        FOR i = 0 TO 27
            prop = board.GetProperty(i)
            IF prop.GetOwner() = pIdx THEN
                grp = prop.GetGroup()
                IF board.PlayerHasMonopoly(pIdx, grp) = 1 THEN
                    IF prop.CanBuild() = 1 THEN
                        IF prop.GetHouses() < minHouses THEN
                            minHouses = prop.GetHouses()
                            bestIdx = i
                        END IF
                    END IF
                END IF
            END IF
        NEXT i

        GetAIBuildTarget = bestIdx
    END FUNCTION

    ' Check if can build on property
    FUNCTION CanBuildOnProperty(pIdx AS INTEGER, propIdx AS INTEGER) AS INTEGER
        DIM prop AS GameProperty
        DIM grp AS INTEGER
        DIM houseCost AS INTEGER
        DIM minHouses AS INTEGER
        DIM maxHouses AS INTEGER
        DIM i AS INTEGER
        DIM p AS GameProperty

        prop = board.GetProperty(propIdx)
        grp = prop.GetGroup()

        CanBuildOnProperty = 0

        IF prop.GetOwner() <> pIdx THEN EXIT FUNCTION
        IF prop.CanBuild() = 0 THEN EXIT FUNCTION
        IF board.PlayerHasMonopoly(pIdx, grp) = 0 THEN EXIT FUNCTION

        houseCost = prop.GetHouseCost()
        IF players(pIdx).GetMoney() < houseCost THEN EXIT FUNCTION

        ' Check even building rule
        minHouses = 99
        maxHouses = 0
        FOR i = 0 TO 27
            p = board.GetProperty(i)
            IF p.GetOwner() = pIdx THEN
                IF p.GetGroup() = grp THEN
                    IF p.GetHouses() < minHouses THEN minHouses = p.GetHouses()
                    IF p.GetHouses() > maxHouses THEN maxHouses = p.GetHouses()
                END IF
            END IF
        NEXT i

        ' Can only build if this property has minimum houses in group
        IF prop.GetHouses() = minHouses THEN
            ' Check house/hotel availability
            IF prop.GetHouses() = 4 THEN
                IF hotelsAvailable > 0 THEN CanBuildOnProperty = 1
            ELSE
                IF housesAvailable > 0 THEN CanBuildOnProperty = 1
            END IF
        END IF
    END FUNCTION

    ' Build a house (human)
    SUB BuildHouse(pIdx AS INTEGER)
        DIM propNum AS STRING
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty

        display.ShowMessage("Enter property number to build on (0-27): ")
        propNum = display.GetInput("")
        propIdx = VAL(propNum)

        IF propIdx >= 0 THEN
            IF propIdx <= 27 THEN
                IF Me.CanBuildOnProperty(pIdx, propIdx) = 1 THEN
                    Me.DoBuildHouse(pIdx, propIdx)
                ELSE
                    display.ShowError("Cannot build there")
                    display.Pause(1000)
                END IF
            END IF
        END IF
    END SUB

    ' Actually build a house
    SUB DoBuildHouse(pIdx AS INTEGER, propIdx AS INTEGER)
        DIM p AS Player
        DIM prop AS GameProperty
        DIM houses AS INTEGER

        p = players(pIdx)
        prop = board.GetProperty(propIdx)
        houses = prop.GetHouses()

        p.SubtractMoney(prop.GetHouseCost())

        IF houses = 4 THEN
            ' Building hotel
            prop.SetHouses(5)
            housesAvailable = housesAvailable + 4
            hotelsAvailable = hotelsAvailable - 1
        ELSE
            prop.SetHouses(houses + 1)
            housesAvailable = housesAvailable - 1
        END IF

        totalHousesBuilt = totalHousesBuilt + 1
    END SUB

    ' Mortgage property (human)
    SUB MortgageProperty(pIdx AS INTEGER)
        DIM propNum AS STRING
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty

        display.ShowMessage("Enter property number to mortgage (0-27): ")
        propNum = display.GetInput("")
        propIdx = VAL(propNum)

        IF propIdx >= 0 THEN
            IF propIdx <= 27 THEN
                prop = board.GetProperty(propIdx)
                IF prop.GetOwner() = pIdx THEN
                    IF prop.IsMortgaged() = 0 THEN
                        IF prop.GetHouses() = 0 THEN
                            prop.SetMortgaged(1)
                            players(pIdx).AddMoney(prop.GetMortgageValue())
                            display.ShowMessage("Mortgaged for $" + STR$(prop.GetMortgageValue()))
                            display.Pause(500)
                        ELSE
                            display.ShowError("Must sell houses first")
                            display.Pause(1000)
                        END IF
                    END IF
                END IF
            END IF
        END IF
    END SUB

    ' Unmortgage property
    SUB UnmortgageProperty(pIdx AS INTEGER)
        DIM propNum AS STRING
        DIM propIdx AS INTEGER
        DIM prop AS GameProperty
        DIM cost AS INTEGER

        display.ShowMessage("Enter property number to unmortgage (0-27): ")
        propNum = display.GetInput("")
        propIdx = VAL(propNum)

        IF propIdx >= 0 THEN
            IF propIdx <= 27 THEN
                prop = board.GetProperty(propIdx)
                IF prop.GetOwner() = pIdx THEN
                    IF prop.IsMortgaged() = 1 THEN
                        cost = prop.GetMortgageValue() + prop.GetMortgageValue() / 10
                        IF players(pIdx).GetMoney() >= cost THEN
                            prop.SetMortgaged(0)
                            players(pIdx).SubtractMoney(cost)
                            display.ShowMessage("Unmortgaged for $" + STR$(cost))
                            display.Pause(500)
                        ELSE
                            display.ShowError("Not enough money")
                            display.Pause(1000)
                        END IF
                    END IF
                END IF
            END IF
        END IF
    END SUB

    ' Initiate a trade
    SUB InitiateTrade(pIdx AS INTEGER)
        DIM targetNum AS STRING
        DIM targetIdx AS INTEGER
        DIM offerProp AS STRING
        DIM wantProp AS STRING
        DIM offerIdx AS INTEGER
        DIM wantIdx AS INTEGER
        DIM offerCash AS STRING
        DIM wantCash AS STRING
        DIM oCash AS INTEGER
        DIM wCash AS INTEGER

        display.ShowMessage("Trade with which player (1-3)? ")
        targetNum = display.GetInput("")
        targetIdx = VAL(targetNum)

        IF targetIdx < 1 THEN EXIT SUB
        IF targetIdx > 3 THEN EXIT SUB
        IF players(targetIdx).IsBankrupt() = 1 THEN EXIT SUB

        display.ShowMessage("Your property to offer (prop # or -1 for none): ")
        offerProp = display.GetInput("")
        offerIdx = VAL(offerProp)

        display.ShowMessage("Property you want (prop # or -1 for none): ")
        wantProp = display.GetInput("")
        wantIdx = VAL(wantProp)

        display.ShowMessage("Cash you offer: ")
        offerCash = display.GetInput("")
        oCash = VAL(offerCash)

        display.ShowMessage("Cash you want: ")
        wantCash = display.GetInput("")
        wCash = VAL(wantCash)

        Me.ProcessTrade(pIdx, targetIdx, offerIdx, wantIdx, oCash, wCash)
    END SUB

    ' Process a trade offer
    SUB ProcessTrade(fromIdx AS INTEGER, toIdx AS INTEGER, offerProp AS INTEGER, wantProp AS INTEGER, offerCash AS INTEGER, wantCash AS INTEGER)
        DIM target AS Player
        DIM giveValue AS INTEGER
        DIM getValue AS INTEGER
        DIM completesMonopoly AS INTEGER
        DIM accepted AS INTEGER
        DIM prop AS GameProperty

        target = players(toIdx)

        ' Calculate trade values
        giveValue = wantCash
        getValue = offerCash

        IF offerProp >= 0 THEN
            prop = board.GetProperty(offerProp)
            getValue = getValue + prop.GetCost()
        END IF

        IF wantProp >= 0 THEN
            prop = board.GetProperty(wantProp)
            giveValue = giveValue + prop.GetCost()
        END IF

        ' Check if trade completes a monopoly for target
        completesMonopoly = 0
        IF offerProp >= 0 THEN
            prop = board.GetProperty(offerProp)
            ' Simplified check - would need full monopoly analysis
        END IF

        IF target.IsAI() = 1 THEN
            accepted = aiBrains(toIdx).ShouldAcceptTrade(giveValue, getValue, completesMonopoly)
            IF accepted = 1 THEN
                display.ShowMessage(target.GetName() + " accepts the trade!")
                Me.ExecuteTrade(fromIdx, toIdx, offerProp, wantProp, offerCash, wantCash)
            ELSE
                display.ShowMessage(target.GetName() + " declines the trade.")
            END IF
        ELSE
            display.ShowMessage("Accept trade? [Y/N]")
            IF UCASE$(display.WaitForKey()) = "Y" THEN
                Me.ExecuteTrade(fromIdx, toIdx, offerProp, wantProp, offerCash, wantCash)
            END IF
        END IF
        display.Pause(1000)
    END SUB

    ' Execute an accepted trade
    SUB ExecuteTrade(fromIdx AS INTEGER, toIdx AS INTEGER, offerProp AS INTEGER, wantProp AS INTEGER, offerCash AS INTEGER, wantCash AS INTEGER)
        DIM prop AS GameProperty

        ' Transfer properties
        IF offerProp >= 0 THEN
            prop = board.GetProperty(offerProp)
            prop.SetOwner(toIdx)
            players(fromIdx).RemoveProperty(offerProp)
            players(toIdx).AddProperty(offerProp)
        END IF

        IF wantProp >= 0 THEN
            prop = board.GetProperty(wantProp)
            prop.SetOwner(fromIdx)
            players(toIdx).RemoveProperty(wantProp)
            players(fromIdx).AddProperty(wantProp)
        END IF

        ' Transfer cash
        players(fromIdx).SubtractMoney(offerCash)
        players(fromIdx).AddMoney(wantCash)
        players(toIdx).AddMoney(offerCash)
        players(toIdx).SubtractMoney(wantCash)

        display.ShowMessage("Trade completed!")
    END SUB

    ' Move to next player
    SUB NextPlayer()
        currentPlayerIdx = currentPlayerIdx + 1
        IF currentPlayerIdx >= numPlayers THEN
            currentPlayerIdx = 0
        END IF

        ' Skip bankrupt players
        DIM skipped AS INTEGER
        skipped = 0
        DO WHILE players(currentPlayerIdx).IsBankrupt() = 1
            currentPlayerIdx = currentPlayerIdx + 1
            IF currentPlayerIdx >= numPlayers THEN
                currentPlayerIdx = 0
            END IF
            skipped = skipped + 1
            IF skipped >= numPlayers THEN EXIT DO
        LOOP
    END SUB

    ' Count active (non-bankrupt) players
    FUNCTION CountActivePlayers() AS INTEGER
        DIM cnt AS INTEGER
        DIM i AS INTEGER

        cnt = 0
        FOR i = 0 TO numPlayers - 1
            IF players(i).IsBankrupt() = 0 THEN
                cnt = cnt + 1
            END IF
        NEXT i

        CountActivePlayers = cnt
    END FUNCTION

    ' Get last active player
    FUNCTION GetLastActivePlayer() AS INTEGER
        DIM i AS INTEGER

        FOR i = 0 TO numPlayers - 1
            IF players(i).IsBankrupt() = 0 THEN
                GetLastActivePlayer = i
                EXIT FUNCTION
            END IF
        NEXT i

        GetLastActivePlayer = -1
    END FUNCTION

    ' Calculate net worth of a player
    FUNCTION CalculateNetWorth(pIdx AS INTEGER) AS INTEGER
        DIM total AS INTEGER
        DIM i AS INTEGER
        DIM prop AS GameProperty
        DIM p AS Player

        p = players(pIdx)
        total = p.GetMoney()

        FOR i = 0 TO 27
            prop = board.GetProperty(i)
            IF prop.GetOwner() = pIdx THEN
                IF prop.IsMortgaged() = 1 THEN
                    total = total + prop.GetMortgageValue()
                ELSE
                    total = total + prop.GetCost()
                    total = total + prop.GetHouses() * prop.GetHouseCost()
                END IF
            END IF
        NEXT i

        CalculateNetWorth = total
    END FUNCTION

    ' Save game to file using string concatenation
    SUB SaveGame(filename AS STRING)
        DIM saveData AS STRING
        DIM i AS INTEGER
        DIM p AS Player
        DIM prop AS GameProperty
        DIM nl AS STRING

        nl = CHR$(10)

        ' Save game state as comma-separated values
        saveData = STR$(turnNumber) + "," + STR$(currentPlayerIdx) + ","
        saveData = saveData + STR$(housesAvailable) + "," + STR$(hotelsAvailable) + ","
        saveData = saveData + STR$(freeParkingPot) + nl

        ' Save players (one per line)
        FOR i = 0 TO numPlayers - 1
            p = players(i)
            saveData = saveData + STR$(p.GetPosition()) + ","
            saveData = saveData + STR$(p.GetMoney()) + ","
            saveData = saveData + STR$(p.GetJailTurns()) + ","
            saveData = saveData + STR$(p.HasJailCard()) + ","
            saveData = saveData + STR$(p.IsBankrupt()) + nl
        NEXT i

        ' Save properties (one per line)
        FOR i = 0 TO 27
            prop = board.GetProperty(i)
            saveData = saveData + STR$(prop.GetOwner()) + ","
            saveData = saveData + STR$(prop.GetHouses()) + ","
            saveData = saveData + STR$(prop.IsMortgaged()) + nl
        NEXT i

        ' Write to file
        Viper.IO.File.WriteAllText(filename, saveData)
    END SUB

    ' Load game stub - file reading works but parsing is simplified
    SUB LoadGame(filename AS STRING)
        DIM content AS STRING
        DIM exists AS INTEGER

        exists = Viper.IO.File.Exists(filename)
        IF exists = 0 THEN
            display.ShowMessage("Save file not found")
            EXIT SUB
        END IF

        content = Viper.IO.File.ReadAllText(filename)
        display.ShowMessage("File loaded: " + STR$(LEN(content)) + " bytes")
    END SUB
END CLASS

