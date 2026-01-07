' ╔══════════════════════════════════════════════════════════╗
' ║  CENTIPEDE - Classic Arcade Game for Viper BASIC        ║
' ║  Features: ANSI Colors, Scoreboard, Multiple Levels     ║
' ╚══════════════════════════════════════════════════════════╝

' Include game modules
AddFile "field.bas"
AddFile "player.bas"
AddFile "creature.bas"
AddFile "scoreboard.bas"

' Game state
Dim TheField As GameField
Dim ThePlayer As Player
Dim TheCentipede As Centipede
Dim TheSpider As Spider
Dim Scores As ScoreBoard
Dim CurrentLevel As Integer
Dim GameRunning As Integer
Dim SpiderSpawnTimer As Integer

' === SHOW MAIN MENU ===
Sub ShowMainMenu()
    Viper.Terminal.Clear()

    ' Title in bright green
    Viper.Terminal.SetColor(10, 0)
    Viper.Terminal.SetPosition(3, 15)
    PRINT "+=================================+"
    Viper.Terminal.SetPosition(4, 15)
    PRINT "|                                 |"
    Viper.Terminal.SetPosition(5, 15)
    PRINT "|";
    Viper.Terminal.SetColor(11, 0)
    PRINT "       C E N T I P E D E       ";
    Viper.Terminal.SetColor(10, 0)
    PRINT "|"
    Viper.Terminal.SetPosition(6, 15)
    PRINT "|                                 |"
    Viper.Terminal.SetPosition(7, 15)
    PRINT "+=================================+"

    ' Centipede ASCII art
    Viper.Terminal.SetColor(14, 0)
    Viper.Terminal.SetPosition(9, 22)
    PRINT "  <@@@@@@@@@@>"
    Viper.Terminal.SetColor(6, 0)
    Viper.Terminal.SetPosition(10, 22)
    PRINT "  /|||||||||||\\"

    ' Menu options
    Viper.Terminal.SetPosition(13, 22)
    Viper.Terminal.SetColor(15, 0)
    PRINT "[1] ";
    Viper.Terminal.SetColor(10, 0)
    PRINT "NEW GAME"

    Viper.Terminal.SetPosition(15, 22)
    Viper.Terminal.SetColor(15, 0)
    PRINT "[2] ";
    Viper.Terminal.SetColor(11, 0)
    PRINT "INSTRUCTIONS"

    Viper.Terminal.SetPosition(17, 22)
    Viper.Terminal.SetColor(15, 0)
    PRINT "[3] ";
    Viper.Terminal.SetColor(14, 0)
    PRINT "HIGH SCORES"

    Viper.Terminal.SetPosition(19, 22)
    Viper.Terminal.SetColor(15, 0)
    PRINT "[Q] ";
    Viper.Terminal.SetColor(12, 0)
    PRINT "QUIT"

    ' Footer
    Viper.Terminal.SetColor(8, 0)
    Viper.Terminal.SetPosition(22, 17)
    PRINT "      Viper BASIC Demo 2024      "

    Viper.Terminal.SetColor(7, 0)
End Sub

' === SHOW INSTRUCTIONS ===
Sub ShowInstructions()
    Viper.Terminal.Clear()

    ' Title
    Viper.Terminal.SetColor(11, 0)
    Viper.Terminal.SetPosition(2, 20)
    PRINT "=== INSTRUCTIONS ==="

    ' Controls section
    Viper.Terminal.SetColor(14, 0)
    Viper.Terminal.SetPosition(5, 10)
    PRINT "CONTROLS:"
    Viper.Terminal.SetColor(7, 0)
    Viper.Terminal.SetPosition(7, 12)
    PRINT "W / Up Arrow    - Move up"
    Viper.Terminal.SetPosition(8, 12)
    PRINT "S / Down Arrow  - Move down"
    Viper.Terminal.SetPosition(9, 12)
    PRINT "A / Left Arrow  - Move left"
    Viper.Terminal.SetPosition(10, 12)
    PRINT "D / Right Arrow - Move right"
    Viper.Terminal.SetPosition(11, 12)
    PRINT "SPACE           - Fire bullet"
    Viper.Terminal.SetPosition(12, 12)
    PRINT "Q               - Quit to menu"

    ' Objectives section
    Viper.Terminal.SetColor(14, 0)
    Viper.Terminal.SetPosition(15, 10)
    PRINT "OBJECTIVE:"
    Viper.Terminal.SetColor(7, 0)
    Viper.Terminal.SetPosition(17, 12)
    PRINT "Destroy the centipede before it reaches you!"
    Viper.Terminal.SetPosition(18, 12)
    PRINT "Shoot mushrooms to clear a path."
    Viper.Terminal.SetPosition(19, 12)
    PRINT "Watch out for the spider!"

    ' Scoring section
    Viper.Terminal.SetColor(14, 0)
    Viper.Terminal.SetPosition(22, 10)
    PRINT "SCORING:"
    Viper.Terminal.SetColor(10, 0)
    Viper.Terminal.SetPosition(24, 12)
    PRINT "Centipede Segment: 10 pts"
    Viper.Terminal.SetPosition(25, 12)
    PRINT "Mushroom:           1 pt"
    Viper.Terminal.SetPosition(26, 12)
    PRINT "Spider:            50 pts"

    ' Press any key prompt
    Viper.Terminal.SetColor(12, 0)
    Viper.Terminal.SetPosition(29, 15)
    PRINT "Press any key to return to menu..."

    Viper.Terminal.SetColor(7, 0)
End Sub

' === SHOW HIGH SCORES ===
Sub ShowHighScores()
    Viper.Terminal.Clear()
    Scores.Draw(3)

    ' Press any key prompt
    Viper.Terminal.SetColor(12, 0)
    Viper.Terminal.SetPosition(22, 15)
    PRINT "Press any key to return to menu..."
    Viper.Terminal.SetColor(7, 0)
End Sub

' === WAIT FOR KEY ===
Sub WaitForKey()
    Dim k As String
    k = Viper.Terminal.GetKey()
End Sub

' === INITIALIZE NEW LEVEL ===
Sub InitLevel(level As Integer)
    TheField = New GameField
    TheField.GenerateMushrooms(level)

    TheCentipede = New Centipede
    TheCentipede.Init(level, FIELD_WIDTH / 2)

    TheSpider = New Spider
    SpiderSpawnTimer = 100

    CurrentLevel = level
End Sub

' === DRAW GAME SCREEN ===
Sub DrawGameScreen()
    Viper.Terminal.Clear()
    ThePlayer.DrawHUD()
    TheField.DrawField()

    ' Level indicator
    Viper.Terminal.SetPosition(1, FIELD_LEFT + 35)
    Viper.Terminal.SetColor(14, 0)
    PRINT "LVL:"
    Viper.Terminal.SetColor(15, 0)
    PRINT CurrentLevel
End Sub

' === GAME OVER SCREEN ===
Sub ShowGameOver()
    Viper.Terminal.SetPosition(12, FIELD_LEFT + 10)
    Viper.Terminal.SetColor(12, 0)
    PRINT "*** GAME OVER ***"

    Viper.Terminal.SetPosition(14, FIELD_LEFT + 8)
    Viper.Terminal.SetColor(11, 0)
    PRINT "Final Score: "
    Viper.Terminal.SetColor(15, 0)
    PRINT ThePlayer.Score

    ' Check for high score
    If Scores.IsHighScore(ThePlayer.Score) = 1 Then
        Viper.Terminal.SetPosition(16, FIELD_LEFT + 5)
        Viper.Terminal.SetColor(14, 0)
        PRINT "NEW HIGH SCORE!"
        Scores.AddScore("YOU", ThePlayer.Score)
    End If

    Viper.Terminal.SetPosition(18, FIELD_LEFT + 5)
    Viper.Terminal.SetColor(8, 0)
    PRINT "Press any key..."
    WaitForKey()
End Sub

' === LEVEL COMPLETE SCREEN ===
Sub ShowLevelComplete()
    Viper.Terminal.SetPosition(12, FIELD_LEFT + 8)
    Viper.Terminal.SetColor(10, 0)
    PRINT "*** LEVEL COMPLETE ***"

    Viper.Terminal.SetPosition(14, FIELD_LEFT + 10)
    Viper.Terminal.SetColor(11, 0)
    PRINT "Score: "
    Viper.Terminal.SetColor(15, 0)
    PRINT ThePlayer.Score

    Viper.Time.SleepMs(1500)
End Sub

' === MAIN GAME LOOP ===
Sub PlayGame()
    Dim key As String
    Dim oldX As Integer
    Dim oldY As Integer
    Dim oldBulletY As Integer
    Dim hitSegment As Integer
    Dim points As Integer

    ThePlayer = New Player
    InitLevel(1)
    DrawGameScreen()
    GameRunning = 1

    Do While GameRunning = 1
        ' Get input (non-blocking)
        key = Viper.Terminal.InKey()

        ' Handle input
        oldX = ThePlayer.X
        oldY = ThePlayer.Y

        If key = "w" Or key = "W" Then
            ThePlayer.MoveUp()
        ElseIf key = "s" Or key = "S" Then
            ThePlayer.MoveDown()
        ElseIf key = "a" Or key = "A" Then
            ThePlayer.MoveLeft()
        ElseIf key = "d" Or key = "D" Then
            ThePlayer.MoveRight()
        ElseIf key = " " Then
            ThePlayer.Fire()
        ElseIf key = "q" Or key = "Q" Then
            GameRunning = 0
        End If

        ' Redraw player if moved
        If oldX <> ThePlayer.X Or oldY <> ThePlayer.Y Then
            TheField.ClearPosition(oldX, oldY)
            ThePlayer.Draw()
        End If

        ' Update bullet
        If ThePlayer.BulletActive = 1 Then
            oldBulletY = ThePlayer.BulletY
            ThePlayer.ClearBullet(TheField)
            ThePlayer.UpdateBullet()

            ' Check bullet collisions
            If ThePlayer.BulletActive = 1 Then
                ' Hit mushroom?
                If TheField.GetMushroom(ThePlayer.BulletX, ThePlayer.BulletY) > 0 Then
                    If TheField.DamageMushroom(ThePlayer.BulletX, ThePlayer.BulletY) = 1 Then
                        ThePlayer.AddScore(1)
                    End If
                    TheField.DrawMushroom(ThePlayer.BulletX, ThePlayer.BulletY)
                    ThePlayer.DeactivateBullet()
                    ThePlayer.DrawHUD()
                End If

                ' Hit centipede?
                If ThePlayer.BulletActive = 1 Then
                    hitSegment = TheCentipede.SegmentAt(ThePlayer.BulletX, ThePlayer.BulletY)
                    If hitSegment >= 0 Then
                        points = TheCentipede.KillSegment(hitSegment, TheField)
                        ThePlayer.AddScore(points)
                        ThePlayer.DeactivateBullet()
                        ThePlayer.DrawHUD()
                        TheField.DrawMushroom(ThePlayer.BulletX, ThePlayer.BulletY)
                    End If
                End If

                ' Hit spider?
                If ThePlayer.BulletActive = 1 Then
                    If TheSpider.IsAt(ThePlayer.BulletX, ThePlayer.BulletY) = 1 Then
                        TheSpider.Kill()
                        ThePlayer.AddScore(SPIDER_POINTS)
                        ThePlayer.DeactivateBullet()
                        ThePlayer.DrawHUD()
                    End If
                End If

                ' Draw bullet if still active
                If ThePlayer.BulletActive = 1 Then
                    ThePlayer.DrawBullet()
                End If
            End If
        End If

        ' Move centipede
        TheCentipede.Clear(TheField)
        If TheCentipede.Move(TheField) = 1 Then
            ' Centipede reached bottom - lose life
            If ThePlayer.LoseLife() = 1 Then
                ShowGameOver()
                GameRunning = 0
            Else
                ' Reset centipede
                TheCentipede.Init(CurrentLevel, FIELD_WIDTH / 2)
                ThePlayer.DrawHUD()
            End If
        End If
        TheCentipede.Draw()

        ' Check player collision with centipede
        If TheCentipede.SegmentAt(ThePlayer.X, ThePlayer.Y) >= 0 Then
            If ThePlayer.LoseLife() = 1 Then
                ShowGameOver()
                GameRunning = 0
            Else
                TheCentipede.Init(CurrentLevel, FIELD_WIDTH / 2)
                ThePlayer.DrawHUD()
            End If
        End If

        ' Spider logic
        SpiderSpawnTimer = SpiderSpawnTimer - 1
        If TheSpider.Active = 0 And SpiderSpawnTimer <= 0 Then
            TheSpider.Spawn()
            SpiderSpawnTimer = 150 + RandInt(100)
        End If

        If TheSpider.Active = 1 Then
            TheSpider.Clear(TheField)
            TheSpider.Move(TheField)

            ' Check spider collision with player
            If TheSpider.IsAt(ThePlayer.X, ThePlayer.Y) = 1 Then
                If ThePlayer.LoseLife() = 1 Then
                    ShowGameOver()
                    GameRunning = 0
                Else
                    TheSpider.Kill()
                    ThePlayer.DrawHUD()
                End If
            End If

            TheSpider.Draw()
        End If

        ' Check for level complete
        If TheCentipede.ActiveCount() = 0 Then
            ShowLevelComplete()
            CurrentLevel = CurrentLevel + 1
            InitLevel(CurrentLevel)
            DrawGameScreen()
        End If

        ' Always draw player
        ThePlayer.Draw()

        ' Frame delay
        Viper.Time.SleepMs(30)
    Loop
End Sub

' === MAIN PROGRAM ===
Dim running As Integer
Dim choice As String

' Initialize all constants
InitFieldConstants()
InitCreatureConstants()
InitScoreboardConstants()

Scores = New ScoreBoard
Randomize
running = 1

Do While running = 1
    ShowMainMenu()
    choice = Viper.Terminal.GetKey()

    If choice = "1" Then
        PlayGame()
    ElseIf choice = "2" Then
        ShowInstructions()
        WaitForKey()
    ElseIf choice = "3" Then
        ShowHighScores()
        WaitForKey()
    ElseIf choice = "q" Or choice = "Q" Then
        running = 0
    End If
Loop

Viper.Terminal.Clear()
Viper.Terminal.SetPosition(1, 1)
PRINT "Thanks for playing CENTIPEDE!"
