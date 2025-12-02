' saveload.bas - Save/Load and High Score system
' Uses Viper.IO.File for persistence

' ============================================================================
' SAVE DATA CLASS
' ============================================================================
CLASS SaveData
    ' Player info
    DIM playerX AS INTEGER
    DIM playerY AS INTEGER
    DIM playerClass AS INTEGER
    DIM playerLevel AS INTEGER
    DIM playerXP AS INTEGER
    DIM playerHP AS INTEGER
    DIM playerMaxHP AS INTEGER
    DIM playerMana AS INTEGER
    DIM playerMaxMana AS INTEGER
    DIM playerHunger AS INTEGER
    DIM playerGold AS INTEGER

    ' Stats
    DIM stats(5) AS INTEGER

    ' Game state
    DIM floorLevel AS INTEGER
    DIM turnCount AS INTEGER
    DIM gameWon AS INTEGER

    SUB Init()
        playerX = 0
        playerY = 0
        playerClass = CLASS_WARRIOR
        playerLevel = 1
        playerXP = 0
        playerHP = 100
        playerMaxHP = 100
        playerMana = 50
        playerMaxMana = 50
        playerHunger = HUNGER_MAX
        playerGold = 0
        floorLevel = 1
        turnCount = 0
        gameWon = 0

        DIM i AS INTEGER
        FOR i = 0 TO 5
            stats(i) = 10
        NEXT i
    END SUB

    ' Setters
    SUB SetPlayerPos(x AS INTEGER, y AS INTEGER)
        playerX = x
        playerY = y
    END SUB

    SUB SetPlayerClass(c AS INTEGER)
        playerClass = c
    END SUB

    SUB SetPlayerLevel(l AS INTEGER)
        playerLevel = l
    END SUB

    SUB SetPlayerXP(xp AS INTEGER)
        playerXP = xp
    END SUB

    SUB SetPlayerHP(hp AS INTEGER, maxHP AS INTEGER)
        playerHP = hp
        playerMaxHP = maxHP
    END SUB

    SUB SetPlayerMana(mp AS INTEGER, maxMP AS INTEGER)
        playerMana = mp
        playerMaxMana = maxMP
    END SUB

    SUB SetPlayerHunger(h AS INTEGER)
        playerHunger = h
    END SUB

    SUB SetPlayerGold(g AS INTEGER)
        playerGold = g
    END SUB

    SUB SetStat(idx AS INTEGER, val AS INTEGER)
        IF idx >= 0 THEN
            IF idx <= 5 THEN
                stats(idx) = val
            END IF
        END IF
    END SUB

    SUB SetFloorLevel(f AS INTEGER)
        floorLevel = f
    END SUB

    SUB SetTurnCount(t AS INTEGER)
        turnCount = t
    END SUB

    SUB SetGameWon(w AS INTEGER)
        gameWon = w
    END SUB

    ' Getters
    FUNCTION GetPlayerX() AS INTEGER
        GetPlayerX = playerX
    END FUNCTION

    FUNCTION GetPlayerY() AS INTEGER
        GetPlayerY = playerY
    END FUNCTION

    FUNCTION GetPlayerClass() AS INTEGER
        GetPlayerClass = playerClass
    END FUNCTION

    FUNCTION GetPlayerLevel() AS INTEGER
        GetPlayerLevel = playerLevel
    END FUNCTION

    FUNCTION GetPlayerXP() AS INTEGER
        GetPlayerXP = playerXP
    END FUNCTION

    FUNCTION GetPlayerHP() AS INTEGER
        GetPlayerHP = playerHP
    END FUNCTION

    FUNCTION GetPlayerMaxHP() AS INTEGER
        GetPlayerMaxHP = playerMaxHP
    END FUNCTION

    FUNCTION GetPlayerMana() AS INTEGER
        GetPlayerMana = playerMana
    END FUNCTION

    FUNCTION GetPlayerMaxMana() AS INTEGER
        GetPlayerMaxMana = playerMaxMana
    END FUNCTION

    FUNCTION GetPlayerHunger() AS INTEGER
        GetPlayerHunger = playerHunger
    END FUNCTION

    FUNCTION GetPlayerGold() AS INTEGER
        GetPlayerGold = playerGold
    END FUNCTION

    FUNCTION GetStat(idx AS INTEGER) AS INTEGER
        IF idx >= 0 THEN
            IF idx <= 5 THEN
                GetStat = stats(idx)
                EXIT FUNCTION
            END IF
        END IF
        GetStat = 10
    END FUNCTION

    FUNCTION GetFloorLevel() AS INTEGER
        GetFloorLevel = floorLevel
    END FUNCTION

    FUNCTION GetTurnCount() AS INTEGER
        GetTurnCount = turnCount
    END FUNCTION

    FUNCTION GetGameWon() AS INTEGER
        GetGameWon = gameWon
    END FUNCTION

    ' Serialize to string
    FUNCTION ToSaveString() AS STRING
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        sb.Append("ROGUELIKE_SAVE_V1")
        sb.Append(CHR(10))

        sb.Append(Viper.Convert.ToString(playerX))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerY))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerClass))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerLevel))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerXP))
        sb.Append(CHR(10))

        sb.Append(Viper.Convert.ToString(playerHP))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerMaxHP))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerMana))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerMaxMana))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerHunger))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(playerGold))
        sb.Append(CHR(10))

        DIM i AS INTEGER
        FOR i = 0 TO 5
            sb.Append(Viper.Convert.ToString(stats(i)))
            IF i < 5 THEN sb.Append(",")
        NEXT i
        sb.Append(CHR(10))

        sb.Append(Viper.Convert.ToString(floorLevel))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(turnCount))
        sb.Append(",")
        sb.Append(Viper.Convert.ToString(gameWon))
        sb.Append(CHR(10))

        ToSaveString = sb.ToString()
    END FUNCTION

    ' Parse from string (simplified - just checks header for now)
    FUNCTION FromSaveString(data AS STRING) AS INTEGER
        ' Check header
        IF Viper.String.Left(data, 17) <> "ROGUELIKE_SAVE_V1" THEN
            FromSaveString = 0
            EXIT FUNCTION
        END IF

        ' In a real implementation, we'd parse all the values
        ' For now, just validate it's our format
        FromSaveString = 1
    END FUNCTION
END CLASS

' ============================================================================
' HIGH SCORE ENTRY
' ============================================================================
CLASS HighScoreEntry
    DIM playerName AS STRING
    DIM score AS INTEGER
    DIM floorReached AS INTEGER
    DIM characterClass AS INTEGER
    DIM characterLevel AS INTEGER
    DIM won AS INTEGER

    SUB Init(pName AS STRING, pScore AS INTEGER, pFloor AS INTEGER, pClass AS INTEGER, pLevel AS INTEGER, pWon AS INTEGER)
        playerName = pName
        score = pScore
        floorReached = pFloor
        characterClass = pClass
        characterLevel = pLevel
        won = pWon
    END SUB

    FUNCTION GetName() AS STRING
        GetName = playerName
    END FUNCTION

    FUNCTION GetScore() AS INTEGER
        GetScore = score
    END FUNCTION

    FUNCTION GetFloor() AS INTEGER
        GetFloor = floorReached
    END FUNCTION

    FUNCTION GetClass() AS INTEGER
        GetClass = characterClass
    END FUNCTION

    FUNCTION GetLevel() AS INTEGER
        GetLevel = characterLevel
    END FUNCTION

    FUNCTION HasWon() AS INTEGER
        HasWon = won
    END FUNCTION

    FUNCTION GetClassName() AS STRING
        SELECT CASE characterClass
            CASE CLASS_WARRIOR : GetClassName = "Warrior"
            CASE CLASS_MAGE : GetClassName = "Mage"
            CASE CLASS_ROGUE : GetClassName = "Rogue"
            CASE CLASS_RANGER : GetClassName = "Ranger"
            CASE ELSE : GetClassName = "Unknown"
        END SELECT
    END FUNCTION
END CLASS

' ============================================================================
' HIGH SCORE TABLE
' ============================================================================
CLASS HighScoreTable
    DIM entries(9) AS HighScoreEntry
    DIM entryCount AS INTEGER
    DIM maxEntries AS INTEGER

    SUB Init()
        entryCount = 0
        maxEntries = 10
    END SUB

    ' Add a new score
    FUNCTION AddScore(pName AS STRING, pScore AS INTEGER, pFloor AS INTEGER, _
                       pClass AS INTEGER, pLevel AS INTEGER, pWon AS INTEGER) AS INTEGER
        ' Check if this score makes the list
        DIM position AS INTEGER
        DIM i AS INTEGER
        position = -1

        ' Find position
        FOR i = 0 TO entryCount - 1
            IF pScore > entries(i).GetScore() THEN
                position = i
                EXIT FOR
            END IF
        NEXT i

        IF position < 0 THEN
            IF entryCount < maxEntries THEN
                position = entryCount
            ELSE
                AddScore = -1
                EXIT FUNCTION
            END IF
        END IF

        ' Shift entries down
        IF position < entryCount THEN
            FOR i = entryCount - 1 TO position STEP -1
                IF i + 1 < maxEntries THEN
                    entries(i + 1) = entries(i)
                END IF
            NEXT i
        END IF

        ' Insert new entry
        DIM newEntry AS HighScoreEntry
        newEntry = NEW HighScoreEntry()
        newEntry.Init(pName, pScore, pFloor, pClass, pLevel, pWon)
        entries(position) = newEntry

        IF entryCount < maxEntries THEN
            entryCount = entryCount + 1
        END IF

        AddScore = position
    END FUNCTION

    FUNCTION GetEntryCount() AS INTEGER
        GetEntryCount = entryCount
    END FUNCTION

    FUNCTION GetEntry(idx AS INTEGER) AS HighScoreEntry
        IF idx >= 0 THEN
            IF idx < entryCount THEN
                GetEntry = entries(idx)
                EXIT FUNCTION
            END IF
        END IF
        ' Return dummy entry
        DIM dummy AS HighScoreEntry
        dummy = NEW HighScoreEntry()
        dummy.Init("---", 0, 0, 0, 0, 0)
        GetEntry = dummy
    END FUNCTION

    ' Calculate score
    FUNCTION CalculateScore(floor AS INTEGER, level AS INTEGER, gold AS INTEGER, _
                             turns AS INTEGER, won AS INTEGER) AS INTEGER
        DIM score AS INTEGER

        ' Base score from floor progress
        score = floor * 1000

        ' Bonus for level
        score = score + level * 500

        ' Gold contributes
        score = score + gold

        ' Efficiency bonus (fewer turns = better)
        IF turns > 0 THEN
            score = score + 50000 / turns
        END IF

        ' Victory bonus
        IF won = 1 THEN
            score = score + 100000
        END IF

        CalculateScore = score
    END FUNCTION

    ' Serialize to string for saving
    FUNCTION ToSaveString() AS STRING
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        sb.Append("ROGUELIKE_HISCORE_V1")
        sb.Append(CHR(10))
        sb.Append(Viper.Convert.ToString(entryCount))
        sb.Append(CHR(10))

        DIM i AS INTEGER
        FOR i = 0 TO entryCount - 1
            DIM e AS HighScoreEntry
            e = entries(i)
            sb.Append(e.GetName())
            sb.Append("|")
            sb.Append(Viper.Convert.ToString(e.GetScore()))
            sb.Append("|")
            sb.Append(Viper.Convert.ToString(e.GetFloor()))
            sb.Append("|")
            sb.Append(Viper.Convert.ToString(e.GetClass()))
            sb.Append("|")
            sb.Append(Viper.Convert.ToString(e.GetLevel()))
            sb.Append("|")
            sb.Append(Viper.Convert.ToString(e.HasWon()))
            sb.Append(CHR(10))
        NEXT i

        ToSaveString = sb.ToString()
    END FUNCTION
END CLASS

' ============================================================================
' SAVE/LOAD MANAGER
' ============================================================================
CLASS SaveLoadManager
    DIM savePath AS STRING
    DIM highScorePath AS STRING

    SUB Init()
        savePath = "roguelike_save.dat"
        highScorePath = "roguelike_scores.dat"
    END SUB

    ' Check if save exists
    FUNCTION SaveExists() AS INTEGER
        SaveExists = Viper.IO.File.Exists(savePath)
    END FUNCTION

    ' Save game state
    FUNCTION SaveGame(saveData AS SaveData) AS INTEGER
        DIM content AS STRING
        content = saveData.ToSaveString()
        Viper.IO.File.WriteAllText(savePath, content)
        SaveGame = 1
    END FUNCTION

    ' Load game state
    FUNCTION LoadGame(saveData AS SaveData) AS INTEGER
        IF Viper.IO.File.Exists(savePath) = 0 THEN
            LoadGame = 0
            EXIT FUNCTION
        END IF

        DIM content AS STRING
        content = Viper.IO.File.ReadAllText(savePath)

        IF saveData.FromSaveString(content) = 0 THEN
            LoadGame = 0
            EXIT FUNCTION
        END IF

        LoadGame = 1
    END FUNCTION

    ' Delete save (permadeath)
    SUB DeleteSave()
        IF Viper.IO.File.Exists(savePath) = 1 THEN
            Viper.IO.File.Delete(savePath)
        END IF
    END SUB

    ' Save high scores
    SUB SaveHighScores(table AS HighScoreTable)
        DIM content AS STRING
        content = table.ToSaveString()
        Viper.IO.File.WriteAllText(highScorePath, content)
    END SUB

    ' Load high scores
    FUNCTION LoadHighScores(table AS HighScoreTable) AS INTEGER
        IF Viper.IO.File.Exists(highScorePath) = 0 THEN
            LoadHighScores = 0
            EXIT FUNCTION
        END IF

        ' In a full implementation, we'd parse the file
        ' For now, just check it exists
        LoadHighScores = 1
    END FUNCTION
END CLASS
