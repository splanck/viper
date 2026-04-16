' ============================================================================
' MODULE: frogger_scores.bas
' PURPOSE: Top-5 high-score system with file-backed persistence, plus an
'          interactive name-entry prompt for a qualifying score.
'
' WHERE-THIS-FITS: Standalone service. Called from frogger.bas at three
'          moments:
'            1. At startup: InitHighScores -> SaveHighScores -> LoadHighScores
'               (defensive sequence — write defaults if no file yet, then
'               reload them).
'            2. At game-over: IsHighScore + AddHighScore.
'            3. From the main menu: DisplayHighScores.
'
' KEY-DESIGN-CHOICES:
'   * FIVE INDIVIDUAL NAME VARIABLES. `highScoreName1..5` are separate
'     `DIM ... AS STRING` variables rather than a `DIM Names(5) AS
'     STRING` array. Comment in the original source explained this is
'     a parser workaround in the dialect — string arrays aren't fully
'     supported. `GetHighScoreName` / `SetHighScoreName` hide the
'     five-way branch so the rest of the code indexes by integer.
'   * PARALLEL ARRAYS (MIXED). Scores live in `highScoreValues(4)` (a
'     real integer array) while names live in the five discrete
'     variables. The hybrid arrangement is a consequence of the
'     parser limitation; consumers treat the two storage shapes
'     uniformly through the getter/setter pair.
'   * FILE-BACKED PERSISTENCE. `LoadHighScores` and `SaveHighScores`
'     read/write `frogger_highscores.txt` in a plain-text format:
'     alternating name / score lines. This keeps the format human-
'     editable for debugging. File opens use classic BASIC #1 handle
'     syntax.
'   * SORTED INSERTION. `AddHighScore` finds the insertion point with a
'     linear scan (first slot the new score strictly beats), shifts
'     lower entries down, and writes at the insert position. The
'     `SaveHighScores` call at the end persists the change immediately
'     so a crash doesn't lose the new score.
'   * CHR(13) / CHR(8) KEYBOARD HANDLING. `GetPlayerName` implements a
'     simple line editor: ENTER (CHR(13)) commits, BACKSPACE (CHR(8))
'     deletes one char (+ echoes the "\b \b" sequence for visual
'     erase), printable ASCII (" " through "~") appends.
'
' HOW-TO-READ: Initialization (trivial) -> getter/setter pair (index
'   -> variable map) -> Load/Save (file format) -> IsHighScore /
'   AddHighScore (sorted-insertion contract) -> DisplayHighScores /
'   GetPlayerName (presentation).
' ============================================================================

DIM SCORE_FILE AS STRING
SCORE_FILE = "frogger_highscores.txt"

' Five individual name strings — the dialect's parser doesn't accept
' string arrays here, so we unroll the storage and fake the array API
' via GetHighScoreName / SetHighScoreName below.
DIM highScoreName1 AS STRING
DIM highScoreName2 AS STRING
DIM highScoreName3 AS STRING
DIM highScoreName4 AS STRING
DIM highScoreName5 AS STRING

' Integer score array, parallel to the five name variables above.
' highScoreValues(i) pairs with GetHighScoreName(i).
DIM highScoreValues(4) AS INTEGER
DIM highScoreCount AS INTEGER

' Zero the table. Called once at startup before the file is loaded so
' the defaults are in place even if the file doesn't exist.
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

' Index -> variable getter. Returns "---" for out-of-range indices so
' render code doesn't need a guard.
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

' Index -> variable setter. Silently no-ops for out-of-range indices.
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

' Read the top-5 table from SCORE_FILE. Format is:
'     line 1: name of rank 1
'     line 2: score of rank 1
'     line 3: name of rank 2
'     ... etc for 5 entries.
' Counts non-zero scores to re-establish highScoreCount after load.
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

' Write the top-5 table to SCORE_FILE using the same alternating-line
' format as LoadHighScores. Called after every AddHighScore so a crash
' doesn't lose the new entry.
SUB SaveHighScores()
    DIM i AS INTEGER

    OPEN SCORE_FILE FOR OUTPUT AS #1

    FOR i = 0 TO 4
        PRINT #1, GetHighScoreName(i)
        PRINT #1, highScoreValues(i)
    NEXT i

    CLOSE #1
END SUB

' Qualification check. A score qualifies if it beats the last (lowest)
' entry OR the table isn't full yet. O(1) thanks to the sorted
' invariant.
FUNCTION IsHighScore(score AS INTEGER) AS INTEGER
    IF score > highScoreValues(4) OR highScoreCount < 5 THEN
        IsHighScore = 1
    ELSE
        IsHighScore = 0
    END IF
END FUNCTION

' Insert a new entry at its sorted position. Same shape as the
' centipede / vtris scoreboards: scan for insertion point, shift down,
' write. Persists to disk before returning.
SUB AddHighScore(name AS STRING, score AS INTEGER)
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM insertPos AS INTEGER

    ' Find the first slot this score beats.
    insertPos = 4
    FOR i = 0 TO 4
        IF score > highScoreValues(i) THEN
            insertPos = i
            EXIT FOR
        END IF
    NEXT i

    ' Shift lower entries down one slot (reverse loop to avoid trampling).
    FOR i = 4 TO insertPos + 1 STEP -1
        SetHighScoreName(i, GetHighScoreName(i - 1))
        highScoreValues(i) = highScoreValues(i - 1)
    NEXT i

    ' Overwrite the freed slot with the new entry.
    SetHighScoreName(insertPos, name)
    highScoreValues(insertPos) = score

    ' Track populated count (caps at 5).
    IF highScoreCount < 5 THEN
        highScoreCount = highScoreCount + 1
    END IF

    SaveHighScores()
END SUB

' Render the high-score panel. Shows "No high scores yet!" if nothing
' has been recorded, otherwise a formatted table. The trailing "press
' any key" prompt tells the main menu to WAIT before redrawing.
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

' Interactive name-entry prompt for a new high score. Reads characters
' one at a time until ENTER is pressed or the name reaches 15
' characters. Handles BACKSPACE (CHR(8)) in-place with the conventional
' "\b \b" erase trick. Accepts only printable ASCII (" " through "~")
' so control characters can't sneak in. Returns "Anonymous" if the
' player commits an empty name.
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
                ' ENTER — commit the current name.
                EXIT WHILE
            ELSE IF ch = CHR(8) THEN
                ' BACKSPACE — pop the last char and visually erase.
                IF LEN(name) > 0 THEN
                    name = LEFT$(name, LEN(name) - 1)
                    PRINT CHR(8); " "; CHR(8);
                END IF
            ELSE IF ch >= " " AND ch <= "~" THEN
                ' Printable ASCII — append and echo.
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
