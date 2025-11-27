REM ====================================================================
REM MONOPOLY - Board Display and UI
REM ASCII art board with ANSI colors
REM ====================================================================

REM ANSI escape code prefix
DIM ESC AS STRING
ESC = CHR(27)

REM ANSI Colors
DIM CLR_RESET AS STRING
DIM CLR_BLACK AS STRING
DIM CLR_RED AS STRING
DIM CLR_GREEN AS STRING
DIM CLR_YELLOW AS STRING
DIM CLR_BLUE AS STRING
DIM CLR_MAGENTA AS STRING
DIM CLR_CYAN AS STRING
DIM CLR_WHITE AS STRING
DIM CLR_BRIGHT_RED AS STRING
DIM CLR_BRIGHT_GREEN AS STRING
DIM CLR_BRIGHT_YELLOW AS STRING
DIM CLR_BRIGHT_BLUE AS STRING
DIM CLR_BRIGHT_MAGENTA AS STRING
DIM CLR_BRIGHT_CYAN AS STRING
DIM CLR_BRIGHT_WHITE AS STRING
DIM CLR_BG_RED AS STRING
DIM CLR_BG_GREEN AS STRING
DIM CLR_BG_YELLOW AS STRING
DIM CLR_BG_BLUE AS STRING
DIM CLR_BG_MAGENTA AS STRING
DIM CLR_BG_CYAN AS STRING
DIM CLR_BG_WHITE AS STRING
DIM CLR_BG_BROWN AS STRING

CLR_RESET = "[0m"
CLR_BLACK = "[30m"
CLR_RED = "[31m"
CLR_GREEN = "[32m"
CLR_YELLOW = "[33m"
CLR_BLUE = "[34m"
CLR_MAGENTA = "[35m"
CLR_CYAN = "[36m"
CLR_WHITE = "[37m"
CLR_BRIGHT_RED = "[91m"
CLR_BRIGHT_GREEN = "[92m"
CLR_BRIGHT_YELLOW = "[93m"
CLR_BRIGHT_BLUE = "[94m"
CLR_BRIGHT_MAGENTA = "[95m"
CLR_BRIGHT_CYAN = "[96m"
CLR_BRIGHT_WHITE = "[97m"
CLR_BG_RED = "[41m"
CLR_BG_GREEN = "[42m"
CLR_BG_YELLOW = "[43m"
CLR_BG_BLUE = "[44m"
CLR_BG_MAGENTA = "[45m"
CLR_BG_CYAN = "[46m"
CLR_BG_WHITE = "[47m"
CLR_BG_BROWN = "[48;5;94m"

REM Player tokens
DIM TOKEN_1 AS STRING
DIM TOKEN_2 AS STRING
DIM TOKEN_3 AS STRING
DIM TOKEN_4 AS STRING
DIM TOKEN_5 AS STRING
DIM TOKEN_6 AS STRING

TOKEN_1 = "@"
TOKEN_2 = "#"
TOKEN_3 = "$"
TOKEN_4 = "%"
TOKEN_5 = "&"
TOKEN_6 = "*"

REM ====================================================================
REM Clear screen
REM ====================================================================
SUB ClearScreen()
    PRINT ESC; "[2J"; ESC; "[H";
END SUB

REM ====================================================================
REM Move cursor to position
REM ====================================================================
SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; row; ";"; col; "H";
END SUB

REM ====================================================================
REM Print colored text at position
REM ====================================================================
SUB PrintAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    GotoXY(row, col)
    PRINT ESC; clr; text; ESC; CLR_RESET;
END SUB

REM ====================================================================
REM Get color for property group
REM ====================================================================
FUNCTION GetGroupColor(grp AS INTEGER) AS STRING
    DIM clr AS STRING
    IF grp = GROUP_BROWN THEN
        clr = "[38;5;94m"
    ELSEIF grp = GROUP_LTBLUE THEN
        clr = CLR_BRIGHT_CYAN
    ELSEIF grp = GROUP_PINK THEN
        clr = CLR_BRIGHT_MAGENTA
    ELSEIF grp = GROUP_ORANGE THEN
        clr = "[38;5;208m"
    ELSEIF grp = GROUP_RED THEN
        clr = CLR_BRIGHT_RED
    ELSEIF grp = GROUP_YELLOW THEN
        clr = CLR_BRIGHT_YELLOW
    ELSEIF grp = GROUP_GREEN THEN
        clr = CLR_BRIGHT_GREEN
    ELSEIF grp = GROUP_DKBLUE THEN
        clr = CLR_BRIGHT_BLUE
    ELSEIF grp = GROUP_RAILROAD THEN
        clr = CLR_WHITE
    ELSEIF grp = GROUP_UTILITY THEN
        clr = CLR_CYAN
    ELSE
        clr = CLR_WHITE
    END IF
    GetGroupColor = clr
END FUNCTION

REM ====================================================================
REM Get abbreviated space name (8 chars max)
REM ====================================================================
FUNCTION GetShortName(pos AS INTEGER) AS STRING
    DIM name AS STRING
    IF pos = 0 THEN
        name = "  GO   "
    ELSEIF pos = 1 THEN
        name = "Medit  "
    ELSEIF pos = 2 THEN
        name = "Chest  "
    ELSEIF pos = 3 THEN
        name = "Baltic "
    ELSEIF pos = 4 THEN
        name = "Income$"
    ELSEIF pos = 5 THEN
        name = "ReadRR "
    ELSEIF pos = 6 THEN
        name = "Orient "
    ELSEIF pos = 7 THEN
        name = "Chance "
    ELSEIF pos = 8 THEN
        name = "Vermont"
    ELSEIF pos = 9 THEN
        name = "Connect"
    ELSEIF pos = 10 THEN
        name = " JAIL  "
    ELSEIF pos = 11 THEN
        name = "StCharl"
    ELSEIF pos = 12 THEN
        name = "Electrc"
    ELSEIF pos = 13 THEN
        name = "States "
    ELSEIF pos = 14 THEN
        name = "Virgina"
    ELSEIF pos = 15 THEN
        name = "PennRR "
    ELSEIF pos = 16 THEN
        name = "StJames"
    ELSEIF pos = 17 THEN
        name = "Chest  "
    ELSEIF pos = 18 THEN
        name = "Tennes "
    ELSEIF pos = 19 THEN
        name = "NewYork"
    ELSEIF pos = 20 THEN
        name = "Parking"
    ELSEIF pos = 21 THEN
        name = "Kentuck"
    ELSEIF pos = 22 THEN
        name = "Chance "
    ELSEIF pos = 23 THEN
        name = "Indiana"
    ELSEIF pos = 24 THEN
        name = "Illinoi"
    ELSEIF pos = 25 THEN
        name = "B&O RR "
    ELSEIF pos = 26 THEN
        name = "Atlanti"
    ELSEIF pos = 27 THEN
        name = "Ventnor"
    ELSEIF pos = 28 THEN
        name = "Water  "
    ELSEIF pos = 29 THEN
        name = "Marvin "
    ELSEIF pos = 30 THEN
        name = "GOTOJAL"
    ELSEIF pos = 31 THEN
        name = "Pacific"
    ELSEIF pos = 32 THEN
        name = "N.Carol"
    ELSEIF pos = 33 THEN
        name = "Chest  "
    ELSEIF pos = 34 THEN
        name = "PennAve"
    ELSEIF pos = 35 THEN
        name = "ShortRR"
    ELSEIF pos = 36 THEN
        name = "Chance "
    ELSEIF pos = 37 THEN
        name = "ParkPl "
    ELSEIF pos = 38 THEN
        name = "Luxury$"
    ELSEIF pos = 39 THEN
        name = "Boardwk"
    ELSE
        name = "       "
    END IF
    GetShortName = name
END FUNCTION

REM ====================================================================
REM Get space color based on property group
REM ====================================================================
FUNCTION GetSpaceColor(pos AS INTEGER) AS STRING
    DIM propIdx AS INTEGER
    DIM p AS PropData
    DIM clr AS STRING

    propIdx = GetPropertyIndexAt(pos)
    IF propIdx >= 0 THEN
        p = properties.get_Item(propIdx)
        clr = GetGroupColor(p.GetGroup())
    ELSE
        REM Special spaces
        IF pos = 0 THEN
            clr = CLR_BRIGHT_GREEN
        ELSEIF pos = 2 THEN
            clr = CLR_YELLOW
        ELSEIF pos = 4 THEN
            clr = CLR_RED
        ELSEIF pos = 7 THEN
            clr = CLR_MAGENTA
        ELSEIF pos = 10 THEN
            clr = CLR_WHITE
        ELSEIF pos = 17 THEN
            clr = CLR_YELLOW
        ELSEIF pos = 20 THEN
            clr = CLR_GREEN
        ELSEIF pos = 22 THEN
            clr = CLR_MAGENTA
        ELSEIF pos = 30 THEN
            clr = CLR_BRIGHT_RED
        ELSEIF pos = 33 THEN
            clr = CLR_YELLOW
        ELSEIF pos = 36 THEN
            clr = CLR_MAGENTA
        ELSEIF pos = 38 THEN
            clr = CLR_RED
        ELSE
            clr = CLR_WHITE
        END IF
    END IF
    GetSpaceColor = clr
END FUNCTION

REM ====================================================================
REM Check if any player is on a space
REM ====================================================================
FUNCTION GetPlayersOnSpace(pos AS INTEGER) AS STRING
    DIM result AS STRING
    DIM i AS INTEGER
    DIM p AS Player

    result = ""
    i = 0
    WHILE i < players.Count
        p = players.get_Item(i)
        IF p.IsBankrupt() = 0 THEN
            IF p.GetPosition() = pos THEN
                result = result + p.GetToken()
            END IF
        END IF
        i = i + 1
    WEND
    GetPlayersOnSpace = result
END FUNCTION

REM ====================================================================
REM Get owner indicator for a property
REM ====================================================================
FUNCTION GetOwnerIndicator(pos AS INTEGER) AS STRING
    DIM propIdx AS INTEGER
    DIM p AS PropData
    DIM owner AS INTEGER
    DIM result AS STRING
    DIM pl AS Player

    result = " "
    propIdx = GetPropertyIndexAt(pos)
    IF propIdx >= 0 THEN
        p = properties.get_Item(propIdx)
        owner = p.GetOwner()
        IF owner >= 0 THEN
            IF owner < players.Count THEN
                pl = players.get_Item(owner)
                result = pl.GetToken()
            END IF
        END IF
    END IF
    GetOwnerIndicator = result
END FUNCTION

REM ====================================================================
REM Get houses indicator for a property
REM ====================================================================
FUNCTION GetHousesIndicator(pos AS INTEGER) AS STRING
    DIM propIdx AS INTEGER
    DIM p AS PropData
    DIM houses AS INTEGER
    DIM result AS STRING

    result = " "
    propIdx = GetPropertyIndexAt(pos)
    IF propIdx >= 0 THEN
        p = properties.get_Item(propIdx)
        houses = p.GetHouses()
        IF p.IsMortgaged() = 1 THEN
            result = "M"
        ELSEIF houses = 0 THEN
            result = " "
        ELSEIF houses = 1 THEN
            result = "."
        ELSEIF houses = 2 THEN
            result = ":"
        ELSEIF houses = 3 THEN
            result = "="
        ELSEIF houses = 4 THEN
            result = "#"
        ELSEIF houses = 5 THEN
            result = "H"
        END IF
    END IF
    GetHousesIndicator = result
END FUNCTION

REM ====================================================================
REM Draw a single board cell
REM ====================================================================
SUB DrawCell(row AS INTEGER, col AS INTEGER, pos AS INTEGER)
    DIM name AS STRING
    DIM clr AS STRING
    DIM playersStr AS STRING
    DIM ownerInd AS STRING
    DIM housesInd AS STRING

    name = GetShortName(pos)
    clr = GetSpaceColor(pos)
    playersStr = GetPlayersOnSpace(pos)
    ownerInd = GetOwnerIndicator(pos)
    housesInd = GetHousesIndicator(pos)

    REM Draw cell border and name
    PrintAt(row, col, clr, name)
    PrintAt(row + 1, col, CLR_BRIGHT_WHITE, ownerInd)
    PrintAt(row + 1, col + 1, CLR_YELLOW, housesInd)

    REM Show players on this space
    IF LEN(playersStr) > 0 THEN
        PrintAt(row + 1, col + 3, CLR_BRIGHT_CYAN, playersStr)
    END IF
END SUB

REM ====================================================================
REM Draw the full game board
REM ====================================================================
SUB DrawBoard()
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM i AS INTEGER

    REM Top row (positions 20-30): Free Parking to Go To Jail
    row = 2
    col = 2
    i = 20
    WHILE i <= 30
        DrawCell(row, col, i)
        col = col + 8
        i = i + 1
    WEND

    REM Left column (positions 19-11): New York to St. Charles
    row = 4
    col = 2
    i = 19
    WHILE i >= 11
        DrawCell(row, col, i)
        row = row + 2
        i = i - 1
    WEND

    REM Bottom row (positions 10-0): Jail to GO
    row = 22
    col = 2
    i = 10
    WHILE i >= 0
        DrawCell(row, col, i)
        col = col + 8
        i = i - 1
    WEND

    REM Right column (positions 31-39): Pacific to Boardwalk
    row = 4
    col = 82
    i = 31
    WHILE i <= 39
        DrawCell(row, col, i)
        row = row + 2
        i = i + 1
    WEND

    REM Draw board title in center
    PrintAt(10, 35, CLR_BRIGHT_WHITE, "M O N O P O L Y")
    PrintAt(12, 38, CLR_WHITE, "Viper BASIC")
END SUB

REM ====================================================================
REM Draw player status panel
REM ====================================================================
SUB DrawPlayerStatus(playerIdx AS INTEGER)
    DIM p AS Player
    DIM row AS INTEGER
    DIM moneyStr AS STRING
    DIM posStr AS STRING
    DIM statusStr AS STRING
    DIM clr AS STRING

    p = players.get_Item(playerIdx)
    row = 25

    IF p.IsBankrupt() = 1 THEN
        clr = CLR_RED
        statusStr = "BANKRUPT"
    ELSEIF p.IsInJail() = 1 THEN
        clr = CLR_YELLOW
        statusStr = "IN JAIL"
    ELSE
        clr = CLR_BRIGHT_GREEN
        statusStr = "Active"
    END IF

    moneyStr = "$" + STR$(p.GetMoney())
    posStr = GetShortName(p.GetPosition())

    PrintAt(row, 2, CLR_BRIGHT_WHITE, "Player: ")
    PrintAt(row, 10, clr, p.GetName() + " [" + p.GetToken() + "]")
    PrintAt(row + 1, 2, CLR_WHITE, "Money: ")
    PrintAt(row + 1, 10, CLR_BRIGHT_GREEN, moneyStr)
    PrintAt(row + 1, 25, CLR_WHITE, "Position: ")
    PrintAt(row + 1, 36, CLR_CYAN, posStr)
    PrintAt(row + 1, 50, CLR_WHITE, "Status: ")
    PrintAt(row + 1, 58, clr, statusStr)

    IF p.HasJailFreeCard() = 1 THEN
        PrintAt(row + 1, 72, CLR_YELLOW, "[GOOJF]")
    END IF
END SUB

REM ====================================================================
REM Draw property details
REM ====================================================================
SUB ShowPropertyCard(propIdx AS INTEGER)
    DIM p AS PropData
    DIM row AS INTEGER

    IF propIdx < 0 THEN
        EXIT SUB
    END IF
    IF propIdx >= properties.Count THEN
        EXIT SUB
    END IF

    p = properties.get_Item(propIdx)
    row = 28

    PrintAt(row, 2, CLR_BRIGHT_WHITE, "=== PROPERTY CARD ===")
    PrintAt(row + 1, 2, GetGroupColor(p.GetGroup()), p.GetName())
    PrintAt(row + 2, 2, CLR_WHITE, "Group: " + GetGroupName(p.GetGroup()))
    PrintAt(row + 3, 2, CLR_WHITE, "Cost: $" + STR$(p.GetCost()))

    IF p.GetGroup() < GROUP_RAILROAD THEN
        PrintAt(row + 4, 2, CLR_WHITE, "Rent: $" + STR$(p.GetBaseRent()))
        PrintAt(row + 5, 2, CLR_WHITE, "1 House: $" + STR$(p.GetRent()))
        PrintAt(row + 4, 30, CLR_WHITE, "House Cost: $" + STR$(p.GetHouseCost()))
    ELSEIF p.GetGroup() = GROUP_RAILROAD THEN
        PrintAt(row + 4, 2, CLR_WHITE, "1 RR: $25  2 RR: $50  3 RR: $100  4 RR: $200")
    ELSE
        PrintAt(row + 4, 2, CLR_WHITE, "1 Util: 4x dice  2 Utils: 10x dice")
    END IF

    IF p.GetOwner() >= 0 THEN
        DIM pl AS Player
        pl = players.get_Item(p.GetOwner())
        PrintAt(row + 6, 2, CLR_YELLOW, "Owner: " + pl.GetName())
        IF p.IsMortgaged() = 1 THEN
            PrintAt(row + 6, 30, CLR_RED, "[MORTGAGED]")
        ELSEIF p.GetHouses() > 0 THEN
            IF p.GetHouses() = 5 THEN
                PrintAt(row + 6, 30, CLR_BRIGHT_RED, "[HOTEL]")
            ELSE
                PrintAt(row + 6, 30, CLR_GREEN, "[" + STR$(p.GetHouses()) + " Houses]")
            END IF
        END IF
    ELSE
        PrintAt(row + 6, 2, CLR_WHITE, "Owner: Bank (For Sale)")
    END IF
END SUB

REM ====================================================================
REM Show message in message area
REM ====================================================================
SUB ShowMessage(msg AS STRING)
    PrintAt(35, 2, CLR_BRIGHT_WHITE, "                                                                            ")
    PrintAt(35, 2, CLR_BRIGHT_WHITE, msg)
END SUB

REM ====================================================================
REM Show game menu
REM ====================================================================
SUB ShowGameMenu()
    PrintAt(37, 2, CLR_WHITE, "[R]oll  [B]uy  [A]uction  [T]rade  [M]ortgage  [H]ouses  [S]tatus  [Q]uit")
END SUB

REM ====================================================================
REM Get player input
REM ====================================================================
FUNCTION GetInput(prompt AS STRING) AS STRING
    DIM inp AS STRING
    PrintAt(36, 2, CLR_BRIGHT_CYAN, prompt + ": ")
    PrintAt(36, LEN(prompt) + 4, CLR_WHITE, "                                        ")
    GotoXY(36, LEN(prompt) + 4)
    INPUT inp
    GetInput = inp
END FUNCTION

REM ====================================================================
REM Clear message area
REM ====================================================================
SUB ClearMessageArea()
    DIM i AS INTEGER
    i = 35
    WHILE i <= 40
        PrintAt(i, 2, CLR_RESET, "                                                                              ")
        i = i + 1
    WEND
END SUB

REM ====================================================================
REM Show dice roll animation
REM ====================================================================
SUB ShowDiceRoll(d1 AS INTEGER, d2 AS INTEGER)
    DIM i AS INTEGER
    DIM tempD1 AS INTEGER
    DIM tempD2 AS INTEGER

    i = 0
    WHILE i < 5
        tempD1 = INT(RND() * 6) + 1
        tempD2 = INT(RND() * 6) + 1
        PrintAt(14, 40, CLR_BRIGHT_WHITE, "[" + STR$(tempD1) + "] [" + STR$(tempD2) + "]")
        SLEEP 100
        i = i + 1
    WEND

    PrintAt(14, 40, CLR_BRIGHT_YELLOW, "[" + STR$(d1) + "] [" + STR$(d2) + "]")
    PrintAt(15, 40, CLR_WHITE, "Total: " + STR$(d1 + d2))

    IF d1 = d2 THEN
        PrintAt(16, 40, CLR_BRIGHT_GREEN, "DOUBLES!")
    ELSE
        PrintAt(16, 40, CLR_RESET, "        ")
    END IF
END SUB

REM ====================================================================
REM Show list of properties owned by a player
REM ====================================================================
SUB ShowPlayerProperties(playerIdx AS INTEGER)
    DIM i AS INTEGER
    DIM p AS PropData
    DIM row AS INTEGER
    DIM count AS INTEGER
    DIM pl AS Player

    pl = players.get_Item(playerIdx)

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== Properties owned by " + pl.GetName() + " ===")
    row = 3
    count = 0

    i = 0
    WHILE i < properties.Count
        p = properties.get_Item(i)
        IF p.GetOwner() = playerIdx THEN
            PrintAt(row, 2, GetGroupColor(p.GetGroup()), STR$(count + 1) + ". " + p.GetName())
            IF p.IsMortgaged() = 1 THEN
                PrintAt(row, 30, CLR_RED, "[M]")
            ELSEIF p.GetHouses() > 0 THEN
                IF p.GetHouses() = 5 THEN
                    PrintAt(row, 30, CLR_YELLOW, "[H]")
                ELSE
                    PrintAt(row, 30, CLR_GREEN, "[" + STR$(p.GetHouses()) + "]")
                END IF
            END IF
            row = row + 1
            count = count + 1
        END IF
        i = i + 1
    WEND

    IF count = 0 THEN
        PrintAt(row, 2, CLR_WHITE, "No properties owned.")
    END IF

    PrintAt(row + 2, 2, CLR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

REM ====================================================================
REM Show all players summary
REM ====================================================================
SUB ShowAllPlayersStatus()
    DIM i AS INTEGER
    DIM p AS Player
    DIM row AS INTEGER
    DIM netWorth AS INTEGER
    DIM propCount AS INTEGER
    DIM j AS INTEGER
    DIM prop AS PropData

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== PLAYER STATUS ===")
    row = 3

    i = 0
    WHILE i < players.Count
        p = players.get_Item(i)

        REM Calculate net worth
        netWorth = p.GetMoney()
        propCount = 0
        j = 0
        WHILE j < properties.Count
            prop = properties.get_Item(j)
            IF prop.GetOwner() = i THEN
                IF prop.IsMortgaged() = 1 THEN
                    netWorth = netWorth + prop.GetMortgageValue()
                ELSE
                    netWorth = netWorth + prop.GetCost()
                    netWorth = netWorth + (prop.GetHouses() * prop.GetHouseCost())
                END IF
                propCount = propCount + 1
            END IF
            j = j + 1
        WEND

        IF p.IsBankrupt() = 1 THEN
            PrintAt(row, 2, CLR_RED, p.GetName() + " [" + p.GetToken() + "]: BANKRUPT")
        ELSE
            PrintAt(row, 2, CLR_BRIGHT_WHITE, p.GetName() + " [" + p.GetToken() + "]")
            PrintAt(row, 25, CLR_GREEN, "$" + STR$(p.GetMoney()))
            PrintAt(row, 38, CLR_WHITE, "Net: $" + STR$(netWorth))
            PrintAt(row, 55, CLR_CYAN, STR$(propCount) + " props")
            IF p.IsInJail() = 1 THEN
                PrintAt(row, 68, CLR_YELLOW, "[JAIL]")
            END IF
        END IF

        row = row + 1
        i = i + 1
    WEND

    PrintAt(row + 2, 2, CLR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

