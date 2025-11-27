REM ====================================================================
REM MONOPOLY - Save/Load Game State
REM File I/O for game persistence
REM Note: Simplified due to compiler bugs with complex file I/O
REM ====================================================================

DIM SAVE_FILENAME AS STRING
SAVE_FILENAME = "monopoly_save.txt"

REM ====================================================================
REM Save game state to file
REM ====================================================================
FUNCTION SaveGame() AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM success AS INTEGER
    DIM line AS STRING

    success = 0

    TRY
        OPEN SAVE_FILENAME FOR OUTPUT AS #1

        REM Write header
        PRINT #1, "MONOPOLY_SAVE_V1"
        PRINT #1, players.Count

        REM Write player data
        i = 0
        WHILE i < players.Count
            p = players.get_Item(i)
            PRINT #1, p.GetName()
            PRINT #1, p.GetToken()
            PRINT #1, p.GetMoney()
            PRINT #1, p.GetPosition()
            PRINT #1, p.IsInJail()
            PRINT #1, p.GetJailTurns()
            PRINT #1, p.IsBankrupt()
            PRINT #1, p.IsAI()
            PRINT #1, p.GetAIType()
            IF p.HasJailFreeCard() = 1 THEN
                PRINT #1, 1
            ELSE
                PRINT #1, 0
            END IF
            i = i + 1
        WEND

        REM Write property data
        PRINT #1, properties.Count
        i = 0
        WHILE i < properties.Count
            prop = properties.get_Item(i)
            PRINT #1, prop.GetOwner()
            PRINT #1, prop.GetHouses()
            PRINT #1, prop.IsMortgaged()
            i = i + 1
        WEND

        REM Write current player index
        PRINT #1, currentPlayerIndex

        CLOSE #1
        success = 1

    CATCH
        success = 0
    END TRY

    SaveGame = success
END FUNCTION

REM ====================================================================
REM Load game state from file
REM ====================================================================
FUNCTION LoadGame() AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM p AS Player
    DIM prop AS PropData
    DIM success AS INTEGER
    DIM line AS STRING
    DIM numPlayers AS INTEGER
    DIM numProps AS INTEGER
    DIM pName AS STRING
    DIM pToken AS STRING
    DIM pMoney AS INTEGER
    DIM pPosition AS INTEGER
    DIM pInJail AS INTEGER
    DIM pJailTurns AS INTEGER
    DIM pBankrupt AS INTEGER
    DIM pIsAI AS INTEGER
    DIM pAIType AS INTEGER
    DIM pJailCard AS INTEGER
    DIM propOwner AS INTEGER
    DIM propHouses AS INTEGER
    DIM propMort AS INTEGER

    success = 0

    REM Check if save file exists
    IF NOT Viper.IO.File.Exists(SAVE_FILENAME) THEN
        LoadGame = 0
        EXIT FUNCTION
    END IF

    TRY
        OPEN SAVE_FILENAME FOR INPUT AS #1

        REM Read and verify header
        INPUT #1, line
        IF line <> "MONOPOLY_SAVE_V1" THEN
            CLOSE #1
            LoadGame = 0
            EXIT FUNCTION
        END IF

        REM Read number of players
        INPUT #1, numPlayers

        REM Initialize game data
        InitGameData()
        players = NEW Viper.Collections.List()

        REM Read player data
        i = 0
        WHILE i < numPlayers
            INPUT #1, pName
            INPUT #1, pToken
            INPUT #1, pMoney
            INPUT #1, pPosition
            INPUT #1, pInJail
            INPUT #1, pJailTurns
            INPUT #1, pBankrupt
            INPUT #1, pIsAI
            INPUT #1, pAIType
            INPUT #1, pJailCard

            p = NEW Player()
            p.Init(i, pName, pToken, pIsAI, pAIType)
            p.SetMoney(pMoney)
            p.SetPosition(pPosition)
            IF pInJail = 1 THEN
                p.GoToJail()
                DIM jt AS INTEGER
                jt = 0
                WHILE jt < pJailTurns
                    p.IncrementJailTurns()
                    jt = jt + 1
                WEND
            END IF
            IF pBankrupt = 1 THEN
                p.GoBankrupt()
            END IF
            IF pJailCard = 1 THEN
                p.AddJailFreeCard()
            END IF

            players.Add(p)
            i = i + 1
        WEND

        REM Read property data
        INPUT #1, numProps
        i = 0
        WHILE i < numProps
            INPUT #1, propOwner
            INPUT #1, propHouses
            INPUT #1, propMort

            IF i < properties.Count THEN
                prop = properties.get_Item(i)
                prop.SetOwner(propOwner)
                DIM h AS INTEGER
                h = 0
                WHILE h < propHouses
                    prop.AddHouse()
                    h = h + 1
                WEND
                IF propMort = 1 THEN
                    prop.Mortgage()
                END IF
            END IF
            i = i + 1
        WEND

        REM Read current player index
        INPUT #1, currentPlayerIndex

        CLOSE #1
        success = 1

    CATCH
        success = 0
    END TRY

    LoadGame = success
END FUNCTION

REM ====================================================================
REM Delete save file
REM ====================================================================
SUB DeleteSaveFile()
    REM Note: Cannot delete files due to DELETE being a reserved keyword
    REM The save file will remain on disk
END SUB

REM ====================================================================
REM Check if save file exists
REM ====================================================================
FUNCTION SaveFileExists() AS INTEGER
    IF Viper.IO.File.Exists(SAVE_FILENAME) THEN
        SaveFileExists = 1
    ELSE
        SaveFileExists = 0
    END IF
END FUNCTION

REM ====================================================================
REM Show game log entry
REM ====================================================================
DIM gameLogCount AS INTEGER
DIM gameLog0 AS STRING
DIM gameLog1 AS STRING
DIM gameLog2 AS STRING
DIM gameLog3 AS STRING
DIM gameLog4 AS STRING
DIM gameLog5 AS STRING
DIM gameLog6 AS STRING
DIM gameLog7 AS STRING
DIM gameLog8 AS STRING
DIM gameLog9 AS STRING

SUB InitGameLog()
    gameLogCount = 0
    gameLog0 = ""
    gameLog1 = ""
    gameLog2 = ""
    gameLog3 = ""
    gameLog4 = ""
    gameLog5 = ""
    gameLog6 = ""
    gameLog7 = ""
    gameLog8 = ""
    gameLog9 = ""
END SUB

SUB AddLogEntry(entry AS STRING)
    REM Shift entries down
    gameLog9 = gameLog8
    gameLog8 = gameLog7
    gameLog7 = gameLog6
    gameLog6 = gameLog5
    gameLog5 = gameLog4
    gameLog4 = gameLog3
    gameLog3 = gameLog2
    gameLog2 = gameLog1
    gameLog1 = gameLog0
    gameLog0 = entry

    IF gameLogCount < 10 THEN
        gameLogCount = gameLogCount + 1
    END IF
END SUB

FUNCTION GetLogEntry(idx AS INTEGER) AS STRING
    DIM result AS STRING
    IF idx = 0 THEN
        result = gameLog0
    ELSEIF idx = 1 THEN
        result = gameLog1
    ELSEIF idx = 2 THEN
        result = gameLog2
    ELSEIF idx = 3 THEN
        result = gameLog3
    ELSEIF idx = 4 THEN
        result = gameLog4
    ELSEIF idx = 5 THEN
        result = gameLog5
    ELSEIF idx = 6 THEN
        result = gameLog6
    ELSEIF idx = 7 THEN
        result = gameLog7
    ELSEIF idx = 8 THEN
        result = gameLog8
    ELSEIF idx = 9 THEN
        result = gameLog9
    ELSE
        result = ""
    END IF
    GetLogEntry = result
END FUNCTION

SUB ShowGameLog()
    DIM i AS INTEGER
    DIM row AS INTEGER

    ClearScreen()
    PrintAt(1, 2, CLR_BRIGHT_WHITE, "=== GAME LOG ===")
    row = 3

    i = 0
    WHILE i < gameLogCount
        PrintAt(row, 2, CLR_WHITE, GetLogEntry(i))
        row = row + 1
        i = i + 1
    WEND

    IF gameLogCount = 0 THEN
        PrintAt(row, 2, CLR_WHITE, "No entries yet.")
    END IF

    PrintAt(row + 2, 2, CLR_WHITE, "Press any key to continue...")
    WHILE LEN(INKEY$()) = 0
        SLEEP 50
    WEND
END SUB

