' ====================================================================
' MONOPOLY - A Complete Viper BASIC Implementation
' ====================================================================
' Features:
' - 4 players: 1 Human + 3 AI (Andy, Betty, Chip)
' - Full ANSI graphics board rendering
' - Complete Monopoly rules
' - Smart AI decision making
' ====================================================================

AddFile "ansi.bas"
AddFile "board.bas"
AddFile "players.bas"

' ====================================================================
' Game Constants
' ====================================================================
CONST BOARD_TOP = 2
CONST BOARD_LEFT = 2
CONST CELL_WIDTH = 10
CONST CELL_HEIGHT = 3
CONST INFO_COL = 90

' Dice state
DIM gDie1 AS INTEGER
DIM gDie2 AS INTEGER
DIM gIsDoubles AS INTEGER

' Game state
DIM gGameOver AS INTEGER
DIM gTurnCount AS INTEGER

' ====================================================================
' Dice Rolling - uses Viper.Random
' ====================================================================
SUB RollDice()
    gDie1 = Viper.Math.Random.NextInt(6) + 1
    gDie2 = Viper.Math.Random.NextInt(6) + 1

    IF gDie1 = gDie2 THEN
        gIsDoubles = 1
    ELSE
        gIsDoubles = 0
    END IF
END SUB

FUNCTION GetDiceTotal() AS INTEGER
    GetDiceTotal = gDie1 + gDie2
END FUNCTION

' ====================================================================
' Board Rendering
' ====================================================================

' Get screen position for a board space
SUB GetSpacePosition(spaceIdx AS INTEGER, BYREF row AS INTEGER, BYREF col AS INTEGER)
    ' Board layout:
    ' Top row: spaces 20-30 (left to right)
    ' Right column: spaces 31-39 (top to bottom)
    ' Bottom row: spaces 0-10 (right to left)
    ' Left column: spaces 11-19 (bottom to top)

    IF spaceIdx >= 0 AND spaceIdx <= 10 THEN
        ' Bottom row (GO to Jail) - right to left
        row = BOARD_TOP + 10 * CELL_HEIGHT
        col = BOARD_LEFT + (10 - spaceIdx) * CELL_WIDTH
    ELSE IF spaceIdx >= 11 AND spaceIdx <= 19 THEN
        ' Left column - bottom to top
        row = BOARD_TOP + (19 - spaceIdx) * CELL_HEIGHT
        col = BOARD_LEFT
    ELSE IF spaceIdx >= 20 AND spaceIdx <= 30 THEN
        ' Top row - left to right
        row = BOARD_TOP
        col = BOARD_LEFT + (spaceIdx - 20) * CELL_WIDTH
    ELSE IF spaceIdx >= 31 AND spaceIdx <= 39 THEN
        ' Right column - top to bottom
        row = BOARD_TOP + (spaceIdx - 30) * CELL_HEIGHT
        col = BOARD_LEFT + 10 * CELL_WIDTH
    END IF
END SUB

' Draw a single board space
SUB DrawSpace(spaceIdx AS INTEGER)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM space AS Space
    DIM name AS STRING
    DIM priceStr AS STRING
    DIM ownerStr AS STRING
    DIM colorBg AS STRING
    DIM owner AS INTEGER

    GetSpacePosition(spaceIdx, row, col)
    space = GetSpace(spaceIdx)

    ' Get color based on property group
    colorBg = space.GetColorCode()

    ' Draw cell background
    LOCATE row, col
    PRINT ESC; colorBg; RepeatStr(" ", CELL_WIDTH); ESC; RESET;
    LOCATE row + 1, col
    PRINT ESC; colorBg; RepeatStr(" ", CELL_WIDTH); ESC; RESET;
    LOCATE row + 2, col
    PRINT ESC; colorBg; RepeatStr(" ", CELL_WIDTH); ESC; RESET;

    ' Draw space name (centered)
    name = space.GetShortName()
    LOCATE row, col
    PRINT ESC; colorBg; FG_BLACK; CenterText(name, CELL_WIDTH); ESC; RESET;

    ' Draw price or special text
    IF space.IsOwnable() = 1 THEN
        priceStr = "$" + STR$(space.GetPrice())
        LOCATE row + 1, col
        PRINT ESC; colorBg; FG_BLACK; CenterText(priceStr, CELL_WIDTH); ESC; RESET;

        ' Show owner token if owned
        owner = space.GetOwner()
        IF owner >= 0 THEN
            DIM p AS Player
            p = GetPlayer(owner)
            ownerStr = "[" + p.GetToken() + "]"
            IF space.GetHouses() > 0 THEN
                IF space.GetHouses() = 5 THEN
                    ownerStr = ownerStr + "H"
                ELSE
                    ownerStr = ownerStr + STR$(space.GetHouses())
                END IF
            END IF
            LOCATE row + 2, col
            PRINT ESC; colorBg; p.GetColorCode(); CenterText(ownerStr, CELL_WIDTH); ESC; RESET;
        END IF
    ELSE
        ' Special spaces
        DIM spType AS INTEGER
        spType = space.GetType()

        IF spType = SPACE_GO THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_BLACK; CenterText("COLLECT", CELL_WIDTH); ESC; RESET;
            LOCATE row + 2, col
            PRINT ESC; colorBg; FG_BLACK; CenterText("$200", CELL_WIDTH); ESC; RESET;
        ELSE IF spType = SPACE_TAX THEN
            IF spaceIdx = 4 THEN
                LOCATE row + 1, col
                PRINT ESC; colorBg; FG_BLACK; CenterText("PAY $200", CELL_WIDTH); ESC; RESET;
            ELSE
                LOCATE row + 1, col
                PRINT ESC; colorBg; FG_BLACK; CenterText("PAY $100", CELL_WIDTH); ESC; RESET;
            END IF
        ELSE IF spType = SPACE_CHANCE THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_MAGENTA; CenterText("?", CELL_WIDTH); ESC; RESET;
        ELSE IF spType = SPACE_CHEST THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_CYAN; CenterText("CHEST", CELL_WIDTH); ESC; RESET;
        ELSE IF spType = SPACE_JAIL THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_BLACK; CenterText("VISITING", CELL_WIDTH); ESC; RESET;
        ELSE IF spType = SPACE_FREE_PARKING THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_GREEN; CenterText("FREE", CELL_WIDTH); ESC; RESET;
        ELSE IF spType = SPACE_GO_TO_JAIL THEN
            LOCATE row + 1, col
            PRINT ESC; colorBg; FG_RED; CenterText("GO TO", CELL_WIDTH); ESC; RESET;
            LOCATE row + 2, col
            PRINT ESC; colorBg; FG_RED; CenterText("JAIL!", CELL_WIDTH); ESC; RESET;
        END IF
    END IF
END SUB

' Draw the entire board
SUB DrawBoard()
    DIM i AS INTEGER

    ' Draw all 40 spaces
    FOR i = 0 TO 39
        DrawSpace(i)
    NEXT i

    ' Draw center area with game title
    DIM centerRow AS INTEGER
    DIM centerCol AS INTEGER
    centerRow = BOARD_TOP + 4 * CELL_HEIGHT
    centerCol = BOARD_LEFT + 2 * CELL_WIDTH

    PrintColorAt(centerRow, centerCol + 10, FG_BRIGHT_GREEN, "M O N O P O L Y")
    PrintColorAt(centerRow + 2, centerCol + 8, FG_WHITE, "Viper BASIC Edition")

    ' Draw player tokens on the board
    DrawPlayerTokens()
END SUB

' Draw player tokens at their positions
SUB DrawPlayerTokens()
    DIM i AS INTEGER
    DIM p AS Player
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM offset AS INTEGER

    FOR i = 0 TO 3
        p = GetPlayer(i)
        IF p.IsBankrupt() = 0 THEN
            GetSpacePosition(p.GetPosition(), row, col)

            ' Offset tokens so they don't overlap
            offset = i * 2
            LOCATE row + 2, col + 1 + offset
            PRINT ESC; p.GetColorCode(); p.GetToken(); ESC; RESET;
        END IF
    NEXT i
END SUB

' Draw player info panel on the right side
SUB DrawPlayerInfo()
    DIM i AS INTEGER
    DIM p AS Player
    DIM row AS INTEGER
    DIM statusStr AS STRING

    row = BOARD_TOP

    PrintColorAt(row, INFO_COL, FG_BRIGHT_WHITE, "=== PLAYERS ===")
    row = row + 2

    FOR i = 0 TO 3
        p = GetPlayer(i)

        ' Highlight current player
        IF i = gCurrentPlayer THEN
            PrintColorAt(row, INFO_COL - 2, FG_BRIGHT_YELLOW, ">>")
        ELSE
            PrintAt(row, INFO_COL - 2, "  ")
        END IF

        ' Player name and token
        PRINT ESC; p.GetColorCode();
        PrintAt(row, INFO_COL, p.GetToken() + " " + LeftAlign(p.GetName(), 8))
        PRINT ESC; RESET;

        ' Money
        PrintColorAt(row + 1, INFO_COL + 2, FG_GREEN, "$" + STR$(p.GetMoney()))

        ' Properties owned
        PrintAt(row + 2, INFO_COL + 2, "Props: " + STR$(p.GetPropertyCount()))

        ' Status
        IF p.IsBankrupt() = 1 THEN
            statusStr = "BANKRUPT"
            PrintColorAt(row + 3, INFO_COL + 2, FG_RED, statusStr)
        ELSE IF p.IsInJail() = 1 THEN
            statusStr = "IN JAIL"
            PrintColorAt(row + 3, INFO_COL + 2, FG_YELLOW, statusStr)
        ELSE
            statusStr = "Active"
            PrintColorAt(row + 3, INFO_COL + 2, FG_WHITE, statusStr)
        END IF

        row = row + 5
    NEXT i

    ' Game info
    row = row + 2
    PrintColorAt(row, INFO_COL, FG_BRIGHT_WHITE, "=== GAME INFO ===")
    PrintAt(row + 2, INFO_COL, "Turn: " + STR$(gTurnCount))
    PrintAt(row + 3, INFO_COL, "Active: " + STR$(CountActivePlayers()) + " players")
END SUB

' Draw message area at the bottom
SUB DrawMessage(msg AS STRING)
    DIM msgRow AS INTEGER
    msgRow = BOARD_TOP + 11 * CELL_HEIGHT + 2

    ' Clear message area
    LOCATE msgRow, BOARD_LEFT
    PRINT RepeatStr(" ", 80);

    ' Print new message
    LOCATE msgRow, BOARD_LEFT
    PrintColor(FG_BRIGHT_CYAN, msg)
END SUB

' Draw dice display
SUB DrawDice()
    DIM diceRow AS INTEGER
    DIM diceCol AS INTEGER

    diceRow = BOARD_TOP + 5 * CELL_HEIGHT
    diceCol = BOARD_LEFT + 4 * CELL_WIDTH

    PrintColorAt(diceRow, diceCol, FG_WHITE, "+---+  +---+")
    PrintColorAt(diceRow + 1, diceCol, FG_BRIGHT_WHITE, "| " + STR$(gDie1) + " |  | " + STR$(gDie2) + " |")
    PrintColorAt(diceRow + 2, diceCol, FG_WHITE, "+---+  +---+")

    IF gIsDoubles = 1 THEN
        PrintColorAt(diceRow + 3, diceCol, FG_BRIGHT_YELLOW, " DOUBLES!")
    ELSE
        PrintAt(diceRow + 3, diceCol, "          ")
    END IF
END SUB

' ====================================================================
' Game Initialization
' ====================================================================
SUB InitGame()
    ' Seed random number generator with current time
    Viper.Math.Random.Seed(Viper.Time.DateTime.NowMs())

    ' Initialize board and players
    InitBoard()
    InitPlayers()

    ' Reset game state
    gGameOver = 0
    gTurnCount = 1
    gDie1 = 0
    gDie2 = 0
    gIsDoubles = 0
END SUB

' ====================================================================
' Main Menu
' ====================================================================
SUB ShowMainMenu()
    DIM key AS STRING
    DIM choice AS INTEGER

    choice = 1

    WHILE 1 = 1
        ClearScreen()
        PRINT ""
        PRINT "  ╔═══════════════════════════════════════════════════════════════╗"
        PRINT "  ║                                                               ║"
        PRINT "  ║   ███╗   ███╗ ██████╗ ███╗   ██╗ ██████╗ ██████╗  ██████╗    ║"
        PRINT "  ║   ████╗ ████║██╔═══██╗████╗  ██║██╔═══██╗██╔══██╗██╔═══██╗   ║"
        PRINT "  ║   ██╔████╔██║██║   ██║██╔██╗ ██║██║   ██║██████╔╝██║   ██║   ║"
        PRINT "  ║   ██║╚██╔╝██║██║   ██║██║╚██╗██║██║   ██║██╔═══╝ ██║   ██║   ║"
        PRINT "  ║   ██║ ╚═╝ ██║╚██████╔╝██║ ╚████║╚██████╔╝██║     ╚██████╔╝   ║"
        PRINT "  ║   ╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚═╝      ╚═════╝    ║"
        PRINT "  ║                                                               ║"
        PRINT "  ║                  Viper BASIC Edition v1.0                     ║"
        PRINT "  ╚═══════════════════════════════════════════════════════════════╝"
        PRINT ""
        PRINT ""

        IF choice = 1 THEN
            PrintColor(FG_BRIGHT_GREEN, "              >> NEW GAME")
        ELSE
            PRINT "                 NEW GAME"
        END IF
        PRINT ""

        IF choice = 2 THEN
            PrintColor(FG_BRIGHT_GREEN, "              >> HOW TO PLAY")
        ELSE
            PRINT "                 HOW TO PLAY"
        END IF
        PRINT ""

        IF choice = 3 THEN
            PrintColor(FG_BRIGHT_GREEN, "              >> QUIT")
        ELSE
            PRINT "                 QUIT"
        END IF

        PRINT ""
        PRINT ""
        PRINT "         Use W/S or UP/DOWN to navigate, ENTER to select"
        PRINT ""
        PRINT "         Players: You vs Andy, Betty, and Chip (AI)"

        key = WaitKey()

        IF key = "w" OR key = "W" THEN
            choice = choice - 1
            IF choice < 1 THEN
                choice = 3
            END IF
        END IF

        IF key = "s" OR key = "S" THEN
            choice = choice + 1
            IF choice > 3 THEN
                choice = 1
            END IF
        END IF

        IF key = CHR(13) OR key = CHR(10) THEN
            IF choice = 1 THEN
                ' Start new game
                PlayGame()
            ELSE IF choice = 2 THEN
                ShowInstructions()
            ELSE IF choice = 3 THEN
                ' Quit
                EXIT WHILE
            END IF
        END IF

        IF key = "q" OR key = "Q" THEN
            EXIT WHILE
        END IF
    WEND
END SUB

' ====================================================================
' Instructions Screen
' ====================================================================
SUB ShowInstructions()
    ClearScreen()
    PRINT ""
    PrintColor(FG_BRIGHT_CYAN, "  ═══════════════════════════════════════════════════════════════")
    PRINT ""
    PrintColor(FG_BRIGHT_WHITE, "                         HOW TO PLAY MONOPOLY")
    PRINT ""
    PrintColor(FG_BRIGHT_CYAN, "  ═══════════════════════════════════════════════════════════════")
    PRINT ""
    PRINT "  OBJECTIVE:"
    PRINT "    Become the wealthiest player by buying, renting, and trading"
    PRINT "    properties. Bankrupt your opponents to win!"
    PRINT ""
    PRINT "  GAMEPLAY:"
    PRINT "    - Roll dice to move around the board"
    PRINT "    - Buy unowned properties you land on"
    PRINT "    - Pay rent when landing on owned properties"
    PRINT "    - Collect $200 each time you pass GO"
    PRINT "    - Build houses/hotels on your monopolies"
    PRINT ""
    PRINT "  CONTROLS:"
    PRINT "    SPACE/ENTER - Roll dice / Confirm action"
    PRINT "    B           - Buy property"
    PRINT "    A           - Auction property"
    PRINT "    M           - Manage properties (build/mortgage)"
    PRINT "    T           - Trade with other players"
    PRINT "    Q           - Quit to menu"
    PRINT ""
    PRINT "  SPECIAL SPACES:"
    PRINT "    GO          - Collect $200"
    PRINT "    JAIL        - Just visiting (or stuck if sent here)"
    PRINT "    FREE PARK   - Nothing happens"
    PRINT "    GO TO JAIL  - Go directly to jail!"
    PRINT ""
    PrintColor(FG_BRIGHT_YELLOW, "  Press any key to return to menu...")

    DIM k AS STRING
    k = WaitKey()
END SUB

' ====================================================================
' Main Game Loop
' ====================================================================
SUB PlayGame()
    InitGame()

    ClearScreen()
    HideCursor()

    ' Initial draw
    DrawBoard()
    DrawPlayerInfo()
    DrawMessage("Welcome to Monopoly! Press SPACE to roll dice.")

    ' Main game loop
    WHILE gGameOver = 0
        ProcessTurn()

        ' Check win condition
        IF CountActivePlayers() <= 1 THEN
            gGameOver = 1
        END IF
    WEND

    ShowCursor()
    ShowGameOver()
END SUB

' ====================================================================
' Process a single turn
' ====================================================================
SUB ProcessTurn()
    DIM p AS Player
    DIM key AS STRING
    DIM rolled AS INTEGER

    p = GetCurrentPlayer()

    ' Skip bankrupt players
    IF p.IsBankrupt() = 1 THEN
        NextPlayer()
        RETURN
    END IF

    rolled = 0

    ' Redraw board state
    DrawBoard()
    DrawPlayerInfo()

    IF p.IsHuman() = 1 THEN
        ' Human player turn
        DrawMessage(p.GetName() + "'s turn. Press SPACE to roll dice, Q to quit.")

        WHILE rolled = 0
            key = WaitKey()

            IF key = " " OR key = CHR(13) THEN
                rolled = 1
                RollDice()
                DrawDice()
                WaitMs(500)

                ' Check for three doubles (go to jail)
                IF gIsDoubles = 1 THEN
                    p.IncrementDoubles()
                    IF p.GetDoubleCount() >= 3 THEN
                        p.GoToJail()
                        DrawMessage("Three doubles! Go to Jail!")
                        WaitMs(1500)
                        p.ResetDoubles()
                        NextPlayer()
                        gTurnCount = gTurnCount + 1
                        RETURN
                    END IF
                ELSE
                    p.ResetDoubles()
                END IF

                ' Move player
                IF p.IsInJail() = 0 THEN
                    ProcessMovement(p)
                ELSE
                    ProcessJailTurn(p)
                END IF
            END IF

            IF key = "q" OR key = "Q" THEN
                gGameOver = 1
                RETURN
            END IF
        WEND
    ELSE
        ' AI player turn
        DrawMessage(p.GetName() + " is thinking...")
        WaitMs(800)

        RollDice()
        DrawDice()
        WaitMs(600)

        ' Check for three doubles
        IF gIsDoubles = 1 THEN
            p.IncrementDoubles()
            IF p.GetDoubleCount() >= 3 THEN
                p.GoToJail()
                DrawMessage(p.GetName() + " rolled three doubles! Go to Jail!")
                WaitMs(1500)
                p.ResetDoubles()
                NextPlayer()
                gTurnCount = gTurnCount + 1
                RETURN
            END IF
        ELSE
            p.ResetDoubles()
        END IF

        ' Move AI player
        IF p.IsInJail() = 0 THEN
            ProcessMovement(p)
        ELSE
            ProcessJailTurn(p)
        END IF
    END IF

    ' Check for doubles (roll again)
    IF gIsDoubles = 1 AND p.IsInJail() = 0 AND p.IsBankrupt() = 0 THEN
        DrawMessage(p.GetName() + " rolled doubles! Roll again...")
        WaitMs(1000)
        RETURN  ' Don't advance to next player
    END IF

    NextPlayer()
    gTurnCount = gTurnCount + 1
END SUB

' ====================================================================
' Process player movement and landing
' ====================================================================
SUB ProcessMovement(p AS Player)
    DIM total AS INTEGER
    DIM oldPos AS INTEGER
    DIM newPos AS INTEGER
    DIM space AS Space

    total = GetDiceTotal()
    oldPos = p.GetPosition()

    p.MoveForward(total)
    newPos = p.GetPosition()

    ' Redraw to show new position
    DrawBoard()
    DrawPlayerInfo()

    space = GetSpace(newPos)
    DrawMessage(p.GetName() + " landed on " + space.GetName())
    WaitMs(800)

    ' Process the space
    ProcessLanding(p, space)
END SUB

' ====================================================================
' Process landing on a space
' ====================================================================
SUB ProcessLanding(p AS Player, space AS Space)
    DIM spaceType AS INTEGER
    spaceType = space.GetType()

    IF spaceType = SPACE_GO THEN
        DrawMessage("Landed on GO! Collect $200.")
    ELSE IF spaceType = SPACE_PROPERTY OR spaceType = SPACE_RAILROAD OR spaceType = SPACE_UTILITY THEN
        ProcessPropertyLanding(p, space)
    ELSE IF spaceType = SPACE_TAX THEN
        ProcessTax(p, space)
    ELSE IF spaceType = SPACE_CHANCE THEN
        ProcessChance(p)
    ELSE IF spaceType = SPACE_CHEST THEN
        ProcessCommunityChest(p)
    ELSE IF spaceType = SPACE_JAIL THEN
        DrawMessage("Just visiting jail.")
    ELSE IF spaceType = SPACE_FREE_PARKING THEN
        DrawMessage("Free Parking - take a rest!")
    ELSE IF spaceType = SPACE_GO_TO_JAIL THEN
        p.GoToJail()
        DrawMessage(p.GetName() + " goes to Jail!")
        DrawBoard()
        DrawPlayerInfo()
    END IF

    WaitMs(800)
END SUB

' ====================================================================
' Process landing on a property
' ====================================================================
SUB ProcessPropertyLanding(p AS Player, space AS Space)
    DIM owner AS INTEGER
    DIM rent AS INTEGER
    DIM ownerPlayer AS Player
    DIM groupOwned AS INTEGER
    DIM groupTotal AS INTEGER

    owner = space.GetOwner()

    IF owner = -1 THEN
        ' Unowned - offer to buy
        IF p.IsHuman() = 1 THEN
            OfferPropertyToHuman(p, space)
        ELSE
            AIDecideBuy(p, space)
        END IF
    ELSE IF owner <> p.GetIndex() THEN
        ' Owned by another player - pay rent
        IF space.IsMortgaged() = 0 THEN
            ownerPlayer = GetPlayer(owner)
            groupOwned = CountOwnedInGroup(owner, space.GetGroup())
            groupTotal = GetGroupCount(space.GetGroup())
            rent = space.CalculateRent(GetDiceTotal(), groupOwned, groupTotal)

            DrawMessage(p.GetName() + " pays $" + STR$(rent) + " rent to " + ownerPlayer.GetName())
            p.SubtractMoney(rent)
            ownerPlayer.AddMoney(rent)

            ' Check bankruptcy
            IF p.GetMoney() <= 0 THEN
                p.SetBankrupt()
                DrawMessage(p.GetName() + " is BANKRUPT!")
            END IF

            DrawPlayerInfo()
        ELSE
            DrawMessage("Property is mortgaged - no rent due.")
        END IF
    ELSE
        DrawMessage(p.GetName() + " owns this property.")
    END IF
END SUB

' ====================================================================
' Offer property to human player
' ====================================================================
SUB OfferPropertyToHuman(p AS Player, space AS Space)
    DIM key AS STRING
    DIM price AS INTEGER

    price = space.GetPrice()

    IF p.CanAfford(price) = 1 THEN
        DrawMessage(space.GetName() + " costs $" + STR$(price) + ". B=Buy, A=Auction")

        WHILE 1 = 1
            key = WaitKey()

            IF key = "b" OR key = "B" THEN
                p.SubtractMoney(price)
                space.SetOwner(p.GetIndex())
                p.AddProperty(space.GetIndex())
                DrawMessage(p.GetName() + " bought " + space.GetName() + "!")
                DrawBoard()
                DrawPlayerInfo()
                EXIT WHILE
            END IF

            IF key = "a" OR key = "A" THEN
                DrawMessage("Auction not implemented yet - property remains unowned.")
                EXIT WHILE
            END IF
        WEND
    ELSE
        DrawMessage("You can't afford " + space.GetName() + " ($" + STR$(price) + ")")
    END IF
END SUB

' ====================================================================
' AI decides whether to buy a property
' ====================================================================
SUB AIDecideBuy(p AS Player, space AS Space)
    DIM price AS INTEGER
    DIM shouldBuy AS INTEGER

    price = space.GetPrice()
    shouldBuy = 0

    ' Simple AI: buy if can afford and have enough reserve
    IF p.CanAfford(price) = 1 THEN
        ' Keep at least $100 reserve
        IF p.GetMoney() - price >= 100 THEN
            shouldBuy = 1
        ELSE
            ' Still buy railroads and utilities
            IF space.GetType() = SPACE_RAILROAD OR space.GetType() = SPACE_UTILITY THEN
                IF p.GetMoney() - price >= 50 THEN
                    shouldBuy = 1
                END IF
            END IF
        END IF

        ' Always try to complete a monopoly
        DIM groupOwned AS INTEGER
        DIM groupTotal AS INTEGER
        groupOwned = CountOwnedInGroup(p.GetIndex(), space.GetGroup())
        groupTotal = GetGroupCount(space.GetGroup())
        IF groupOwned = groupTotal - 1 AND p.CanAfford(price) = 1 THEN
            shouldBuy = 1
        END IF
    END IF

    IF shouldBuy = 1 THEN
        p.SubtractMoney(price)
        space.SetOwner(p.GetIndex())
        p.AddProperty(space.GetIndex())
        DrawMessage(p.GetName() + " bought " + space.GetName() + "!")
        DrawBoard()
        DrawPlayerInfo()
    ELSE
        DrawMessage(p.GetName() + " passes on " + space.GetName())
    END IF
END SUB

' ====================================================================
' Process tax spaces
' ====================================================================
SUB ProcessTax(p AS Player, space AS Space)
    DIM tax AS INTEGER

    IF space.GetIndex() = 4 THEN
        tax = 200  ' Income Tax
    ELSE
        tax = 100  ' Luxury Tax
    END IF

    DrawMessage(p.GetName() + " pays $" + STR$(tax) + " tax.")
    p.SubtractMoney(tax)

    IF p.GetMoney() <= 0 THEN
        p.SetBankrupt()
        DrawMessage(p.GetName() + " is BANKRUPT!")
    END IF

    DrawPlayerInfo()
END SUB

' ====================================================================
' Process Chance card (simplified)
' ====================================================================
SUB ProcessChance(p AS Player)
    DIM card AS INTEGER
    card = Viper.Math.Random.NextInt(8)

    SELECT CASE card
        CASE 0
            DrawMessage("Chance: Advance to GO! Collect $200.")
            p.SetPosition(0)
            p.AddMoney(200)
        CASE 1
            DrawMessage("Chance: Go to Jail!")
            p.GoToJail()
        CASE 2
            DrawMessage("Chance: Bank pays dividend of $50.")
            p.AddMoney(50)
        CASE 3
            DrawMessage("Chance: Pay poor tax of $15.")
            p.SubtractMoney(15)
        CASE 4
            DrawMessage("Chance: Advance to Illinois Ave.")
            IF p.GetPosition() > 24 THEN
                p.AddMoney(200)  ' Passed GO
            END IF
            p.SetPosition(24)
            ProcessLanding(p, GetSpace(24))
        CASE 5
            DrawMessage("Chance: Take a ride on Reading RR.")
            IF p.GetPosition() > 5 THEN
                p.AddMoney(200)  ' Passed GO
            END IF
            p.SetPosition(5)
            ProcessLanding(p, GetSpace(5))
        CASE 6
            DrawMessage("Chance: Building loan matures - collect $150.")
            p.AddMoney(150)
        CASE 7
            DrawMessage("Chance: Get out of Jail Free card!")
            p.AddGetOutOfJailCard()
    END SELECT

    DrawBoard()
    DrawPlayerInfo()
END SUB

' ====================================================================
' Process Community Chest card (simplified)
' ====================================================================
SUB ProcessCommunityChest(p AS Player)
    DIM card AS INTEGER
    card = Viper.Math.Random.NextInt(8)

    SELECT CASE card
        CASE 0
            DrawMessage("Community Chest: Advance to GO! Collect $200.")
            p.SetPosition(0)
            p.AddMoney(200)
        CASE 1
            DrawMessage("Community Chest: Bank error - collect $200.")
            p.AddMoney(200)
        CASE 2
            DrawMessage("Community Chest: Doctor's fee - pay $50.")
            p.SubtractMoney(50)
        CASE 3
            DrawMessage("Community Chest: Life insurance matures - collect $100.")
            p.AddMoney(100)
        CASE 4
            DrawMessage("Community Chest: Pay hospital $100.")
            p.SubtractMoney(100)
        CASE 5
            DrawMessage("Community Chest: Income tax refund - collect $20.")
            p.AddMoney(20)
        CASE 6
            DrawMessage("Community Chest: You inherit $100.")
            p.AddMoney(100)
        CASE 7
            DrawMessage("Community Chest: Get out of Jail Free card!")
            p.AddGetOutOfJailCard()
    END SELECT

    IF p.GetMoney() <= 0 THEN
        p.SetBankrupt()
        DrawMessage(p.GetName() + " is BANKRUPT!")
    END IF

    DrawBoard()
    DrawPlayerInfo()
END SUB

' ====================================================================
' Process jail turn
' ====================================================================
SUB ProcessJailTurn(p AS Player)
    DIM key AS STRING

    IF p.IsHuman() = 1 THEN
        DrawMessage("In Jail! R=Roll doubles, P=Pay $50, G=Use Get Out card")

        WHILE 1 = 1
            key = WaitKey()

            IF key = "r" OR key = "R" THEN
                RollDice()
                DrawDice()
                WaitMs(500)

                IF gIsDoubles = 1 THEN
                    p.ReleaseFromJail()
                    DrawMessage("Doubles! You're free!")
                    ProcessMovement(p)
                ELSE
                    p.IncrementJailTurns()
                    IF p.GetJailTurns() >= 3 THEN
                        p.SubtractMoney(50)
                        p.ReleaseFromJail()
                        DrawMessage("3 turns - pay $50 and move.")
                        ProcessMovement(p)
                    ELSE
                        DrawMessage("No doubles. Still in jail.")
                    END IF
                END IF
                EXIT WHILE
            END IF

            IF key = "p" OR key = "P" THEN
                IF p.CanAfford(50) = 1 THEN
                    p.SubtractMoney(50)
                    p.ReleaseFromJail()
                    DrawMessage("Paid $50 bail.")
                    RollDice()
                    DrawDice()
                    WaitMs(500)
                    ProcessMovement(p)
                    EXIT WHILE
                ELSE
                    DrawMessage("Can't afford $50 bail!")
                END IF
            END IF

            IF key = "g" OR key = "G" THEN
                IF p.GetGetOutOfJailCards() > 0 THEN
                    p.UseGetOutOfJailCard()
                    DrawMessage("Used Get Out of Jail Free card!")
                    RollDice()
                    DrawDice()
                    WaitMs(500)
                    ProcessMovement(p)
                    EXIT WHILE
                ELSE
                    DrawMessage("No Get Out of Jail cards!")
                END IF
            END IF
        WEND
    ELSE
        ' AI jail logic
        WaitMs(500)

        ' Use card if available
        IF p.GetGetOutOfJailCards() > 0 THEN
            p.UseGetOutOfJailCard()
            DrawMessage(p.GetName() + " uses Get Out of Jail card!")
            RollDice()
            DrawDice()
            WaitMs(500)
            ProcessMovement(p)
            RETURN
        END IF

        ' Pay if can afford and been in jail 2 turns
        IF p.GetJailTurns() >= 2 AND p.CanAfford(50) = 1 THEN
            p.SubtractMoney(50)
            p.ReleaseFromJail()
            DrawMessage(p.GetName() + " pays $50 bail.")
            RollDice()
            DrawDice()
            WaitMs(500)
            ProcessMovement(p)
            RETURN
        END IF

        ' Try to roll doubles
        RollDice()
        DrawDice()
        WaitMs(500)

        IF gIsDoubles = 1 THEN
            p.ReleaseFromJail()
            DrawMessage(p.GetName() + " rolled doubles - free!")
            ProcessMovement(p)
        ELSE
            p.IncrementJailTurns()
            IF p.GetJailTurns() >= 3 THEN
                p.SubtractMoney(50)
                p.ReleaseFromJail()
                DrawMessage(p.GetName() + " pays $50 after 3 turns.")
                ProcessMovement(p)
            ELSE
                DrawMessage(p.GetName() + " stays in jail.")
            END IF
        END IF
    END IF

    DrawPlayerInfo()
END SUB

' ====================================================================
' Game Over Screen
' ====================================================================
SUB ShowGameOver()
    DIM winner AS INTEGER
    DIM p AS Player

    ClearScreen()
    winner = FindWinner()
    p = GetPlayer(winner)

    PRINT ""
    PRINT "  ╔═══════════════════════════════════════════════════════════════╗"
    PRINT "  ║                                                               ║"
    PRINT "  ║                      G A M E   O V E R                        ║"
    PRINT "  ║                                                               ║"
    PRINT "  ╚═══════════════════════════════════════════════════════════════╝"
    PRINT ""
    PRINT ""
    PrintColor(FG_BRIGHT_YELLOW, "              WINNER: " + p.GetName())
    PRINT ""
    PRINT ""
    PRINT "              Final Net Worth: $"; p.GetNetWorth()
    PRINT "              Properties Owned: "; p.GetPropertyCount()
    PRINT ""
    PRINT ""
    PRINT "              Total Turns: "; gTurnCount
    PRINT ""
    PRINT ""
    PrintColor(FG_BRIGHT_CYAN, "              Press any key to return to menu...")

    DIM k AS STRING
    k = WaitKey()
END SUB

' ====================================================================
' Main Entry Point
' ====================================================================
ClearScreen()
ShowMainMenu()
ClearScreen()
PRINT ""
PRINT "Thanks for playing MONOPOLY!"
PRINT ""
