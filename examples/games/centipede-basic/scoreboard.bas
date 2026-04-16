' ============================================================================
' MODULE: scoreboard.bas
' PURPOSE: Persistent top-10 high-score table. Handles qualification
'          checks, sorted insertion, and the rendered "HIGH SCORES" panel.
'
' WHERE-THIS-FITS: Standalone "side service". Unlike field/creature/player
'          which are ticked every frame, the scoreboard is consulted only
'          at game-over (IsHighScore / AddScore) and from the main menu's
'          "high scores" screen (Draw). It does not reference any global
'          state from the other modules.
'
' KEY-DESIGN-CHOICES:
'   * ARRAY OF RECORDS. Each slot is a `ScoreEntry` with Name + Score.
'     This differs from vtris's parallel-arrays approach — the record
'     form is slightly nicer when the entries grow more than two fields,
'     but both shapes are idiomatic BASIC.
'   * SORTED-DESCENDING INVARIANT. `Entries(0)` is rank 1 (highest score).
'     This makes `IsHighScore` O(1) — just compare against
'     `Entries(MAX - 1)`. `AddScore` maintains the invariant by finding
'     the insertion point with a linear scan and then shift-inserting.
'   * SEEDED DEFAULTS. `InitDefaults` pre-populates a ladder of themed
'     names so the high-score screen looks "lived in" on first launch.
'     This is a UX touch — empty high-score tables feel buggy to players.
'
' HOW-TO-READ: `IsHighScore` and `AddScore` together form the insertion
'   API contract. `Draw` is pure presentation.
' ============================================================================

' Constants as globals (the parser rejects Const inside array-size decls).
Dim MAX_HIGH_SCORES As Integer

Sub InitScoreboardConstants()
    MAX_HIGH_SCORES = 10
End Sub

' ===========================================================================
' Class ScoreEntry
'   A single high-score row. Just two fields plus a sensible default
'   placeholder for rows that have not been claimed yet.
' ===========================================================================
Class ScoreEntry
    Dim Name As String
    Dim Score As Integer

    Sub New()
        Me.Name = "---"
        Me.Score = 0
    End Sub
End Class

' ===========================================================================
' Class ScoreBoard
'   The fixed-size top-10 table plus the qualification/insertion API and
'   the renderer.
' ===========================================================================
Class ScoreBoard
    Dim Entries(10) As ScoreEntry  ' Sized to MAX_HIGH_SCORES.

    ' Allocate ten entry records (each with its own placeholder default)
    ' then seed the ladder with themed demo scores.
    Sub New()
        Dim i As Integer
        For i = 0 To MAX_HIGH_SCORES - 1
            Me.Entries(i) = New ScoreEntry
        Next i
        Me.InitDefaults()
    End Sub

    ' Populate the table with themed placeholder rows so the high-scores
    ' screen is not empty on first launch. Easy to swap out later when
    ' file-backed persistence is wired in.
    Sub InitDefaults()
        Me.Entries(0).Name = "ACE"
        Me.Entries(0).Score = 10000
        Me.Entries(1).Name = "PRO"
        Me.Entries(1).Score = 8000
        Me.Entries(2).Name = "VET"
        Me.Entries(2).Score = 6000
        Me.Entries(3).Name = "HOT"
        Me.Entries(3).Score = 4000
        Me.Entries(4).Name = "NEW"
        Me.Entries(4).Score = 2000
        Me.Entries(5).Name = "BAS"
        Me.Entries(5).Score = 1000
        Me.Entries(6).Name = "VIP"
        Me.Entries(6).Score = 500
        Me.Entries(7).Name = "..."
        Me.Entries(7).Score = 250
        Me.Entries(8).Name = "..."
        Me.Entries(8).Score = 100
        Me.Entries(9).Name = "..."
        Me.Entries(9).Score = 50
    End Sub

    ' O(1) qualification check. Because the array is sorted descending,
    ' the last entry is the lowest score that made the cut. If the new
    ' score beats that, it qualifies.
    Function IsHighScore(score As Integer) As Integer
        If score > Me.Entries(MAX_HIGH_SCORES - 1).Score Then
            IsHighScore = 1
        Else
            IsHighScore = 0
        End If
    End Function

    ' Insert a new entry at its correct sorted position. Two phases:
    '   1. Linear scan for the insertion point (first slot the new score
    '      strictly beats). If nothing is beaten, the score doesn't
    '      qualify — `insertPos` stays at MAX and no insert happens.
    '   2. Shift every entry from `insertPos+1 .. MAX-1` down one slot
    '      (walking in reverse so we don't trample source data), then
    '      write the new record at `insertPos`.
    ' The bottom entry is overwritten when the new score barely qualifies.
    Sub AddScore(name As String, score As Integer)
        Dim i As Integer
        Dim j As Integer
        Dim insertPos As Integer

        ' Linear scan for the insertion index.
        insertPos = MAX_HIGH_SCORES
        For i = 0 To MAX_HIGH_SCORES - 1
            If score > Me.Entries(i).Score Then
                insertPos = i
                Exit For
            End If
        Next i

        ' Shift down + insert, but only if the score qualifies.
        If insertPos < MAX_HIGH_SCORES Then
            For j = MAX_HIGH_SCORES - 1 To insertPos + 1 Step -1
                Me.Entries(j).Name = Me.Entries(j - 1).Name
                Me.Entries(j).Score = Me.Entries(j - 1).Score
            Next j

            Me.Entries(insertPos).Name = name
            Me.Entries(insertPos).Score = score
        End If
    End Sub

    ' Render the scoreboard panel. Header + column labels + ten rows with
    ' rank / name / score. Top-3 rows in bright green ("gold-ish"), the
    ' rest in gray. The panel is anchored at `startRow` so callers can
    ' place it anywhere on-screen.
    Sub Draw(startRow As Integer)
        Dim i As Integer
        Dim row As Integer

        ' Title banner.
        Viper.Terminal.SetColor(14, 0)
        Viper.Terminal.SetPosition(startRow, 20)
        PRINT "=== HIGH SCORES ==="

        ' Column headers + separator.
        Viper.Terminal.SetColor(11, 0)
        Viper.Terminal.SetPosition(startRow + 2, 15)
        PRINT "RANK    NAME          SCORE"
        Viper.Terminal.SetPosition(startRow + 3, 15)
        PRINT "---------------------------"

        ' One row per entry.
        For i = 0 To MAX_HIGH_SCORES - 1
            row = startRow + 5 + i

            ' Podium colouring: top 3 in bright green, rest in gray.
            If i < 3 Then
                Viper.Terminal.SetColor(10, 0)
            Else
                Viper.Terminal.SetColor(7, 0)
            End If

            Viper.Terminal.SetPosition(row, 15)
            ' Leading space pads single-digit ranks so rank 10 lines up.
            If i < 9 Then
                PRINT " "
            End If
            PRINT i + 1
            PRINT ".     "
            PRINT Me.Entries(i).Name
            PRINT "           "
            PRINT Me.Entries(i).Score
            PRINT "     "
        Next i
    End Sub
End Class
