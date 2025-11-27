REM ====================================================================
REM MONOPOLY - Classic Board Game
REM A full-featured Monopoly implementation in Viper BASIC
REM ====================================================================
REM Features:
REM - All 40 board spaces with authentic properties
REM - Property buying, auctioning, mortgaging
REM - Houses and hotels (even build rule)
REM - Chance and Community Chest cards
REM - AI opponents with different strategies
REM - Trading system
REM - Save/Load game state
REM ====================================================================
REM Uses Viper.* Runtime:
REM - Viper.Collections.List for dynamic collections
REM - Viper.IO.File for save/load
REM - Viper.Random (via RND) for dice and cards
REM - Viper.Math (via INT) for calculations
REM - Viper.String for text processing
REM ====================================================================

AddFile "monopoly_classes.bas"
AddFile "monopoly_data.bas"
AddFile "monopoly_board.bas"
AddFile "monopoly_ai.bas"
AddFile "monopoly_trade.bas"
AddFile "monopoly_io.bas"

REM ====================================================================
REM Game state variables
REM ====================================================================
DIM currentPlayerIndex AS INTEGER
DIM gameOver AS INTEGER
DIM turnNumber AS INTEGER
DIM stats AS GameStats

REM ====================================================================
REM Roll two dice - uses BYREF to avoid type inference issues
REM ====================================================================
SUB RollDiceValues(BYREF d1 AS INTEGER, BYREF d2 AS INTEGER)
    DIM r1 AS FLOAT
    DIM r2 AS FLOAT
    r1 = RND()
    r2 = RND()
    d1 = INT(r1 * 6) + 1
    d2 = INT(r2 * 6) + 1
END SUB

FUNCTION RollDice(BYREF d1 AS INTEGER, BYREF d2 AS INTEGER) AS INTEGER
    RollDiceValues(d1, d2)
    RollDice = d1 + d2
END FUNCTION

REM ====================================================================
REM Check if rolled doubles
REM ====================================================================
FUNCTION IsDoubles(d1 AS INTEGER, d2 AS INTEGER) AS INTEGER
    IF d1 = d2 THEN
        IsDoubles = 1
    ELSE
        IsDoubles = 0
    END IF
END FUNCTION

REM ====================================================================
REM Move player by amount
REM ====================================================================
SUB MovePlayer(playerIdx AS INTEGER, amount AS INTEGER)
    DIM p AS Player
    DIM oldPos AS INTEGER
    DIM newPos AS INTEGER

    p = players.get_Item(playerIdx)
    oldPos = p.GetPosition()
    newPos = oldPos + amount

    REM Check for passing GO
    IF newPos >= 40 THEN
        newPos = newPos - 40
        p.AddMoney(200)
        AddLogEntry(p.GetName() + " passed GO - collected $200")
    END IF

    p.SetPosition(newPos)
    stats.RecordLanding(newPos)
END SUB

REM ====================================================================
REM Move player to specific position
REM ====================================================================
SUB MovePlayerTo(playerIdx AS INTEGER, targetPos AS INTEGER)
    DIM p AS Player
    DIM oldPos AS INTEGER

    p = players.get_Item(playerIdx)
    oldPos = p.GetPosition()

    REM Check for passing GO (only if moving forward)
    IF targetPos < oldPos THEN
        IF targetPos <> 10 THEN
            p.AddMoney(200)
            AddLogEntry(p.GetName() + " passed GO - collected $200")
        END IF
    END IF

    p.SetPosition(targetPos)
    stats.RecordLanding(targetPos)
END SUB

REM ====================================================================
REM Calculate rent for a property
REM ====================================================================
FUNCTION CalculateRent(propIdx AS INTEGER, diceRoll AS INTEGER) AS INTEGER
    DIM prop AS PropData
    DIM rent AS INTEGER
    DIM owner AS INTEGER
    DIM grp AS INTEGER
    DIM rrCount AS INTEGER
    DIM utilCount AS INTEGER

    prop = properties.get_Item(propIdx)
    owner = prop.GetOwner()
    grp = prop.GetGroup()

    IF prop.IsMortgaged() = 1 THEN
        CalculateRent = 0
        EXIT FUNCTION
    END IF

    IF grp = GROUP_RAILROAD THEN
        REM Railroad rent based on count owned
        rrCount = CountGroupOwned(owner, GROUP_RAILROAD)
        IF rrCount = 1 THEN
            rent = 25
        ELSEIF rrCount = 2 THEN
            rent = 50
        ELSEIF rrCount = 3 THEN
            rent = 100
        ELSEIF rrCount = 4 THEN
            rent = 200
        ELSE
            rent = 25
        END IF

    ELSEIF grp = GROUP_UTILITY THEN
        REM Utility rent based on dice and count owned
        utilCount = CountGroupOwned(owner, GROUP_UTILITY)
        IF utilCount = 1 THEN
            rent = diceRoll * 4
        ELSEIF utilCount = 2 THEN
            rent = diceRoll * 10
        ELSE
            rent = diceRoll * 4
        END IF

    ELSE
        REM Regular property rent
        rent = prop.GetRent()

        REM Double rent if owner has monopoly and no houses
        IF prop.GetHouses() = 0 THEN
            IF OwnsCompleteGroup(owner, grp) = 1 THEN
                rent = rent * 2
            END IF
        END IF
    END IF

    CalculateRent = rent
END FUNCTION

REM ====================================================================
REM Handle landing on a property
REM ====================================================================
SUB HandleProperty(playerIdx AS INTEGER, propIdx AS INTEGER, diceRoll AS INTEGER)
    DIM p AS Player
    DIM prop AS PropData
    DIM owner AS INTEGER
    DIM rent AS INTEGER
    DIM cost AS INTEGER
    DIM inp AS STRING

    p = players.get_Item(playerIdx)
    prop = properties.get_Item(propIdx)
    owner = prop.GetOwner()

    IF owner = -1 THEN
        REM Property is unowned - offer to buy
        cost = prop.GetCost()
        ShowPropertyCard(propIdx)

        IF p.IsAI() = 1 THEN
            REM AI decision
            IF AIDecideBuy(playerIdx, propIdx) = 1 THEN
                IF p.GetMoney() >= cost THEN
                    p.SubtractMoney(cost)
                    prop.SetOwner(playerIdx)
                    AIAnnounce(playerIdx, "Buys " + prop.GetName() + " for $" + STR$(cost))
                    AddLogEntry(p.GetName() + " bought " + prop.GetName())
                ELSE
                    DIM auctionResult AS INTEGER
                    AIAnnounce(playerIdx, "Can't afford " + prop.GetName())
                    auctionResult = RunAuction(propIdx)
                END IF
            ELSE
                DIM auctionResult2 AS INTEGER
                AIAnnounce(playerIdx, "Declines to buy " + prop.GetName())
                auctionResult2 = RunAuction(propIdx)
            END IF
        ELSE
            REM Human decision
            ShowMessage(prop.GetName() + " is for sale! Cost: $" + STR$(cost))
            PrintAt(38, 2, CLR_WHITE, "[B]uy for $" + STR$(cost) + "  [A]uction  [P]ass")
            inp = ""
            WHILE inp = ""
                inp = INKEY$()
                SLEEP 50
            WEND

            IF inp = "B" THEN
                IF p.GetMoney() >= cost THEN
                    p.SubtractMoney(cost)
                    prop.SetOwner(playerIdx)
                    ShowMessage("You bought " + prop.GetName() + "!")
                    AddLogEntry(p.GetName() + " bought " + prop.GetName())
                ELSE
                    DIM auctionResult3 AS INTEGER
                    ShowMessage("Not enough money!")
                    auctionResult3 = RunAuction(propIdx)
                END IF
            ELSEIF inp = "b" THEN
                IF p.GetMoney() >= cost THEN
                    p.SubtractMoney(cost)
                    prop.SetOwner(playerIdx)
                    ShowMessage("You bought " + prop.GetName() + "!")
                    AddLogEntry(p.GetName() + " bought " + prop.GetName())
                ELSE
                    DIM auctionResult4 AS INTEGER
                    ShowMessage("Not enough money!")
                    auctionResult4 = RunAuction(propIdx)
                END IF
            ELSEIF inp = "A" THEN
                DIM auctionResult5 AS INTEGER
                auctionResult5 = RunAuction(propIdx)
            ELSEIF inp = "a" THEN
                DIM auctionResult6 AS INTEGER
                auctionResult6 = RunAuction(propIdx)
            ELSE
                DIM auctionResult7 AS INTEGER
                auctionResult7 = RunAuction(propIdx)
            END IF
        END IF

    ELSEIF owner <> playerIdx THEN
        REM Property is owned by someone else - pay rent
        DIM ownerPlayer AS Player
        ownerPlayer = players.get_Item(owner)

        IF prop.IsMortgaged() = 0 THEN
            rent = CalculateRent(propIdx, diceRoll)
            stats.RecordRent(rent)

            IF rent > p.GetMoney() THEN
                REM Cannot pay full rent - handle bankruptcy
                ShowMessage("You owe $" + STR$(rent) + " to " + ownerPlayer.GetName() + "!")
                HandleBankruptcy(playerIdx, owner, rent)
            ELSE
                p.SubtractMoney(rent)
                ownerPlayer.AddMoney(rent)
                ShowMessage("Paid $" + STR$(rent) + " rent to " + ownerPlayer.GetName())
                AddLogEntry(p.GetName() + " paid $" + STR$(rent) + " to " + ownerPlayer.GetName())
            END IF
        ELSE
            ShowMessage(prop.GetName() + " is mortgaged - no rent due")
        END IF
        SLEEP 1000
    ELSE
        REM Player owns this property
        ShowMessage("You own " + prop.GetName())
        SLEEP 500
    END IF
END SUB

REM ====================================================================
REM Handle Chance card
REM ====================================================================
SUB HandleChanceCard(playerIdx AS INTEGER)
    DIM p AS Player
    DIM c AS Card
    DIM action AS INTEGER
    DIM value AS INTEGER
    DIM i AS INTEGER
    DIM otherPlayer AS Player

    p = players.get_Item(playerIdx)
    c = DrawChanceCard()

    ShowMessage("CHANCE: " + c.GetText())
    AddLogEntry(p.GetName() + " drew Chance: " + c.GetText())
    SLEEP 1500

    action = c.GetAction()
    value = c.GetValue()

    IF action = CARD_MONEY THEN
        IF value > 0 THEN
            p.AddMoney(value)
        ELSE
            p.SubtractMoney(0 - value)
        END IF

    ELSEIF action = CARD_MOVETO THEN
        MovePlayerTo(playerIdx, value)
        HandleLanding(playerIdx, 0)

    ELSEIF action = CARD_MOVE THEN
        MovePlayer(playerIdx, value)
        HandleLanding(playerIdx, 0)

    ELSEIF action = CARD_JAIL THEN
        p.GoToJail()
        ShowMessage("Go to Jail!")

    ELSEIF action = CARD_JAILFREE THEN
        p.AddJailFreeCard()
        ShowMessage("You got a Get Out of Jail Free card!")

    ELSEIF action = CARD_REPAIRS THEN
        DIM houseCost AS INTEGER
        DIM hotelCost AS INTEGER
        DIM totalHouses AS INTEGER
        DIM totalHotels AS INTEGER
        DIM repairCost AS INTEGER
        DIM prop AS PropData

        houseCost = c.GetValue()
        hotelCost = c.GetValue2()
        totalHouses = 0
        totalHotels = 0

        i = 0
        WHILE i < properties.Count
            prop = properties.get_Item(i)
            IF prop.GetOwner() = playerIdx THEN
                IF prop.GetHouses() = 5 THEN
                    totalHotels = totalHotels + 1
                ELSEIF prop.GetHouses() > 0 THEN
                    totalHouses = totalHouses + prop.GetHouses()
                END IF
            END IF
            i = i + 1
        WEND

        repairCost = (totalHouses * houseCost) + (totalHotels * hotelCost)
        p.SubtractMoney(repairCost)
        ShowMessage("Repairs cost $" + STR$(repairCost))

    ELSEIF action = CARD_PAYEACH THEN
        i = 0
        WHILE i < players.Count
            IF i <> playerIdx THEN
                otherPlayer = players.get_Item(i)
                IF otherPlayer.IsBankrupt() = 0 THEN
                    p.SubtractMoney(value)
                    otherPlayer.AddMoney(value)
                END IF
            END IF
            i = i + 1
        WEND

    ELSEIF action = CARD_COLLECTEACH THEN
        i = 0
        WHILE i < players.Count
            IF i <> playerIdx THEN
                otherPlayer = players.get_Item(i)
                IF otherPlayer.IsBankrupt() = 0 THEN
                    otherPlayer.SubtractMoney(value)
                    p.AddMoney(value)
                END IF
            END IF
            i = i + 1
        WEND

    ELSEIF action = CARD_NEARRR THEN
        DIM pos AS INTEGER
        pos = p.GetPosition()
        IF pos < 5 THEN
            MovePlayerTo(playerIdx, 5)
        ELSEIF pos < 15 THEN
            MovePlayerTo(playerIdx, 15)
        ELSEIF pos < 25 THEN
            MovePlayerTo(playerIdx, 25)
        ELSEIF pos < 35 THEN
            MovePlayerTo(playerIdx, 35)
        ELSE
            MovePlayerTo(playerIdx, 5)
        END IF
        HandleLanding(playerIdx, 0)

    ELSEIF action = CARD_NEARUTIL THEN
        DIM pos2 AS INTEGER
        pos2 = p.GetPosition()
        IF pos2 < 12 THEN
            MovePlayerTo(playerIdx, 12)
        ELSEIF pos2 < 28 THEN
            MovePlayerTo(playerIdx, 28)
        ELSE
            MovePlayerTo(playerIdx, 12)
        END IF
        HandleLanding(playerIdx, 0)
    END IF
END SUB

REM ====================================================================
REM Handle Community Chest card
REM ====================================================================
SUB HandleChestCard(playerIdx AS INTEGER)
    DIM p AS Player
    DIM c AS Card
    DIM action AS INTEGER
    DIM value AS INTEGER
    DIM i AS INTEGER
    DIM otherPlayer AS Player

    p = players.get_Item(playerIdx)
    c = DrawChestCard()

    ShowMessage("COMMUNITY CHEST: " + c.GetText())
    AddLogEntry(p.GetName() + " drew Community Chest")
    SLEEP 1500

    action = c.GetAction()
    value = c.GetValue()

    IF action = CARD_MONEY THEN
        IF value > 0 THEN
            p.AddMoney(value)
        ELSE
            p.SubtractMoney(0 - value)
        END IF

    ELSEIF action = CARD_MOVETO THEN
        MovePlayerTo(playerIdx, value)
        HandleLanding(playerIdx, 0)

    ELSEIF action = CARD_JAIL THEN
        p.GoToJail()

    ELSEIF action = CARD_JAILFREE THEN
        p.AddJailFreeCard()

    ELSEIF action = CARD_REPAIRS THEN
        DIM houseCost AS INTEGER
        DIM hotelCost AS INTEGER
        DIM totalHouses AS INTEGER
        DIM totalHotels AS INTEGER
        DIM repairCost AS INTEGER
        DIM prop AS PropData

        houseCost = c.GetValue()
        hotelCost = c.GetValue2()
        totalHouses = 0
        totalHotels = 0

        i = 0
        WHILE i < properties.Count
            prop = properties.get_Item(i)
            IF prop.GetOwner() = playerIdx THEN
                IF prop.GetHouses() = 5 THEN
                    totalHotels = totalHotels + 1
                ELSEIF prop.GetHouses() > 0 THEN
                    totalHouses = totalHouses + prop.GetHouses()
                END IF
            END IF
            i = i + 1
        WEND

        repairCost = (totalHouses * houseCost) + (totalHotels * hotelCost)
        p.SubtractMoney(repairCost)
        ShowMessage("Repairs cost $" + STR$(repairCost))

    ELSEIF action = CARD_COLLECTEACH THEN
        i = 0
        WHILE i < players.Count
            IF i <> playerIdx THEN
                otherPlayer = players.get_Item(i)
                IF otherPlayer.IsBankrupt() = 0 THEN
                    otherPlayer.SubtractMoney(value)
                    p.AddMoney(value)
                END IF
            END IF
            i = i + 1
        WEND
    END IF
END SUB

REM ====================================================================
REM Handle landing on a space
REM ====================================================================
SUB HandleLanding(playerIdx AS INTEGER, diceRoll AS INTEGER)
    DIM p AS Player
    DIM pos AS INTEGER
    DIM space AS BoardSpace
    DIM spaceType AS INTEGER
    DIM propIdx AS INTEGER

    p = players.get_Item(playerIdx)
    pos = p.GetPosition()
    space = boardSpaces.get_Item(pos)
    spaceType = space.GetType()
    propIdx = space.GetPropertyIndex()

    IF spaceType = SPACE_PROPERTY THEN
        HandleProperty(playerIdx, propIdx, diceRoll)
    ELSEIF spaceType = SPACE_RAILROAD THEN
        HandleProperty(playerIdx, propIdx, diceRoll)
    ELSEIF spaceType = SPACE_UTILITY THEN
        HandleProperty(playerIdx, propIdx, diceRoll)
    ELSEIF spaceType = SPACE_CHANCE THEN
        HandleChanceCard(playerIdx)
    ELSEIF spaceType = SPACE_CHEST THEN
        HandleChestCard(playerIdx)
    ELSEIF spaceType = SPACE_TAX THEN
        IF pos = 4 THEN
            REM Income Tax - $200
            p.SubtractMoney(200)
            ShowMessage("Income Tax: Pay $200")
            AddLogEntry(p.GetName() + " paid $200 Income Tax")
        ELSE
            REM Luxury Tax - $100
            p.SubtractMoney(100)
            ShowMessage("Luxury Tax: Pay $100")
            AddLogEntry(p.GetName() + " paid $100 Luxury Tax")
        END IF
        SLEEP 1000
    ELSEIF spaceType = SPACE_GOTOJAIL THEN
        p.GoToJail()
        ShowMessage("Go to Jail!")
        AddLogEntry(p.GetName() + " went to Jail!")
        SLEEP 1000
    ELSEIF spaceType = SPACE_GO THEN
        ShowMessage("Landed on GO!")
        SLEEP 500
    ELSEIF spaceType = SPACE_JAIL THEN
        ShowMessage("Just Visiting Jail")
        SLEEP 500
    ELSEIF spaceType = SPACE_PARKING THEN
        ShowMessage("Free Parking - relax!")
        SLEEP 500
    END IF
END SUB

REM ====================================================================
REM Handle jail turn
REM ====================================================================
FUNCTION HandleJailTurn(playerIdx AS INTEGER) AS INTEGER
    DIM p AS Player
    DIM d1 AS INTEGER
    DIM d2 AS INTEGER
    DIM choice AS INTEGER
    DIM inp AS STRING
    DIM released AS INTEGER

    d1 = 0
    d2 = 0
    p = players.get_Item(playerIdx)
    released = 0

    IF p.IsAI() = 1 THEN
        choice = AIJailDecision(playerIdx)
        IF choice = 2 THEN
            IF p.HasJailFreeCard() = 1 THEN
                p.UseJailFreeCard()
                p.ReleaseFromJail()
                AIAnnounce(playerIdx, "Uses Get Out of Jail Free card")
                released = 1
            END IF
        ELSEIF choice = 1 THEN
            IF p.GetMoney() >= 50 THEN
                p.SubtractMoney(50)
                p.ReleaseFromJail()
                AIAnnounce(playerIdx, "Pays $50 to get out of jail")
                released = 1
            END IF
        END IF

        IF released = 0 THEN
            REM Try to roll doubles
            RollDiceValues(d1, d2)
            ShowDiceRoll(d1, d2)

            IF d1 = d2 THEN
                p.ReleaseFromJail()
                AIAnnounce(playerIdx, "Rolled doubles! Released from jail!")
                MovePlayer(playerIdx, d1 + d2)
                released = 1
            ELSE
                p.IncrementJailTurns()
                IF p.GetJailTurns() >= 3 THEN
                    IF p.GetMoney() >= 50 THEN
                        p.SubtractMoney(50)
                        p.ReleaseFromJail()
                        AIAnnounce(playerIdx, "Must pay $50 after 3 turns")
                        MovePlayer(playerIdx, d1 + d2)
                        released = 1
                    END IF
                ELSE
                    AIAnnounce(playerIdx, "Stays in jail")
                END IF
            END IF
        END IF
    ELSE
        REM Human player jail options
        ShowMessage("You are in Jail! Turn " + STR$(p.GetJailTurns() + 1) + " of 3")
        PrintAt(38, 2, CLR_WHITE, "[R]oll doubles  [P]ay $50  ")
        IF p.HasJailFreeCard() = 1 THEN
            PrintAt(38, 30, CLR_YELLOW, "[C]ard")
        END IF

        inp = ""
        WHILE inp = ""
            inp = INKEY$()
            SLEEP 50
        WEND

        IF inp = "C" THEN
            IF p.HasJailFreeCard() = 1 THEN
                p.UseJailFreeCard()
                p.ReleaseFromJail()
                ShowMessage("Used Get Out of Jail Free card!")
                released = 1
            END IF
        ELSEIF inp = "c" THEN
            IF p.HasJailFreeCard() = 1 THEN
                p.UseJailFreeCard()
                p.ReleaseFromJail()
                ShowMessage("Used Get Out of Jail Free card!")
                released = 1
            END IF
        ELSEIF inp = "P" THEN
            IF p.GetMoney() >= 50 THEN
                p.SubtractMoney(50)
                p.ReleaseFromJail()
                ShowMessage("Paid $50 to get out!")
                released = 1
            ELSE
                ShowMessage("Not enough money!")
            END IF
        ELSEIF inp = "p" THEN
            IF p.GetMoney() >= 50 THEN
                p.SubtractMoney(50)
                p.ReleaseFromJail()
                ShowMessage("Paid $50 to get out!")
                released = 1
            ELSE
                ShowMessage("Not enough money!")
            END IF
        ELSE
            REM Try to roll doubles
            RollDiceValues(d1, d2)
            ShowDiceRoll(d1, d2)

            IF d1 = d2 THEN
                p.ReleaseFromJail()
                ShowMessage("Rolled doubles! You're free!")
                MovePlayer(playerIdx, d1 + d2)
                released = 1
            ELSE
                p.IncrementJailTurns()
                IF p.GetJailTurns() >= 3 THEN
                    IF p.GetMoney() >= 50 THEN
                        p.SubtractMoney(50)
                        p.ReleaseFromJail()
                        ShowMessage("Must pay $50 after 3 turns")
                        MovePlayer(playerIdx, d1 + d2)
                        released = 1
                    ELSE
                        HandleBankruptcy(playerIdx, -1, 50)
                    END IF
                ELSE
                    ShowMessage("No doubles - stay in jail")
                END IF
            END IF
        END IF
        SLEEP 1000
    END IF

    HandleJailTurn = released
END FUNCTION

REM ====================================================================
REM Handle bankruptcy
REM ====================================================================
SUB HandleBankruptcy(playerIdx AS INTEGER, creditorIdx AS INTEGER, amountOwed AS INTEGER)
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM totalValue AS INTEGER
    DIM canPay AS INTEGER

    p = players.get_Item(playerIdx)

    REM Calculate total assets
    totalValue = p.GetMoney()
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.IsMortgaged() = 0 THEN
                totalValue = totalValue + prop.GetMortgageValue()
                totalValue = totalValue + (prop.GetHouses() * prop.GetHouseCost() / 2)
            END IF
        END IF
        i = i + 1
    WEND

    IF totalValue < amountOwed THEN
        REM Player is bankrupt
        ShowMessage(p.GetName() + " is BANKRUPT!")
        AddLogEntry(p.GetName() + " went bankrupt!")
        stats.RecordBankruptcy()

        REM Transfer all properties
        i = 0
        WHILE i < properties.Count
            prop = properties.get_Item(i)
            IF prop.GetOwner() = playerIdx THEN
                IF creditorIdx >= 0 THEN
                    REM Transfer to creditor
                    prop.SetOwner(creditorIdx)
                    prop.Mortgage()
                ELSE
                    REM Return to bank
                    prop.Reset()
                END IF
            END IF
            i = i + 1
        WEND

        REM Transfer remaining money to creditor
        IF creditorIdx >= 0 THEN
            DIM creditor AS Player
            creditor = players.get_Item(creditorIdx)
            creditor.AddMoney(p.GetMoney())
        END IF

        p.GoBankrupt()
        SLEEP 2000
    ELSE
        REM Player can potentially pay - need to sell/mortgage
        ShowMessage("You need $" + STR$(amountOwed) + " - sell houses or mortgage properties!")
        REM Simplified - just mortgage properties
        i = 0
        WHILE i < properties.Count
            IF p.GetMoney() < amountOwed THEN
                prop = properties.get_Item(i)
                IF prop.GetOwner() = playerIdx THEN
                    IF prop.IsMortgaged() = 0 THEN
                        IF prop.GetHouses() = 0 THEN
                            prop.Mortgage()
                            p.AddMoney(prop.GetMortgageValue())
                        END IF
                    END IF
                END IF
            END IF
            i = i + 1
        WEND

        IF p.GetMoney() >= amountOwed THEN
            IF creditorIdx >= 0 THEN
                DIM creditor2 AS Player
                creditor2 = players.get_Item(creditorIdx)
                p.SubtractMoney(amountOwed)
                creditor2.AddMoney(amountOwed)
            ELSE
                p.SubtractMoney(amountOwed)
            END IF
        ELSE
            p.GoBankrupt()
            stats.RecordBankruptcy()
        END IF
    END IF
END SUB

REM ====================================================================
REM Handle house building
REM ====================================================================
SUB HandleBuildHouses(playerIdx AS INTEGER)
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM row AS INTEGER
    DIM inp AS STRING
    DIM propIdx AS INTEGER
    DIM grp AS INTEGER
    DIM canBuild AS INTEGER
    DIM minHouses AS INTEGER
    DIM j AS INTEGER
    DIM prop2 AS PropData

    p = players.get_Item(playerIdx)

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== BUILD HOUSES/HOTELS ===")
    PrintAt(2, 2, CLR_WHITE, "Your money: $" + STR$(p.GetMoney()))

    row = 4
    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            grp = prop.GetGroup()
            IF grp < GROUP_RAILROAD THEN
                IF prop.IsMortgaged() = 0 THEN
                    IF OwnsCompleteGroup(playerIdx, grp) = 1 THEN
                        IF prop.GetHouses() < 5 THEN
                            REM Check even build rule
                            minHouses = 99
                            j = 0
                            WHILE j < properties.Count
                                prop2 = properties.get_Item(j)
                                IF prop2.GetGroup() = grp THEN
                                    IF prop2.GetOwner() = playerIdx THEN
                                        IF prop2.GetHouses() < minHouses THEN
                                            minHouses = prop2.GetHouses()
                                        END IF
                                    END IF
                                END IF
                                j = j + 1
                            WEND

                            IF prop.GetHouses() = minHouses THEN
                                PrintAt(row, 2, GetGroupColor(grp), STR$(i) + ". " + prop.GetName())
                                PrintAt(row, 30, CLR_WHITE, "Houses: " + STR$(prop.GetHouses()))
                                PrintAt(row, 45, CLR_GREEN, "Cost: $" + STR$(prop.GetHouseCost()))
                                row = row + 1
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    IF row = 4 THEN
        PrintAt(row, 2, CLR_WHITE, "No properties eligible for building.")
    END IF

    PrintAt(row + 2, 2, CLR_WHITE, "Enter property # to build (0 to cancel): ")
    GotoXY(row + 2, 45)
    INPUT inp

    propIdx = VAL(inp)
    IF propIdx > 0 THEN
        IF propIdx < properties.Count THEN
            prop = properties.get_Item(propIdx)
            IF prop.GetOwner() = playerIdx THEN
                IF p.GetMoney() >= prop.GetHouseCost() THEN
                    p.SubtractMoney(prop.GetHouseCost())
                    prop.AddHouse()
                    IF prop.GetHouses() = 5 THEN
                        stats.RecordHotel()
                        ShowMessage("Built hotel on " + prop.GetName())
                    ELSE
                        stats.RecordHouse()
                        ShowMessage("Built house on " + prop.GetName())
                    END IF
                    AddLogEntry(p.GetName() + " built on " + prop.GetName())
                ELSE
                    ShowMessage("Not enough money!")
                END IF
                SLEEP 1000
            END IF
        END IF
    END IF
END SUB

REM ====================================================================
REM Handle mortgaging
REM ====================================================================
SUB HandleMortgage(playerIdx AS INTEGER)
    DIM p AS Player
    DIM prop AS PropData
    DIM i AS INTEGER
    DIM row AS INTEGER
    DIM inp AS STRING
    DIM propIdx AS INTEGER

    p = players.get_Item(playerIdx)

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== MORTGAGE/UNMORTGAGE ===")
    PrintAt(2, 2, CLR_WHITE, "Your money: $" + STR$(p.GetMoney()))

    row = 4
    PrintAt(row, 2, CLR_YELLOW, "Properties available to mortgage:")
    row = row + 1

    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.IsMortgaged() = 0 THEN
                IF prop.GetHouses() = 0 THEN
                    PrintAt(row, 2, GetGroupColor(prop.GetGroup()), STR$(i) + ". " + prop.GetName())
                    PrintAt(row, 30, CLR_GREEN, "Value: $" + STR$(prop.GetMortgageValue()))
                    row = row + 1
                END IF
            END IF
        END IF
        i = i + 1
    WEND

    row = row + 1
    PrintAt(row, 2, CLR_YELLOW, "Mortgaged properties (unmortgage cost = value + 10%):")
    row = row + 1

    i = 0
    WHILE i < properties.Count
        prop = properties.get_Item(i)
        IF prop.GetOwner() = playerIdx THEN
            IF prop.IsMortgaged() = 1 THEN
                DIM unmortCost AS INTEGER
                unmortCost = prop.GetMortgageValue() + (prop.GetMortgageValue() / 10)
                PrintAt(row, 2, CLR_RED, STR$(i) + ". " + prop.GetName() + " [M]")
                PrintAt(row, 35, CLR_YELLOW, "Unmortgage: $" + STR$(unmortCost))
                row = row + 1
            END IF
        END IF
        i = i + 1
    WEND

    PrintAt(row + 2, 2, CLR_WHITE, "Enter property # (0 to cancel): ")
    GotoXY(row + 2, 35)
    INPUT inp

    propIdx = VAL(inp)
    IF propIdx > 0 THEN
        IF propIdx < properties.Count THEN
            prop = properties.get_Item(propIdx)
            IF prop.GetOwner() = playerIdx THEN
                IF prop.IsMortgaged() = 0 THEN
                    IF prop.GetHouses() = 0 THEN
                        prop.Mortgage()
                        p.AddMoney(prop.GetMortgageValue())
                        ShowMessage("Mortgaged " + prop.GetName() + " for $" + STR$(prop.GetMortgageValue()))
                        AddLogEntry(p.GetName() + " mortgaged " + prop.GetName())
                    END IF
                ELSE
                    DIM unmortCost2 AS INTEGER
                    unmortCost2 = prop.GetMortgageValue() + (prop.GetMortgageValue() / 10)
                    IF p.GetMoney() >= unmortCost2 THEN
                        p.SubtractMoney(unmortCost2)
                        prop.Unmortgage()
                        ShowMessage("Unmortgaged " + prop.GetName())
                        AddLogEntry(p.GetName() + " unmortgaged " + prop.GetName())
                    ELSE
                        ShowMessage("Not enough money!")
                    END IF
                END IF
                SLEEP 1000
            END IF
        END IF
    END IF
END SUB

REM ====================================================================
REM Check for game over
REM ====================================================================
FUNCTION CheckGameOver() AS INTEGER
    DIM activePlayers AS INTEGER
    DIM i AS INTEGER
    DIM p AS Player

    activePlayers = 0
    i = 0
    WHILE i < players.Count
        p = players.get_Item(i)
        IF p.IsBankrupt() = 0 THEN
            activePlayers = activePlayers + 1
        END IF
        i = i + 1
    WEND

    IF activePlayers <= 1 THEN
        CheckGameOver = 1
    ELSE
        CheckGameOver = 0
    END IF
END FUNCTION

REM ====================================================================
REM Get next active player
REM ====================================================================
FUNCTION GetNextPlayer(current AS INTEGER) AS INTEGER
    DIM nextIdx AS INTEGER
    DIM p AS Player
    DIM found AS INTEGER

    nextIdx = current + 1
    IF nextIdx >= players.Count THEN
        nextIdx = 0
    END IF

    found = 0
    WHILE found = 0
        p = players.get_Item(nextIdx)
        IF p.IsBankrupt() = 0 THEN
            found = 1
        ELSE
            nextIdx = nextIdx + 1
            IF nextIdx >= players.Count THEN
                nextIdx = 0
            END IF
            IF nextIdx = current THEN
                found = 1
            END IF
        END IF
    WEND

    GetNextPlayer = nextIdx
END FUNCTION

REM ====================================================================
REM Play one turn
REM ====================================================================
SUB PlayTurn(playerIdx AS INTEGER)
    DIM p AS Player
    DIM d1 AS INTEGER
    DIM d2 AS INTEGER
    DIM total AS INTEGER
    DIM doubles AS INTEGER
    DIM doublesCount AS INTEGER
    DIM turnDone AS INTEGER
    DIM inp AS STRING
    DIM released AS INTEGER

    d1 = 0
    d2 = 0
    total = 0
    p = players.get_Item(playerIdx)
    doublesCount = 0
    turnDone = 0

    stats.RecordTurn()

    WHILE turnDone = 0
        ClearScreen()
        DrawBoard()
        DrawPlayerStatus(playerIdx)
        ShowGameMenu()

        IF p.IsInJail() = 1 THEN
            released = HandleJailTurn(playerIdx)
            IF released = 1 THEN
                HandleLanding(playerIdx, 0)
            END IF
            turnDone = 1
        ELSE
            IF p.IsAI() = 1 THEN
                REM AI turn
                AIDelay()
                RollDiceValues(d1, d2)
                ShowDiceRoll(d1, d2)
                total = d1 + d2

                IF d1 = d2 THEN
                    doublesCount = doublesCount + 1
                    IF doublesCount >= 3 THEN
                        p.GoToJail()
                        AIAnnounce(playerIdx, "3 doubles - Go to Jail!")
                        turnDone = 1
                    END IF
                END IF

                IF turnDone = 0 THEN
                    MovePlayer(playerIdx, total)
                    ClearScreen()
                    DrawBoard()
                    DrawPlayerStatus(playerIdx)
                    AIDelay()
                    HandleLanding(playerIdx, total)

                    REM AI building decisions
                    DIM buildProp AS INTEGER
                    buildProp = AIDecideBuild(playerIdx)
                    WHILE buildProp >= 0
                        DIM bprop AS PropData
                        bprop = properties.get_Item(buildProp)
                        IF p.GetMoney() >= bprop.GetHouseCost() THEN
                            p.SubtractMoney(bprop.GetHouseCost())
                            bprop.AddHouse()
                            AIAnnounce(playerIdx, "Builds on " + bprop.GetName())
                        END IF
                        buildProp = AIDecideBuild(playerIdx)
                    WEND

                    IF d1 <> d2 THEN
                        turnDone = 1
                    END IF
                END IF
            ELSE
                REM Human turn
                ShowMessage("Press [R] to roll dice, or choose action")
                inp = ""
                WHILE inp = ""
                    inp = INKEY$()
                    SLEEP 50
                WEND

                IF inp = "R" THEN
                    RollDiceValues(d1, d2)
                    ShowDiceRoll(d1, d2)
                    total = d1 + d2
                    SLEEP 500

                    IF d1 = d2 THEN
                        doublesCount = doublesCount + 1
                        IF doublesCount >= 3 THEN
                            p.GoToJail()
                            ShowMessage("3 doubles in a row - Go to Jail!")
                            turnDone = 1
                            SLEEP 1500
                        END IF
                    END IF

                    IF turnDone = 0 THEN
                        MovePlayer(playerIdx, total)
                        ClearScreen()
                        DrawBoard()
                        DrawPlayerStatus(playerIdx)
                        HandleLanding(playerIdx, total)

                        IF d1 <> d2 THEN
                            turnDone = 1
                        ELSE
                            ShowMessage("Doubles! Roll again.")
                            SLEEP 1000
                        END IF
                    END IF

                ELSEIF inp = "r" THEN
                    RollDiceValues(d1, d2)
                    ShowDiceRoll(d1, d2)
                    total = d1 + d2
                    SLEEP 500

                    IF d1 = d2 THEN
                        doublesCount = doublesCount + 1
                        IF doublesCount >= 3 THEN
                            p.GoToJail()
                            ShowMessage("3 doubles in a row - Go to Jail!")
                            turnDone = 1
                            SLEEP 1500
                        END IF
                    END IF

                    IF turnDone = 0 THEN
                        MovePlayer(playerIdx, total)
                        ClearScreen()
                        DrawBoard()
                        DrawPlayerStatus(playerIdx)
                        HandleLanding(playerIdx, total)

                        IF d1 <> d2 THEN
                            turnDone = 1
                        ELSE
                            ShowMessage("Doubles! Roll again.")
                            SLEEP 1000
                        END IF
                    END IF

                ELSEIF inp = "T" THEN
                    DIM tradeResult AS INTEGER
                    tradeResult = ShowTradeInterface(playerIdx)
                ELSEIF inp = "t" THEN
                    DIM tradeResult2 AS INTEGER
                    tradeResult2 = ShowTradeInterface(playerIdx)
                ELSEIF inp = "H" THEN
                    HandleBuildHouses(playerIdx)
                ELSEIF inp = "h" THEN
                    HandleBuildHouses(playerIdx)
                ELSEIF inp = "M" THEN
                    HandleMortgage(playerIdx)
                ELSEIF inp = "m" THEN
                    HandleMortgage(playerIdx)
                ELSEIF inp = "S" THEN
                    ShowAllPlayersStatus()
                ELSEIF inp = "s" THEN
                    ShowAllPlayersStatus()
                ELSEIF inp = "Q" THEN
                    gameOver = 1
                    turnDone = 1
                ELSEIF inp = "q" THEN
                    gameOver = 1
                    turnDone = 1
                END IF
            END IF
        END IF

        IF CheckGameOver() = 1 THEN
            gameOver = 1
            turnDone = 1
        END IF
    WEND
END SUB

REM ====================================================================
REM Setup new game
REM ====================================================================
SUB SetupNewGame()
    DIM numPlayers AS INTEGER
    DIM i AS INTEGER
    DIM inp AS STRING
    DIM pName AS STRING
    DIM pToken AS STRING
    DIM isAI AS INTEGER
    DIM aiType AS INTEGER
    DIM p AS Player
    DIM tokens AS STRING

    tokens = "@#$%&*"

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== MONOPOLY - NEW GAME ===")
    PrintAt(3, 2, CLR_WHITE, "Number of players (2-6): ")
    GotoXY(3, 30)
    INPUT inp
    numPlayers = VAL(inp)

    IF numPlayers < 2 THEN
        numPlayers = 2
    END IF
    IF numPlayers > 6 THEN
        numPlayers = 6
    END IF

    players = NEW Viper.Collections.List()

    i = 0
    WHILE i < numPlayers
        ClearScreen()
        PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== PLAYER " + STR$(i + 1) + " SETUP ===")

        PrintAt(3, 2, CLR_WHITE, "Player name: ")
        GotoXY(3, 16)
        INPUT pName

        IF LEN(pName) = 0 THEN
            pName = "Player " + STR$(i + 1)
        END IF

        pToken = MID$(tokens, i + 1, 1)

        PrintAt(5, 2, CLR_WHITE, "Is this player AI? [Y/N]: ")
        GotoXY(5, 30)
        INPUT inp

        isAI = 0
        aiType = AI_BALANCED

        IF inp = "Y" THEN
            isAI = 1
        ELSEIF inp = "y" THEN
            isAI = 1
        END IF

        IF isAI = 1 THEN
            PrintAt(7, 2, CLR_WHITE, "AI Type: [1]Aggressive [2]Conservative [3]Balanced: ")
            GotoXY(7, 55)
            INPUT inp
            IF inp = "1" THEN
                aiType = AI_AGGRESSIVE
            ELSEIF inp = "2" THEN
                aiType = AI_CONSERVATIVE
            ELSE
                aiType = AI_BALANCED
            END IF
        END IF

        p = NEW Player()
        p.Init(i, pName, pToken, isAI, aiType)
        players.Add(p)

        i = i + 1
    WEND

    currentPlayerIndex = 0
    gameOver = 0
    turnNumber = 0

    stats = NEW GameStats()
    stats.Init()

    InitGameLog()
    AddLogEntry("Game started with " + STR$(numPlayers) + " players")
END SUB

REM ====================================================================
REM Show main menu
REM ====================================================================
FUNCTION ShowMainMenu() AS INTEGER
    DIM choice AS INTEGER
    DIM inp AS STRING

    ClearScreen()

    PrintAt(3, 20, CLR_BRIGHT_WHITE, "M O N O P O L Y")
    PrintAt(5, 18, CLR_CYAN, "Viper BASIC Edition")

    PrintAt(8, 25, CLR_WHITE, "[1] New Game")
    PrintAt(9, 25, CLR_WHITE, "[2] Load Game")
    PrintAt(10, 25, CLR_WHITE, "[3] Rules")
    PrintAt(11, 25, CLR_WHITE, "[Q] Quit")

    PrintAt(14, 20, CLR_YELLOW, "Select option: ")

    choice = 0
    WHILE choice = 0
        inp = INKEY$()
        IF inp = "1" THEN
            choice = 1
        ELSEIF inp = "2" THEN
            choice = 2
        ELSEIF inp = "3" THEN
            choice = 3
        ELSEIF inp = "Q" THEN
            choice = 4
        ELSEIF inp = "q" THEN
            choice = 4
        END IF
        SLEEP 50
    WEND

    ShowMainMenu = choice
END FUNCTION

REM ====================================================================
REM Show rules
REM ====================================================================
SUB ShowRules()
    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== MONOPOLY RULES ===")
    PrintAt(3, 2, CLR_WHITE, "OBJECTIVE: Be the last player with money!")
    PrintAt(5, 2, CLR_CYAN, "ON YOUR TURN:")
    PrintAt(6, 2, CLR_WHITE, "- Roll dice and move that many spaces")
    PrintAt(7, 2, CLR_WHITE, "- If you roll doubles, roll again (3 doubles = jail)")
    PrintAt(9, 2, CLR_CYAN, "LANDING ON SPACES:")
    PrintAt(10, 2, CLR_WHITE, "- Property: Buy it or auction it")
    PrintAt(11, 2, CLR_WHITE, "- Owned property: Pay rent to owner")
    PrintAt(12, 2, CLR_WHITE, "- Chance/Community Chest: Draw a card")
    PrintAt(13, 2, CLR_WHITE, "- Income Tax: Pay $200")
    PrintAt(14, 2, CLR_WHITE, "- Go To Jail: Go directly to jail")
    PrintAt(16, 2, CLR_CYAN, "BUILDING:")
    PrintAt(17, 2, CLR_WHITE, "- Own all properties in a color group")
    PrintAt(18, 2, CLR_WHITE, "- Build houses evenly across the group")
    PrintAt(19, 2, CLR_WHITE, "- 4 houses = can build hotel")
    PrintAt(21, 2, CLR_CYAN, "JAIL:")
    PrintAt(22, 2, CLR_WHITE, "- Pay $50, use card, or try rolling doubles (3 turns max)")

    PrintAt(24, 2, CLR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Show winner
REM ====================================================================
SUB ShowWinner()
    DIM i AS INTEGER
    DIM p AS Player
    DIM winnerIdx AS INTEGER
    DIM winner AS Player

    winnerIdx = -1
    i = 0
    WHILE i < players.Count
        p = players.get_Item(i)
        IF p.IsBankrupt() = 0 THEN
            winnerIdx = i
        END IF
        i = i + 1
    WEND

    ClearScreen()

    IF winnerIdx >= 0 THEN
        winner = players.get_Item(winnerIdx)
        PrintAt(10, 20, CLR_BRIGHT_YELLOW, "*** WINNER ***")
        PrintAt(12, 15, CLR_BRIGHT_WHITE, winner.GetName() + " wins the game!")
        PrintAt(14, 15, CLR_GREEN, "Final money: $" + STR$(winner.GetMoney()))
    ELSE
        PrintAt(10, 20, CLR_WHITE, "Game Over - No winner")
    END IF

    PrintAt(18, 15, CLR_WHITE, "Total turns: " + STR$(stats.GetTotalTurns()))
    PrintAt(19, 15, CLR_WHITE, "Total rent paid: $" + STR$(stats.GetTotalRent()))

    PrintAt(22, 15, CLR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Main game loop
REM ====================================================================
SUB MainGameLoop()
    WHILE gameOver = 0
        PlayTurn(currentPlayerIndex)

        IF gameOver = 0 THEN
            currentPlayerIndex = GetNextPlayer(currentPlayerIndex)
            turnNumber = turnNumber + 1
        END IF
    WEND

    ShowWinner()
END SUB

REM ====================================================================
REM Main program entry
REM ====================================================================
RANDOMIZE

DIM menuChoice AS INTEGER
DIM running AS INTEGER

running = 1

WHILE running = 1
    menuChoice = ShowMainMenu()

    IF menuChoice = 1 THEN
        REM New game
        InitGameData()
        SetupNewGame()
        MainGameLoop()

    ELSEIF menuChoice = 2 THEN
        REM Load game
        IF SaveFileExists() = 1 THEN
            IF LoadGame() = 1 THEN
                ShowMessage("Game loaded!")
                SLEEP 1000
                MainGameLoop()
            ELSE
                ShowMessage("Failed to load game!")
                SLEEP 2000
            END IF
        ELSE
            ClearScreen()
            PrintAt(10, 20, CLR_WHITE, "No saved game found.")
            SLEEP 2000
        END IF

    ELSEIF menuChoice = 3 THEN
        REM Show rules
        ShowRules()

    ELSEIF menuChoice = 4 THEN
        REM Quit
        running = 0
    END IF
WEND

ClearScreen()
PrintAt(10, 20, CLR_WHITE, "Thanks for playing MONOPOLY!")
PrintAt(12, 15, CLR_CYAN, "A Viper BASIC Demo using Viper.Collections.List")
PRINT ""

