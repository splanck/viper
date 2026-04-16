' ============================================================================
' MODULE: scoreboard.bas
' PURPOSE: Maintain a sorted top-10 list of high scores, with operations to
'          test whether a candidate qualifies, find its rank, and insert it
'          in the correct sorted position. Also handles drawing the
'          scoreboard panel.
'
' WHERE-THIS-FITS: A self-contained "side service" of the game. The main
'          game loop calls `IsHighScore` after game-over and, if true,
'          calls `AddScore` to record the result. `DrawScoreboard` is also
'          used by the main menu's "high scores" screen.
'
' KEY-DESIGN-CHOICES:
'   * FIXED-SIZE PARALLEL ARRAYS. We hold ten slots in three parallel
'     arrays (`Scores`, `Names`, `Levels`) rather than an array of records.
'     This is the idiomatic BASIC pattern for small fixed-size collections
'     and avoids needing a custom Score record type.
'   * SORTED INSERTION. `AddScore` shifts entries down one slot from the
'     bottom up to the rank, then writes the new entry at the rank. This
'     is O(n) per insert, fine for n=10. Maintaining sorted order means
'     `IsHighScore` can be O(1) — it just compares against the last entry.
'   * COLOUR-CODED RANKS. Cyan/green/blue for 1st/2nd/3rd, gray for the
'     rest. This gives the scoreboard a "podium" feel without needing
'     graphics.
'   * SEEDED DEFAULTS. `LoadScores` currently seeds five default rows (HAL,
'     GLaDOS, etc.) instead of reading from disk. The `FileName` field
'     reserves a path for a future on-disk implementation; the API stays
'     the same when persistence is added.
'
' HOW-TO-READ: `IsHighScore` and `GetRank` together explain the sort
'   contract. `AddScore` is the only mutator and shows the shift-then-write
'   pattern. `DrawScoreboard` is pure presentation.
' ============================================================================

Class Scoreboard
    ' Top-10 scores, sorted descending. Index 0 is rank 1.
    Dim Scores(10) As Integer
    ' Player names parallel to Scores.
    Dim Names(10) As String
    ' Final level reached, parallel to Scores.
    Dim Levels(10) As Integer
    ' Number of slots actually populated (0..10). Lets us distinguish
    ' "empty slot" from "real score of 0" in the rendering pass.
    Dim Count As Integer
    ' Reserved for future file-backed persistence. Currently informational.
    Dim FileName As String

    ' --------------------------------------------------------------------
    ' Sub New()
    '   Initialise all ten slots to placeholders ("---", score 0, level 1)
    '   and then call `LoadScores` to seed the demo defaults. Construction
    '   is the only place where we touch every slot uniformly.
    ' --------------------------------------------------------------------
    Sub New()
        Me.Count = 0
        Me.FileName = "vtris_scores.txt"

        Dim i As Integer
        For i = 0 To 9
            Me.Scores(i) = 0
            Me.Names(i) = "---"
            Me.Levels(i) = 1
        Next i

        Me.LoadScores()
    End Sub

    ' --------------------------------------------------------------------
    ' Sub LoadScores()
    '   Today: seeds five well-known demo scores so the high-score screen
    '   has something to display on first run. Tomorrow: read `Me.FileName`
    '   from disk and parse one line per entry. The seeding is intentional
    '   — it makes the demo look "lived in" and gives the player concrete
    '   targets to beat.
    '
    '   When wiring real persistence, mirror this method's contract: leave
    '   `Me.Count` accurate after loading, and never write past index 9.
    ' --------------------------------------------------------------------
    Sub LoadScores()
        Me.Count = 5

        Me.Names(0) = "HAL"
        Me.Scores(0) = 50000
        Me.Levels(0) = 10

        Me.Names(1) = "GLaDOS"
        Me.Scores(1) = 35000
        Me.Levels(1) = 8

        Me.Names(2) = "VIPER"
        Me.Scores(2) = 25000
        Me.Levels(2) = 6

        Me.Names(3) = "BYTE"
        Me.Scores(3) = 15000
        Me.Levels(3) = 4

        Me.Names(4) = "CODE"
        Me.Scores(4) = 10000
        Me.Levels(4) = 3
    End Sub

    ' --------------------------------------------------------------------
    ' Function IsHighScore(score) -> 1 if it qualifies, 0 if not
    '   Two cases:
    '     * The board has fewer than 10 entries: any score qualifies (we
    '       still need to fill the slots).
    '     * The board is full: the score must beat the lowest entry, which
    '       is at index 9 because the array is sorted descending.
    '
    '   This is O(1) — one comparison in the common case. Notice that we
    '   never actually scan the array; sorted invariants pay off here.
    ' --------------------------------------------------------------------
    Function IsHighScore(score As Integer) As Integer
        If Me.Count < 10 Then Return 1
        If score > Me.Scores(9) Then Return 1
        Return 0
    End Function

    ' --------------------------------------------------------------------
    ' Function GetRank(score) -> rank position (1..10) or 0 if not qualifying
    '   Walk the populated entries top-to-bottom; the first entry the new
    '   score beats is the rank we want. If we run out without finding one
    '   AND the board is not yet full, the new score lands at the end
    '   (rank = Count + 1). Otherwise it doesn't qualify (return 0).
    '
    '   Returns a 1-based rank because that's what humans expect ("you came
    '   in third!"). Internally we convert back to a 0-based index via
    '   `rank - 1` in `AddScore`.
    ' --------------------------------------------------------------------
    Function GetRank(score As Integer) As Integer
        Dim i As Integer
        For i = 0 To Me.Count - 1
            If score > Me.Scores(i) Then
                Return i + 1
            End If
        Next i

        ' Tied or lost against everyone we have, but there's still room.
        If Me.Count < 10 Then
            Return Me.Count + 1
        End If

        Return 0  ' Board is full and the new score is not better.
    End Function

    ' --------------------------------------------------------------------
    ' Sub AddScore(playerName, score, level)
    '   Insert a new entry at the correct sorted position by:
    '     1. Computing its rank with `GetRank`. Bail if it doesn't qualify.
    '     2. Shifting every existing entry from the bottom of the board up
    '        to the new rank down by one slot. Walking the loop in reverse
    '        (`Step -1`) avoids trampling source data.
    '     3. Writing the new entry at the rank position.
    '     4. Bumping `Count` if the board wasn't full yet.
    '
    '   Edge case: when rank = 10, the loop body still iterates once with
    '   `i = 9`, but the inner guard `If i > 0` makes the shift a no-op
    '   (it only writes to slot i when i > 0). The final write at slot 9
    '   replaces the bottom entry — exactly the behaviour we want when the
    '   incoming score barely makes the cut.
    ' --------------------------------------------------------------------
    Sub AddScore(playerName As String, score As Integer, level As Integer)
        Dim rank As Integer
        rank = Me.GetRank(score)

        If rank = 0 Then Return  ' Below the cutoff; silently discard.

        ' Shift down to make room at `rank - 1`. We start at the very
        ' bottom (index 9) and walk up to `rank` so that source slots are
        ' read before they are overwritten.
        Dim i As Integer
        For i = 9 To rank Step -1
            If i > 0 Then
                Me.Names(i) = Me.Names(i - 1)
                Me.Scores(i) = Me.Scores(i - 1)
                Me.Levels(i) = Me.Levels(i - 1)
            End If
        Next i

        ' Write the new entry at its (1-based) rank, converted to (0-based).
        Me.Names(rank - 1) = playerName
        Me.Scores(rank - 1) = score
        Me.Levels(rank - 1) = level

        ' Grow Count up to the cap of 10.
        If Me.Count < 10 Then
            Me.Count = Me.Count + 1
        End If
    End Sub

    ' --------------------------------------------------------------------
    ' Sub DrawScoreboard(startRow)
    '   Render the framed top-10 panel anchored at terminal row `startRow`.
    '   The panel is 13 rows tall (top border + title + separator + 10
    '   entries + bottom border).
    '
    '   Each line is laid out as fixed-width columns:
    '     " 1. NAMEPAD     SCORES L#"
    '   so the columns align even when names and scores have different
    '   lengths. Padding is computed manually because BASIC has no native
    '   `printf %-8s` formatter.
    '
    '   First three ranks get gold/silver/bronze-equivalent colours
    '   (cyan/green/blue in the CGA palette since red is reserved for the
    '   game-over banner). Empty slots render in dark gray with placeholder
    '   "---" so the panel size is constant regardless of how many real
    '   scores exist.
    ' --------------------------------------------------------------------
    Sub DrawScoreboard(startRow As Integer)
        Dim i As Integer

        ' Top border + title.
        LOCATE startRow, 1
        COLOR 14, 0
        Print "     ╔════════════════════════════════════╗"

        LOCATE startRow + 1, 1
        Print "     ║  ";
        COLOR 15, 0
        Print "★  ALL-TIME HIGH SCORES  ★";
        LOCATE startRow + 1, 43
        COLOR 14, 0
        Print "║"

        LOCATE startRow + 2, 1
        Print "     ╠════════════════════════════════════╣"

        For i = 0 To 9
            LOCATE startRow + 3 + i, 1
            COLOR 14, 0
            Print "     ║ ";

            If i < Me.Count Then
                ' Podium colours for the top three ranks.
                If i = 0 Then
                    COLOR 11, 0  ' Cyan (1st)
                ElseIf i = 1 Then
                    COLOR 10, 0  ' Green (2nd)
                ElseIf i = 2 Then
                    COLOR 9, 0   ' Blue (3rd)
                Else
                    COLOR 7, 0   ' Gray (4th-10th)
                End If

                ' Rank number, padded to a fixed two-digit slot.
                If i < 9 Then
                    Print " "; (i + 1); ". ";
                Else
                    Print (i + 1); ". ";
                End If

                ' Name, padded to 8 characters with trailing spaces.
                COLOR 15, 0
                Dim nameStr As String
                Dim namePad As Integer
                nameStr = Me.Names(i)
                namePad = 8 - Len(nameStr)
                Print nameStr;
                Dim p As Integer
                For p = 1 To namePad
                    Print " ";
                Next p

                ' Score, right-aligned in an 8-character slot.
                COLOR 11, 0
                Dim scoreStr As String
                Dim scorePad As Integer
                scoreStr = Str$(Me.Scores(i))
                scorePad = 8 - Len(scoreStr)
                For p = 1 To scorePad
                    Print " ";
                Next p
                Print scoreStr;

                ' Final level reached.
                COLOR 10, 0
                Print " L"; Me.Levels(i);
            Else
                ' Empty slot: keep the column widths identical so the
                ' frame's right border stays at column 43.
                COLOR 8, 0
                If i < 9 Then
                    Print " "; (i + 1); ". ";
                Else
                    Print (i + 1); ". ";
                End If
                Print "---     ";
                Print "   ---  ";
                Print " L-";
            End If

            LOCATE startRow + 3 + i, 43
            COLOR 14, 0
            Print "║"
        Next i

        LOCATE startRow + 13, 1
        Print "     ╚════════════════════════════════════╝"

        COLOR 7, 0  ' Reset for downstream prints.
    End Sub
End Class
