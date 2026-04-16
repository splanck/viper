' ============================================================================
' MODULE: vtris.bas
' PURPOSE: vTRIS — a Tetris demo for Viper BASIC. This is the program entry
'          point and the orchestrator for the menu, game loop, and
'          game-over screen. All gameplay logic lives in `pieces.bas`,
'          `board.bas`, and `scoreboard.bas`; this file wires them
'          together and owns the per-frame game state.
'
' WHERE-THIS-FITS: Top of the dependency tree. Reads `Piece`, `Board`, and
'          `Scoreboard` classes via `AddFile` and uses them as a small
'          composition layer. The "main program" at the bottom of the file
'          drives a single state machine: menu -> game -> game-over -> menu.
'
' KEY-DESIGN-CHOICES:
'   * GLOBAL GAME STATE. `GameBoard`, `CurrentPiece`, `NextPiece`,
'     `GameScore`, etc. are file-scope `Dim`s. This is the conventional
'     BASIC pattern for small games — passing them through every Sub would
'     add noise without buying encapsulation. Each Sub treats the globals
'     as both input and output.
'   * QUADRATIC LINE-CLEAR SCORING. `LockPiece` awards
'     `lines * lines * 100` points: 100 / 400 / 900 / 1600 for 1 / 2 / 3 /
'     4 lines. This is a beginner-friendly variant of the classic Tetris
'     scoring (which uses 40, 100, 300, 1200 multiplied by level + 1) —
'     the quadratic curve still rewards multi-line clears strongly without
'     needing a level-multiplier lookup table.
'   * TICK-BASED GRAVITY. The game loop ticks every `Sleep 50` (~20 Hz).
'     `DropCounter` increments every tick, and the piece falls one cell
'     when `DropCounter >= DropSpeed`. `DropSpeed` decreases as the level
'     rises (`30 - GameLevel * 2`, floored at 5), which is how levels
'     translate into difficulty.
'   * NO WALL-KICKS. Rotation is rejected entirely if the new orientation
'     overlaps something. We undo by rotating three more times (since 4
'     CW rotations is the identity). A real Tetris would try a small set
'     of offsets ("wall-kick table") before giving up. The simpler version
'     here is intentional — it keeps the rotation code in `pieces.bas`
'     readable without giving up the player's ability to rotate at all.
'   * HARD DROP IS A TIGHT WHILE LOOP. `If k = " "` walks `MoveDown` until
'     `CanPlace` returns 0, then backs off one cell and locks. There is no
'     animation — this is intentional in classic Tetris (the "thunk" is
'     supposed to feel instant).
'
' HOW-TO-READ: Skip the menu/instructions/help subs (pure rendering) and
'   read in this order:
'     1. `Sub InitGame` — initial state setup (you'll see what the globals
'        mean).
'     2. `Sub SpawnPiece` — turning the "next" piece into the "current"
'        piece, with game-over detection.
'     3. `Sub LockPiece` — the "piece can't fall any further" path:
'        commit, score, spawn next.
'     4. `Sub UpdateLevel` — how scoring drives speed.
'     5. `Sub GameLoop` — the per-tick state machine. Note how each input
'        is "try, then undo if illegal" rather than "look ahead, then act".
'     6. The main program at the bottom — the menu state machine.
' ============================================================================

AddFile "pieces.bas"
AddFile "board.bas"
AddFile "scoreboard.bas"

' === GLOBAL VARIABLES ===
Dim GameBoard As Board
Dim CurrentPiece As Piece
Dim NextPiece As Piece
Dim ScoreBoard As Scoreboard
Dim GameScore As Integer
Dim GameLines As Integer
Dim GameLevel As Integer
Dim GameOver As Integer
Dim DropCounter As Integer
Dim DropSpeed As Integer

' === MAIN MENU ===

' --------------------------------------------------------------------
' Sub ShowMainMenu()
'   Pure rendering — paints the title banner, the menu options, and the
'   prompt "Select option:". Does NOT consume input; the main program
'   loop reads the keypress after this Sub returns. This separation lets
'   the same render function be reused if we ever want to refresh the
'   menu without re-reading input (e.g., after a window resize).
' --------------------------------------------------------------------
Sub ShowMainMenu()
    ' Set background color and clear screen
    COLOR 7, 0
    CLS

    ' Title banner
    LOCATE 2, 1
    COLOR 14, 0
    Print "          ╔════════════════════════════╗"
    LOCATE 3, 1
    Print "          ║                            ║"
    LOCATE 4, 1
    Print "          ║  ";
    COLOR 11, 0
    Print "  ██    ██ █████ █████";
    LOCATE 4, 40
    COLOR 14, 0
    Print "║"
    LOCATE 5, 1
    Print "          ║  ";
    COLOR 11, 0
    Print "  ██    ██   ██  ██  █";
    LOCATE 5, 40
    COLOR 14, 0
    Print "║"
    LOCATE 6, 1
    Print "          ║  ";
    COLOR 11, 0
    Print "  ██    ██   ██  █████";
    LOCATE 6, 40
    COLOR 14, 0
    Print "║"
    LOCATE 7, 1
    Print "          ║  ";
    COLOR 11, 0
    Print "   ██  ██    ██  ██  █";
    LOCATE 7, 40
    COLOR 14, 0
    Print "║"
    LOCATE 8, 1
    Print "          ║  ";
    COLOR 11, 0
    Print "    ████     ██  █████";
    LOCATE 8, 40
    COLOR 14, 0
    Print "║"
    LOCATE 9, 1
    Print "          ║                            ║"
    LOCATE 10, 1
    Print "          ║    ";
    COLOR 10, 0
    Print "Viper BASIC Demo";
    LOCATE 10, 40
    COLOR 14, 0
    Print "║"
    LOCATE 11, 1
    Print "          ╚════════════════════════════╝"

    ' Menu options
    LOCATE 14, 1
    COLOR 15, 0
    Print "               [1] ";
    COLOR 11, 0
    Print "NEW GAME"

    LOCATE 16, 1
    COLOR 15, 0
    Print "               [2] ";
    COLOR 10, 0
    Print "HIGH SCORES"

    LOCATE 18, 1
    COLOR 15, 0
    Print "               [3] ";
    COLOR 9, 0
    Print "INSTRUCTIONS"

    LOCATE 20, 1
    COLOR 15, 0
    Print "               [Q] ";
    COLOR 12, 0
    Print "QUIT"

    LOCATE 23, 1
    COLOR 7, 0
    Print "           Select option: ";
    COLOR 15, 0
End Sub

' --------------------------------------------------------------------
' Sub ShowInstructions()
'   Static "how to play" screen with controls, scoring table, and level
'   progression note. Blocks on `Inkey$()` until a key is pressed, then
'   returns to the main menu loop. The blank-string init + `While k = ""`
'   is BASIC's idiomatic "wait for any key" pattern.
' --------------------------------------------------------------------
Sub ShowInstructions()
    COLOR 7, 0
    CLS

    LOCATE 2, 1
    COLOR 14, 0
    Print "╔════════════════════════════════════════════════════╗"
    LOCATE 3, 1
    Print "║                ";
    COLOR 15, 0
    Print "INSTRUCTIONS";
    LOCATE 3, 54
    COLOR 14, 0
    Print "║"
    LOCATE 4, 1
    Print "╚════════════════════════════════════════════════════╝"

    LOCATE 6, 1
    COLOR 11, 0
    Print "  OBJECTIVE:";
    COLOR 7, 0
    Print " Complete horizontal lines to score points"

    LOCATE 8, 1
    COLOR 11, 0
    Print "  CONTROLS:"
    LOCATE 9, 1
    COLOR 15, 0
    Print "    A/D";
    COLOR 7, 0
    Print " - Move piece left/right"
    LOCATE 10, 1
    COLOR 15, 0
    Print "    W";
    COLOR 7, 0
    Print "   - Rotate piece clockwise"
    LOCATE 11, 1
    COLOR 15, 0
    Print "    S";
    COLOR 7, 0
    Print "   - Soft drop (move down faster)"
    LOCATE 12, 1
    COLOR 15, 0
    Print "    Space";
    COLOR 7, 0
    Print " - Hard drop (instant placement)"
    LOCATE 13, 1
    COLOR 15, 0
    Print "    Q";
    COLOR 7, 0
    Print "   - Quit to main menu"

    LOCATE 15, 1
    COLOR 11, 0
    Print "  SCORING:"
    LOCATE 16, 1
    COLOR 7, 0
    Print "    1 Line  = 100 points"
    LOCATE 17, 1
    Print "    2 Lines = 400 points"
    LOCATE 18, 1
    Print "    3 Lines = 900 points"
    LOCATE 19, 1
    Print "    4 Lines = 1600 points"

    LOCATE 21, 1
    COLOR 11, 0
    Print "  LEVELS:";
    COLOR 7, 0
    Print " Speed increases every 10 lines"

    LOCATE 24, 1
    COLOR 8, 0
    Print "Press any key to return to menu...";
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' --------------------------------------------------------------------
' Sub ShowHighScores()
'   Renders the top-10 panel via `ScoreBoard.DrawScoreboard(3)` (anchored
'   at terminal row 3) and waits for a key. The scoreboard data was
'   already loaded by the global `ScoreBoard = New Scoreboard()` at the
'   bottom of the file, so this Sub is purely a viewer.
' --------------------------------------------------------------------
Sub ShowHighScores()
    COLOR 7, 0
    CLS
    ScoreBoard.DrawScoreboard(3)

    LOCATE 23, 1
    COLOR 8, 0
    Print "Press any key to return to menu..."
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' === GAME INITIALIZATION ===

' --------------------------------------------------------------------
' Sub InitGame()
'   Reset every per-game piece of state to its starting value:
'     * Fresh empty Board (the constructor sets up the sentinel walls).
'     * Score / lines / level zeroed; level starts at 1.
'     * `DropSpeed = 30` — the piece falls one cell every 30 ticks (~1.5s
'       at 20 Hz) at level 1. `UpdateLevel` later decreases this.
'     * `CurrentPiece` and `NextPiece` are both spawned with random
'       types. `CurrentPiece` is positioned at column 4 (visual centre)
'       to override the constructor's default of column 3.
'   This is called once each time the player picks "NEW GAME" from the
'   menu. Notice that `ScoreBoard` is NOT reset here — it persists
'   across games as a session-wide best-of leaderboard.
' --------------------------------------------------------------------
Sub InitGame()
    GameBoard = New Board()
    GameScore = 0
    GameLines = 0
    GameLevel = 1
    GameOver = 0
    DropSpeed = 30
    DropCounter = 0

    ' Create first and next pieces
    Dim pieceType As Integer
    pieceType = Int(Rnd() * 7)
    CurrentPiece = New Piece(pieceType)
    CurrentPiece.PosX = 4
    CurrentPiece.PosY = 0

    pieceType = Int(Rnd() * 7)
    NextPiece = New Piece(pieceType)
End Sub

' --------------------------------------------------------------------
' Sub SpawnPiece()
'   Promote the queued `NextPiece` into `CurrentPiece` (positioned at
'   spawn coords 4, 0), then generate a fresh random `NextPiece` for the
'   preview. Detects game-over by asking the board whether the just-
'   spawned piece can occupy its starting position — if not, the well is
'   too full to accept new pieces.
'
'   This is the standard "block out" loss condition in Tetris.
'   "Top out" (a piece locking with cells above the visible playfield)
'   would require additional bookkeeping; vTRIS uses the simpler
'   block-out check.
' --------------------------------------------------------------------
Sub SpawnPiece()
    ' Create new piece based on next piece type
    Dim pieceType As Integer
    pieceType = NextPiece.PieceType
    CurrentPiece = New Piece(pieceType)
    CurrentPiece.PosX = 4
    CurrentPiece.PosY = 0

    ' Check if can spawn (game over if not)
    If GameBoard.CanPlace(CurrentPiece) = 0 Then
        GameOver = 1
    End If

    ' Generate new next piece
    pieceType = Int(Rnd() * 7)
    NextPiece = New Piece(pieceType)
End Sub

' --------------------------------------------------------------------
' Sub DrawUI()
'   Renders the right-side info panel: score, lines cleared, level, the
'   "next piece" preview, and the controls cheat sheet.
'
'   Positioning notes:
'     * The board occupies columns 1..22 (left border, 10 cells x 2 chars,
'       right border). The UI panel starts at column 26 to leave a gap.
'     * Each numeric value is followed by trailing spaces (e.g.,
'       "Print GameScore; '            ';") so that a smaller number
'       overwrites the digits left over from a larger previous value.
'       This is the BASIC equivalent of "clear to end of field" — without
'       it, dropping from a 5-digit score back to a 4-digit one would
'       leave a stale digit on screen.
'     * The next-piece preview reads `NextPiece.Shape` directly and
'       paints filled cells in the piece's colour. This is the same
'       4x4 rendering convention used by `Board.DrawPiece`.
' --------------------------------------------------------------------
Sub DrawUI()
    ' Right side panel
    LOCATE 2, 26
    COLOR 14, 0
    Print "╔══════════════════╗"

    LOCATE 3, 26
    Print "║ ";
    COLOR 15, 0
    Print "vTRIS v2.0";
    LOCATE 3, 45
    COLOR 14, 0
    Print "║"

    LOCATE 4, 26
    Print "╠══════════════════╣"

    ' Score
    LOCATE 5, 26
    COLOR 14, 0
    Print "║ ";
    COLOR 11, 0
    Print "SCORE:";
    LOCATE 5, 45
    COLOR 14, 0
    Print "║"
    LOCATE 6, 26
    Print "║ ";
    COLOR 15, 0
    Print GameScore; "            ";
    LOCATE 6, 45
    COLOR 14, 0
    Print "║"

    ' Lines
    LOCATE 7, 26
    Print "║ ";
    COLOR 10, 0
    Print "LINES:";
    LOCATE 7, 45
    COLOR 14, 0
    Print "║"
    LOCATE 8, 26
    Print "║ ";
    COLOR 15, 0
    Print GameLines; "            ";
    LOCATE 8, 45
    COLOR 14, 0
    Print "║"

    ' Level
    LOCATE 9, 26
    Print "║ ";
    COLOR 9, 0
    Print "LEVEL:";
    LOCATE 9, 45
    COLOR 14, 0
    Print "║"
    LOCATE 10, 26
    Print "║ ";
    COLOR 15, 0
    Print GameLevel; "            ";
    LOCATE 10, 45
    COLOR 14, 0
    Print "║"

    LOCATE 11, 26
    Print "╠══════════════════╣"

    ' Next piece preview
    LOCATE 12, 26
    Print "║ ";
    COLOR 13, 0
    Print "NEXT:";
    LOCATE 12, 45
    COLOR 14, 0
    Print "║"

    ' Draw next piece preview (rows 13-16)
    Dim i As Integer, j As Integer
    For i = 0 To 3
        LOCATE 13 + i, 26
        COLOR 14, 0
        Print "║  ";

        For j = 0 To 3
            If NextPiece.Shape(i, j) = 1 Then
                COLOR NextPiece.PieceColor, 0
                Print "██";
            Else
                COLOR 7, 0
                Print "  ";
            End If
        Next j

        LOCATE 13 + i, 45
        COLOR 14, 0
        Print "║"
    Next i

    LOCATE 17, 26
    Print "╠══════════════════╣"

    ' Controls
    LOCATE 18, 26
    Print "║ ";
    COLOR 8, 0
    Print "A/D - Move";
    LOCATE 18, 45
    COLOR 14, 0
    Print "║"
    LOCATE 19, 26
    Print "║ ";
    COLOR 8, 0
    Print "W - Rotate";
    LOCATE 19, 45
    COLOR 14, 0
    Print "║"
    LOCATE 20, 26
    Print "║ ";
    COLOR 8, 0
    Print "S - Drop";
    LOCATE 20, 45
    COLOR 14, 0
    Print "║"
    LOCATE 21, 26
    Print "║ ";
    COLOR 8, 0
    Print "Space - Hard";
    LOCATE 21, 45
    COLOR 14, 0
    Print "║"
    LOCATE 22, 26
    Print "║ ";
    COLOR 8, 0
    Print "Q - Quit";
    LOCATE 22, 45
    COLOR 14, 0
    Print "║"

    LOCATE 23, 26
    Print "╚══════════════════╝"

    COLOR 7, 0
End Sub

' --------------------------------------------------------------------
' Sub UpdateLevel()
'   Recomputes `GameLevel` from `GameLines` and, if it has gone up,
'   reduces `DropSpeed` to make the piece fall faster.
'
'   Formula:
'     newLevel = floor(GameLines / 10) + 1
'     DropSpeed = 30 - GameLevel * 2   (clamped at floor of 5)
'
'   Reading those backwards: at level 1 the piece falls every 28 ticks
'   (~1.4 s); by level 12 the floor of 5 ticks (~250 ms) is hit and the
'   game stops getting faster. This is a deliberate ceiling — beyond
'   that point the player would rely on hard-drops anyway, so increasing
'   the auto-drop rate further wouldn't change the experience.
' --------------------------------------------------------------------
Sub UpdateLevel()
    ' Increase level every 10 lines
    Dim newLevel As Integer
    newLevel = Int(GameLines / 10) + 1

    If newLevel > GameLevel Then
        GameLevel = newLevel

        ' Increase speed (decrease drop delay)
        DropSpeed = 30 - (GameLevel * 2)
        If DropSpeed < 5 Then
            DropSpeed = 5
        End If
    End If
End Sub

' --------------------------------------------------------------------
' Sub LockPiece()
'   The "piece can no longer fall" path. Three steps:
'     1. Commit the piece's cells to the board (`PlacePiece`).
'     2. Check for completed lines and apply the quadratic score bonus
'        (`linesCleared * linesCleared * 100`).
'     3. Spawn the next piece via `SpawnPiece`, which also handles the
'        game-over check.
'
'   The quadratic score curve is the educational substitute for the
'   classic Tetris BPS-table scoring. The values land at:
'     1 line  ->  100
'     2 lines ->  400  (4x)
'     3 lines ->  900  (9x)
'     4 lines -> 1600 (16x — a "Tetris")
'   The 4x and 9x jumps already match real Tetris's "single < double <
'   triple" intuition; the 16x for a Tetris is roughly half what classic
'   Tetris gives but still a big enough reward to make the strategy
'   visible.
' --------------------------------------------------------------------
Sub LockPiece()
    Dim linesCleared As Integer

    GameBoard.PlacePiece(CurrentPiece)
    linesCleared = GameBoard.CheckLines()

    If linesCleared > 0 Then
        ' Score based on lines cleared
        GameScore = GameScore + (linesCleared * linesCleared * 100)
        GameLines = GameLines + linesCleared
        UpdateLevel()
    End If

    SpawnPiece()
End Sub

' === MAIN GAME LOOP ===

' --------------------------------------------------------------------
' Sub GameLoop()
'   The per-tick state machine. Each iteration:
'     1. Read one keypress (non-blocking — `Inkey$()` returns "" if no
'        key is queued).
'     2. Apply the corresponding action (move, rotate, soft-drop,
'        hard-drop, or quit).
'     3. Increment `DropCounter`; when it crosses `DropSpeed`, auto-drop.
'     4. Redraw the board, the active piece, and the UI panel.
'     5. Sleep 50 ms — caps the loop at ~20 Hz.
'
'   The "try then undo" pattern shows up everywhere here:
'     CurrentPiece.MoveLeft()
'     If GameBoard.CanPlace(CurrentPiece) = 0 Then CurrentPiece.MoveRight()
'   This is simpler than "look ahead and decide" — the piece is the
'   source of truth; the board is a validator. If validation fails, undo
'   is always a single inverse operation.
'
'   The rotation undo is the same idea applied four-cyclically: rotating
'   3 more times returns to the original (4 CW = identity). This is
'   cheaper than storing a snapshot of `Shape` and copying it back.
'
'   Hard-drop is a tight `While CanPlace = 1: MoveDown` loop. Notice that
'   we then `PosY = PosY - 1` to back off the last (illegal) step before
'   locking — a subtle off-by-one that catches beginners every time.
' --------------------------------------------------------------------
Sub GameLoop()
    Dim k As String

    COLOR 7, 0
    CLS

    While GameOver = 0
        ' Get input
        k = Inkey$()

        ' Handle input
        If k = "a" Or k = "A" Then
            CurrentPiece.MoveLeft()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveRight()
            End If
        End If

        If k = "d" Or k = "D" Then
            CurrentPiece.MoveRight()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.MoveLeft()
            End If
        End If

        If k = "w" Or k = "W" Then
            CurrentPiece.RotateClockwise()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                ' Undo rotation
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
                CurrentPiece.RotateClockwise()
            End If
        End If

        If k = "s" Or k = "S" Then
            CurrentPiece.MoveDown()
            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If

        If k = " " Then
            ' Hard drop - instantly move to bottom
            While GameBoard.CanPlace(CurrentPiece) = 1
                CurrentPiece.MoveDown()
            Wend
            CurrentPiece.PosY = CurrentPiece.PosY - 1
            LockPiece()
        End If

        If k = "q" Or k = "Q" Then
            GameOver = 1
        End If

        ' Auto drop
        DropCounter = DropCounter + 1
        If DropCounter >= DropSpeed Then
            DropCounter = 0
            CurrentPiece.MoveDown()

            If GameBoard.CanPlace(CurrentPiece) = 0 Then
                CurrentPiece.PosY = CurrentPiece.PosY - 1
                LockPiece()
            End If
        End If

        ' Draw everything
        GameBoard.DrawBoard()
        GameBoard.DrawPiece(CurrentPiece)
        DrawUI()

        Sleep 50
    Wend
End Sub

' --------------------------------------------------------------------
' Sub GameOverScreen()
'   Displays the final stats (score, lines, level) and, if the score
'   qualifies, congratulates the player and inserts the entry into the
'   `ScoreBoard`. Player name is hardcoded to "YOU" for the demo — a
'   real implementation would call a 3-letter input routine here, but
'   that adds parsing complexity that distracts from the gameplay
'   tutorial.
'
'   Like the other static screens, this blocks on `Inkey$()` until the
'   player acknowledges, then returns to the main menu loop.
' --------------------------------------------------------------------
Sub GameOverScreen()
    COLOR 7, 0
    CLS

    LOCATE 8, 1
    COLOR 12, 0
    Print "     ╔════════════════════════════════════════╗"
    LOCATE 9, 1
    Print "     ║                                        ║"
    LOCATE 10, 1
    Print "     ║           ";
    COLOR 15, 0
    Print "GAME OVER!";
    LOCATE 10, 47
    COLOR 12, 0
    Print "║"
    LOCATE 11, 1
    Print "     ║                                        ║"
    LOCATE 12, 1
    Print "     ╚════════════════════════════════════════╝"

    LOCATE 14, 1
    COLOR 11, 0
    Print "            Final Score: ";
    COLOR 15, 0
    Print GameScore

    LOCATE 15, 1
    COLOR 10, 0
    Print "            Lines Cleared: ";
    COLOR 15, 0
    Print GameLines

    LOCATE 16, 1
    COLOR 9, 0
    Print "            Level Reached: ";
    COLOR 15, 0
    Print GameLevel

    ' Check for high score
    If ScoreBoard.IsHighScore(GameScore) = 1 Then
        LOCATE 18, 1
        COLOR 14, 0
        Print "        ★ NEW HIGH SCORE! ★"

        LOCATE 19, 1
        COLOR 15, 0
        Print "        Enter name (3 letters): "

        ' For demo, use a default name
        Dim playerName As String
        playerName = "YOU"

        ScoreBoard.AddScore(playerName, GameScore, GameLevel)
    End If

    LOCATE 22, 1
    COLOR 8, 0
    Print "Press any key to return to menu...";
    COLOR 7, 0

    Dim k As String
    k = ""
    While k = ""
        k = Inkey$()
    Wend
End Sub

' ============================================================================
' MAIN PROGRAM
' ----------------------------------------------------------------------------
' The classic "menu state machine" idiom: an outer `While running = 1`
' loop displays the menu, reads a single key, and dispatches to one of
' four handlers (new game / high scores / instructions / quit). When
' the game ends, control naturally returns to the top of the loop and
' redraws the menu.
'
' `Randomize` seeds the RNG once at startup. `ScoreBoard` is built once
' (so high scores survive across multiple plays in a single session).
' Game state (`GameBoard`, `CurrentPiece`, etc.) is rebuilt each game
' inside `InitGame`.
'
' On exit, the screen is cleared and a "thanks for playing" banner is
' printed before the program returns to the OS.
' ============================================================================

Randomize

' Initialize scoreboard (needs to persist across games)
ScoreBoard = New Scoreboard()

Dim menuChoice As String
Dim running As Integer
running = 1

While running = 1
    ShowMainMenu()

    menuChoice = ""
    While menuChoice = ""
        menuChoice = Inkey$()
    Wend

    If menuChoice = "1" Then
        InitGame()
        GameLoop()
        GameOverScreen()
    ElseIf menuChoice = "2" Then
        ShowHighScores()
    ElseIf menuChoice = "3" Then
        ShowInstructions()
    ElseIf menuChoice = "q" Or menuChoice = "Q" Then
        running = 0
    End If
Wend

' Exit message
CLS
LOCATE 12, 1
COLOR 11, 0
Print "     Thanks for playing vTRIS!"
LOCATE 13, 1
COLOR 7, 0
Print "     A Viper BASIC Demonstration"
LOCATE 15, 1
