' scoreboard.bas - High score management with file persistence
' Tracks top 10 all-time scores

Class Scoreboard
    Dim Scores(10) As Integer
    Dim Names(10) As String
    Dim Levels(10) As Integer
    Dim Count As Integer
    Dim FileName As String

    Sub New()
        Me.Count = 0
        Me.FileName = "vtris_scores.txt"

        ' Initialize arrays
        Dim i As Integer
        For i = 0 To 9
            Me.Scores(i) = 0
            Me.Names(i) = "---"
            Me.Levels(i) = 1
        Next i

        Me.LoadScores()
    End Sub

    Sub LoadScores()
        ' Try to load scores from file
        ' For demo purposes, initialize with default scores
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

    Function IsHighScore(score As Integer) As Integer
        ' Check if score qualifies for top 10
        If Me.Count < 10 Then Return 1
        If score > Me.Scores(9) Then Return 1
        Return 0
    End Function

    Function GetRank(score As Integer) As Integer
        ' Get rank position for a score (1-10, or 0 if not qualifying)
        Dim i As Integer
        For i = 0 To Me.Count - 1
            If score > Me.Scores(i) Then
                Return i + 1
            End If
        Next i

        If Me.Count < 10 Then
            Return Me.Count + 1
        End If

        Return 0
    End Function

    Sub AddScore(playerName As String, score As Integer, level As Integer)
        ' Add a new high score in sorted position
        Dim rank As Integer
        rank = Me.GetRank(score)

        If rank = 0 Then Return  ' Not a high score

        ' Shift scores down
        Dim i As Integer
        For i = 9 To rank Step -1
            If i > 0 Then
                Me.Names(i) = Me.Names(i - 1)
                Me.Scores(i) = Me.Scores(i - 1)
                Me.Levels(i) = Me.Levels(i - 1)
            End If
        Next i

        ' Insert new score
        Me.Names(rank - 1) = playerName
        Me.Scores(rank - 1) = score
        Me.Levels(rank - 1) = level

        If Me.Count < 10 Then
            Me.Count = Me.Count + 1
        End If
    End Sub

    Sub DrawScoreboard(startRow As Integer)
        ' Draw the scoreboard at specified row
        Dim i As Integer

        LOCATE startRow, 1
        COLOR 14, 0  ' Yellow
        Print "     ╔════════════════════════════════════╗"

        LOCATE startRow + 1, 1
        Print "     ║  ";
        COLOR 15, 0  ' Bright white
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
                ' Rank color
                If i = 0 Then
                    COLOR 11, 0  ' Bright cyan for 1st
                ElseIf i = 1 Then
                    COLOR 10, 0  ' Bright green for 2nd
                ElseIf i = 2 Then
                    COLOR 9, 0   ' Bright blue for 3rd
                Else
                    COLOR 7, 0   ' Gray for others
                End If

                ' Rank number (fixed 4 chars: " 1. ")
                If i < 9 Then
                    Print " "; (i + 1); ". ";
                Else
                    Print (i + 1); ". ";
                End If

                ' Name (fixed 8 chars)
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

                ' Score (fixed 8 chars)
                COLOR 11, 0
                Dim scoreStr As String
                Dim scorePad As Integer
                scoreStr = Str$(Me.Scores(i))
                scorePad = 8 - Len(scoreStr)
                For p = 1 To scorePad
                    Print " ";
                Next p
                Print scoreStr;

                ' Level (fixed width)
                COLOR 10, 0
                Print " L"; Me.Levels(i);
            Else
                ' Empty slot
                COLOR 8, 0  ' Dark gray
                If i < 9 Then
                    Print " "; (i + 1); ". ";
                Else
                    Print (i + 1); ". ";
                End If
                Print "---     ";  ' Name field (8 chars)
                Print "   ---  ";  ' Score field (8 chars)
                Print " L-";       ' Level field
            End If

            LOCATE startRow + 3 + i, 43
            COLOR 14, 0
            Print "║"
        Next i

        LOCATE startRow + 13, 1
        Print "     ╚════════════════════════════════════╝"

        COLOR 7, 0  ' Reset
    End Sub
End Class
