REM ====================================================================
REM High Score Management for Centipede
REM Uses individual STRING variables due to string array bug (BUG-OOP-011)
REM ====================================================================

REM High score names (individual variables to work around string array bug)
DIM hsName0 AS STRING
DIM hsName1 AS STRING
DIM hsName2 AS STRING
DIM hsName3 AS STRING
DIM hsName4 AS STRING
DIM hsName5 AS STRING
DIM hsName6 AS STRING
DIM hsName7 AS STRING
DIM hsName8 AS STRING
DIM hsName9 AS STRING

REM High score values and levels (integer arrays work fine)
DIM highScores(9) AS INTEGER
DIM highScoreLevels(9) AS INTEGER

REM Getter for high score name
FUNCTION GetHSName(idx AS INTEGER) AS STRING
    IF idx = 0 THEN
        GetHSName = hsName0
    ELSEIF idx = 1 THEN
        GetHSName = hsName1
    ELSEIF idx = 2 THEN
        GetHSName = hsName2
    ELSEIF idx = 3 THEN
        GetHSName = hsName3
    ELSEIF idx = 4 THEN
        GetHSName = hsName4
    ELSEIF idx = 5 THEN
        GetHSName = hsName5
    ELSEIF idx = 6 THEN
        GetHSName = hsName6
    ELSEIF idx = 7 THEN
        GetHSName = hsName7
    ELSEIF idx = 8 THEN
        GetHSName = hsName8
    ELSEIF idx = 9 THEN
        GetHSName = hsName9
    ELSE
        GetHSName = "---"
    END IF
END FUNCTION

REM Setter for high score name
SUB SetHSName(idx AS INTEGER, n AS STRING)
    IF idx = 0 THEN
        hsName0 = n
    ELSEIF idx = 1 THEN
        hsName1 = n
    ELSEIF idx = 2 THEN
        hsName2 = n
    ELSEIF idx = 3 THEN
        hsName3 = n
    ELSEIF idx = 4 THEN
        hsName4 = n
    ELSEIF idx = 5 THEN
        hsName5 = n
    ELSEIF idx = 6 THEN
        hsName6 = n
    ELSEIF idx = 7 THEN
        hsName7 = n
    ELSEIF idx = 8 THEN
        hsName8 = n
    ELSEIF idx = 9 THEN
        hsName9 = n
    END IF
END SUB

REM Initialize high scores with defaults
SUB InitHighScores()
    DIM i AS INTEGER

    FOR i = 0 TO 9
        SetHSName(i, "---")
        highScores(i) = (10 - i) * 1000
        highScoreLevels(i) = 10 - i
    NEXT i

    REM Some default entries
    SetHSName(0, "ACE")
    highScores(0) = 50000
    highScoreLevels(0) = 10

    SetHSName(1, "PRO")
    highScores(1) = 40000
    highScoreLevels(1) = 8

    SetHSName(2, "VIP")
    highScores(2) = 30000
    highScoreLevels(2) = 6
END SUB

REM Check if score qualifies for high score table
FUNCTION IsHighScore(score AS INTEGER) AS INTEGER
    IF score > highScores(9) THEN
        IsHighScore = 1
    ELSE
        IsHighScore = 0
    END IF
END FUNCTION

REM Add a new high score
SUB AddHighScore(playerName AS STRING, score AS INTEGER, lvl AS INTEGER)
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM insertPos AS INTEGER
    DIM tempName AS STRING

    REM Find insertion position
    insertPos = -1
    FOR i = 0 TO 9
        IF score > highScores(i) THEN
            insertPos = i
            EXIT FOR
        END IF
    NEXT i

    IF insertPos >= 0 THEN
        REM Shift scores down
        FOR j = 8 TO insertPos STEP -1
            tempName = GetHSName(j)
            SetHSName(j + 1, tempName)
            highScores(j + 1) = highScores(j)
            highScoreLevels(j + 1) = highScoreLevels(j)
        NEXT j

        REM Insert new score
        SetHSName(insertPos, playerName)
        highScores(insertPos) = score
        highScoreLevels(insertPos) = lvl
    END IF
END SUB

REM Display high scores
SUB DisplayHighScores()
    DIM i AS INTEGER
    DIM rankStr AS STRING
    DIM scoreStr AS STRING
    DIM lvlStr AS STRING
    DIM nameStr AS STRING
    DIM row AS INTEGER
    DIM tempScore AS INTEGER
    DIM tempLevel AS INTEGER

    ClearScreen()
    PRINT ""
    PrintColorAt(2, 25, COLOR_BRIGHT_YELLOW, "=== HIGH SCORES ===")
    PRINT ""
    PrintColorAt(4, 20, COLOR_WHITE, "RANK   NAME    SCORE    LEVEL")
    PrintColorAt(5, 20, COLOR_WHITE, "----   ----    -----    -----")

    FOR i = 0 TO 9
        row = 6 + i
        rankStr = STR$(i + 1)
        nameStr = GetHSName(i)
        tempScore = highScores(i)
        tempLevel = highScoreLevels(i)
        scoreStr = STR$(tempScore)
        lvlStr = STR$(tempLevel)

        IF i < 3 THEN
            PrintColorAt(row, 20, COLOR_BRIGHT_GREEN, rankStr)
        ELSE
            PrintColorAt(row, 20, COLOR_WHITE, rankStr)
        END IF

        PrintColorAt(row, 27, COLOR_CYAN, nameStr)
        PrintColorAt(row, 35, COLOR_YELLOW, scoreStr)
        PrintColorAt(row, 46, COLOR_WHITE, lvlStr)
    NEXT i

    PrintColorAt(18, 20, COLOR_WHITE, "Press any key to continue...")
END SUB

REM Get player name for high score (3 characters)
FUNCTION GetPlayerName() AS STRING
    DIM name AS STRING
    DIM key AS STRING
    DIM charCount AS INTEGER
    DIM keyCode AS INTEGER
    DIM isValidKey AS INTEGER

    name = ""
    charCount = 0

    PrintColorAt(20, 20, COLOR_BRIGHT_CYAN, "Enter your initials (3 letters): ")

    WHILE charCount < 3
        key = INKEY$()
        IF LEN(key) > 0 THEN
            REM Accept A-Z only
            keyCode = ASC(key)
            isValidKey = 0
            IF keyCode >= 65 THEN
                IF keyCode <= 90 THEN
                    isValidKey = 1
                END IF
            END IF
            IF keyCode >= 97 THEN
                IF keyCode <= 122 THEN
                    isValidKey = 1
                END IF
            END IF

            IF isValidKey = 1 THEN
                IF keyCode >= 97 THEN
                    REM Convert to uppercase
                    key = CHR(keyCode - 32)
                END IF
                name = name + key
                charCount = charCount + 1
                PrintColorAt(20, 54 + charCount, COLOR_BRIGHT_WHITE, key)
            END IF
        END IF
        SLEEP 50
    WEND

    GetPlayerName = name
END FUNCTION

REM Save high scores (simplified - no file I/O due to compiler bugs)
SUB SaveHighScores()
    REM High scores are memory-only for now due to string array bugs (BUG-OOP-011)
    REM and complex OR expression bugs in file parsing code
END SUB

REM Load high scores (simplified - no file I/O due to compiler bugs)
SUB LoadHighScores()
    REM High scores are memory-only for now
    REM Scores reset when game restarts
END SUB
