' ═══════════════════════════════════════════════════════════
' SCOREBOARD.BAS - High score management
' ═══════════════════════════════════════════════════════════

' Constants as globals (parser doesn't support Const in array dims)
Dim MAX_HIGH_SCORES As Integer

Sub InitScoreboardConstants()
    MAX_HIGH_SCORES = 10
End Sub

Class ScoreEntry
    Dim Name As String
    Dim Score As Integer

    Sub New()
        Me.Name = "---"
        Me.Score = 0
    End Sub
End Class

Class ScoreBoard
    Dim Entries(10) As ScoreEntry  ' MAX_HIGH_SCORES = 10

    Sub New()
        Dim i As Integer
        For i = 0 To MAX_HIGH_SCORES - 1
            Me.Entries(i) = New ScoreEntry
        Next i
        ' Initialize with default scores
        Me.InitDefaults()
    End Sub

    ' Set up default high scores
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

    ' Check if score qualifies for high score list
    Function IsHighScore(score As Integer) As Integer
        If score > Me.Entries(MAX_HIGH_SCORES - 1).Score Then
            IsHighScore = 1
        Else
            IsHighScore = 0
        End If
    End Function

    ' Add a new high score (assumes it qualifies)
    Sub AddScore(name As String, score As Integer)
        Dim i As Integer
        Dim j As Integer
        Dim insertPos As Integer

        ' Find insertion position
        insertPos = MAX_HIGH_SCORES
        For i = 0 To MAX_HIGH_SCORES - 1
            If score > Me.Entries(i).Score Then
                insertPos = i
                Exit For
            End If
        Next i

        ' Shift lower scores down
        If insertPos < MAX_HIGH_SCORES Then
            For j = MAX_HIGH_SCORES - 1 To insertPos + 1 Step -1
                Me.Entries(j).Name = Me.Entries(j - 1).Name
                Me.Entries(j).Score = Me.Entries(j - 1).Score
            Next j

            ' Insert new score
            Me.Entries(insertPos).Name = name
            Me.Entries(insertPos).Score = score
        End If
    End Sub

    ' Draw the scoreboard
    Sub Draw(startRow As Integer)
        Dim i As Integer
        Dim row As Integer

        ' Header
        Viper.Terminal.SetColor(14, 0)
        Viper.Terminal.SetPosition(startRow, 20)
        PRINT "=== HIGH SCORES ==="

        Viper.Terminal.SetColor(11, 0)
        Viper.Terminal.SetPosition(startRow + 2, 15)
        PRINT "RANK    NAME          SCORE"
        Viper.Terminal.SetPosition(startRow + 3, 15)
        PRINT "---------------------------"

        ' Entries
        For i = 0 To MAX_HIGH_SCORES - 1
            row = startRow + 5 + i

            ' Top 3 in gold/green, rest in gray
            If i < 3 Then
                Viper.Terminal.SetColor(10, 0)
            Else
                Viper.Terminal.SetColor(7, 0)
            End If

            Viper.Terminal.SetPosition(row, 15)
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
