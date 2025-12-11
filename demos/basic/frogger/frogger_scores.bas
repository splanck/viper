REM ====================================================================
REM High Score System for Classic Frogger
REM ====================================================================

DIM SCORE_FILE AS STRING
SCORE_FILE = "frogger_highscores.txt"

REM High score data (top 5) - using individual variables instead of arrays
DIM highScoreName1 AS STRING
DIM highScoreName2 AS STRING
DIM highScoreName3 AS STRING
DIM highScoreName4 AS STRING
DIM highScoreName5 AS STRING

DIM highScoreValues(4) AS INTEGER
DIM highScoreCount AS INTEGER

REM ====================================================================
REM Initialize high scores
REM ====================================================================
SUB InitHighScores()
    DIM i AS INTEGER
    highScoreCount = 0

    highScoreName1 = "---"
    highScoreName2 = "---"
    highScoreName3 = "---"
    highScoreName4 = "---"
    highScoreName5 = "---"

    FOR i = 0 TO 4
        highScoreValues(i) = 0
    NEXT i
END SUB

REM ====================================================================
REM Get high score name by index
REM ====================================================================
FUNCTION GetHighScoreName(index AS INTEGER) AS STRING
    IF index = 0 THEN
        GetHighScoreName = highScoreName1
    ELSE IF index = 1 THEN
        GetHighScoreName = highScoreName2
    ELSE IF index = 2 THEN
        GetHighScoreName = highScoreName3
    ELSE IF index = 3 THEN
        GetHighScoreName = highScoreName4
    ELSE IF index = 4 THEN
        GetHighScoreName = highScoreName5
    ELSE
        GetHighScoreName = "---"
    END IF
END FUNCTION

REM ====================================================================
REM Set high score name by index
REM ====================================================================
SUB SetHighScoreName(index AS INTEGER, name AS STRING)
    IF index = 0 THEN
        highScoreName1 = name
    ELSE IF index = 1 THEN
        highScoreName2 = name
    ELSE IF index = 2 THEN
        highScoreName3 = name
    ELSE IF index = 3 THEN
        highScoreName4 = name
    ELSE IF index = 4 THEN
        highScoreName5 = name
    END IF
END SUB

REM ====================================================================
REM Load high scores from disk
REM ====================================================================
SUB LoadHighScores()
    DIM i AS INTEGER
    DIM name AS STRING
    DIM score AS INTEGER

    OPEN SCORE_FILE FOR INPUT AS #1

    FOR i = 0 TO 4
        INPUT #1, name
        INPUT #1, score
        SetHighScoreName(i, name)
        highScoreValues(i) = score
        IF score > 0 THEN
            highScoreCount = highScoreCount + 1
        END IF
    NEXT i

    CLOSE #1
END SUB

REM ====================================================================
REM Save high scores to disk
REM ====================================================================
SUB SaveHighScores()
    DIM i AS INTEGER

    OPEN SCORE_FILE FOR OUTPUT AS #1

    FOR i = 0 TO 4
        PRINT #1, GetHighScoreName(i)
        PRINT #1, highScoreValues(i)
    NEXT i

    CLOSE #1
END SUB

REM ====================================================================
REM Check if score qualifies for high score list
REM ====================================================================
FUNCTION IsHighScore(score AS INTEGER) AS INTEGER
    IF score > highScoreValues(4) OR highScoreCount < 5 THEN
        IsHighScore = 1
    ELSE
        IsHighScore = 0
    END IF
END FUNCTION

REM ====================================================================
REM Add a new high score
REM ====================================================================
SUB AddHighScore(name AS STRING, score AS INTEGER)
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM insertPos AS INTEGER

    REM Find insertion position
    insertPos = 4
    FOR i = 0 TO 4
        IF score > highScoreValues(i) THEN
            insertPos = i
            EXIT FOR
        END IF
    NEXT i

    REM Shift scores down
    FOR i = 4 TO insertPos + 1 STEP -1
        SetHighScoreName(i, GetHighScoreName(i - 1))
        highScoreValues(i) = highScoreValues(i - 1)
    NEXT i

    REM Insert new score
    SetHighScoreName(insertPos, name)
    highScoreValues(insertPos) = score

    REM Update count
    IF highScoreCount < 5 THEN
        highScoreCount = highScoreCount + 1
    END IF

    SaveHighScores()
END SUB

REM ====================================================================
REM Display high scores
REM ====================================================================
SUB DisplayHighScores()
    DIM i AS INTEGER

    ClearScreen()
    PRINT ""
    PRINT "╔════════════════════════════════════════════════════╗"
    PRINT "║          CLASSIC FROGGER - HIGH SCORES            ║"
    PRINT "╚════════════════════════════════════════════════════╝"
    PRINT ""

    IF highScoreCount = 0 THEN
        PRINT "           No high scores yet!"
        PRINT "           Be the first to set a record!"
    ELSE
        PRINT "  RANK    NAME                    SCORE"
        PRINT "  ────────────────────────────────────────"
        FOR i = 0 TO 4
            IF highScoreValues(i) > 0 THEN
                PRINT "   "; STR$(i + 1); "      "; GetHighScoreName(i); "                    "; STR$(highScoreValues(i))
            END IF
        NEXT i
    END IF

    PRINT ""
    PRINT "  Press any key to continue..."
END SUB

REM ====================================================================
REM Get player name for high score
REM ====================================================================
FUNCTION GetPlayerName() AS STRING
    DIM name AS STRING
    DIM ch AS STRING

    PRINT ""
    PRINT "NEW HIGH SCORE!"
    PRINT ""
    PRINT "Enter your name (max 15 chars, press ENTER when done):"
    PRINT "> ";

    name = ""

    WHILE LEN(name) < 15
        ch = INKEY$()
        IF LEN(ch) > 0 THEN
            IF ch = CHR(13) THEN
                REM Enter key
                EXIT WHILE
            ELSE IF ch = CHR(8) THEN
                REM Backspace
                IF LEN(name) > 0 THEN
                    name = LEFT$(name, LEN(name) - 1)
                    PRINT CHR(8); " "; CHR(8);
                END IF
            ELSE IF ch >= " " AND ch <= "~" THEN
                name = name + ch
                PRINT ch;
            END IF
        END IF
        SLEEP 50
    WEND

    IF LEN(name) = 0 THEN
        name = "Anonymous"
    END IF

    GetPlayerName = name
END FUNCTION
