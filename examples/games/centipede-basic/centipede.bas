' ============================================================================
' MODULE: centipede.bas
' PURPOSE: Program entry point and main game loop for the Centipede demo.
'          Owns the global game state, the menu state machine, and the
'          per-frame orchestration of player + centipede + spider updates.
'
' WHERE-THIS-FITS: Top of the dependency tree. Loads field.bas (must be
'          first — defines FIELD_WIDTH etc.), player.bas, creature.bas,
'          scoreboard.bas via AddFile and wires them together.
'
' KEY-DESIGN-CHOICES:
'   * DIRTY-RECTANGLE RENDERING. Rather than clearing the whole field and
'     repainting every frame, each actor (player, bullet, centipede,
'     spider) erases its OWN previous cell before moving. `ClearPosition`
'     redraws any mushroom that was underneath, so actors and terrain
'     interleave correctly. This keeps the frame paint cost proportional
'     to actors-in-motion rather than to field area.
'   * NON-BLOCKING INPUT. `Zanna.Terminal.PollKey()` returns "" when no key
'     is queued, so the loop ticks even when the player is idle — the
'     centipede still advances. This is the canonical "real-time action
'     game" input pattern. Contrast with the menu code below which uses
'     the blocking `GetKey` because it WANTS to wait.
'   * ORDER OF OPERATIONS IN THE LOOP. Input -> bullet update -> centipede
'     move -> player/centipede collision -> spider logic -> level-complete
'     check -> redraw player -> 30 ms sleep. Bullet-then-centipede order
'     lets a bullet fired on this tick hit a segment that would have
'     moved INTO that cell this tick. Player-collision after centipede-
'     move ensures you die if the centipede stepped onto you, not just if
'     you stepped onto the centipede.
'   * LIFE LOSS EQUIVALENCE. Three situations subtract a life: centipede
'     reaching the bottom, centipede stepping on you, spider touching
'     you. All three paths call `ThePlayer.LoseLife` and either reset the
'     level entities or show the game-over screen.
'   * LEVEL PROGRESSION. Killing every segment triggers `ShowLevelComplete`
'     + `InitLevel(level + 1)`. Each new level plants more mushrooms and
'     spawns a faster, longer centipede. Mushrooms from the previous
'     level are wiped because `InitLevel` replaces `TheField` entirely —
'     this is intentional: a persistent board across levels would be
'     unfair given the quadratic mushroom count.
'
' HOW-TO-READ: Skip the menu/instructions Subs (pure rendering) and read
'   in this order:
'     1. `Sub InitLevel` — sets up the per-level entities.
'     2. `Sub DrawGameScreen` — one-time per-level paint.
'     3. `Sub PlayGame` — the main loop in full. Top to bottom is a full
'        frame's worth of work.
'     4. The main program at the bottom — menu state machine.
' ============================================================================

' Include game modules. ORDER MATTERS: field.bas declares the dimension
' globals that creature.bas and player.bas reference in their array sizes
' and clamping logic. The InitFieldConstants() call at the bottom of this
' file must run before any GameField/Player/Centipede is constructed.
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
' Renders the title card, centipede ASCII art, and four menu options.
' Pure rendering — does not consume input. The main program below reads
' the keypress after this returns.
Sub ShowMainMenu()
    Zanna.Terminal.Clear()

    ' Title in bright green
    Zanna.Terminal.SetColor(10, 0)
    Zanna.Terminal.SetPosition(3, 15)
    PRINT "+=================================+"
    Zanna.Terminal.SetPosition(4, 15)
    PRINT "|                                 |"
    Zanna.Terminal.SetPosition(5, 15)
    PRINT "|";
    Zanna.Terminal.SetColor(11, 0)
    PRINT "       C E N T I P E D E       ";
    Zanna.Terminal.SetColor(10, 0)
    PRINT "|"
    Zanna.Terminal.SetPosition(6, 15)
    PRINT "|                                 |"
    Zanna.Terminal.SetPosition(7, 15)
    PRINT "+=================================+"

    ' Centipede ASCII art
    Zanna.Terminal.SetColor(14, 0)
    Zanna.Terminal.SetPosition(9, 22)
    PRINT "  <@@@@@@@@@@>"
    Zanna.Terminal.SetColor(6, 0)
    Zanna.Terminal.SetPosition(10, 22)
    PRINT "  /|||||||||||\\"

    ' Menu options
    Zanna.Terminal.SetPosition(13, 22)
    Zanna.Terminal.SetColor(15, 0)
    PRINT "[1] ";
    Zanna.Terminal.SetColor(10, 0)
    PRINT "NEW GAME"

    Zanna.Terminal.SetPosition(15, 22)
    Zanna.Terminal.SetColor(15, 0)
    PRINT "[2] ";
    Zanna.Terminal.SetColor(11, 0)
    PRINT "INSTRUCTIONS"

    Zanna.Terminal.SetPosition(17, 22)
    Zanna.Terminal.SetColor(15, 0)
    PRINT "[3] ";
    Zanna.Terminal.SetColor(14, 0)
    PRINT "HIGH SCORES"

    Zanna.Terminal.SetPosition(19, 22)
    Zanna.Terminal.SetColor(15, 0)
    PRINT "[Q] ";
    Zanna.Terminal.SetColor(12, 0)
    PRINT "QUIT"

    ' Footer
    Zanna.Terminal.SetColor(8, 0)
    Zanna.Terminal.SetPosition(22, 17)
    PRINT "      Zanna BASIC Demo 2024      "

    Zanna.Terminal.SetColor(7, 0)
End Sub

' === SHOW INSTRUCTIONS ===
' Static help screen: controls, objectives, scoring. Pure render — caller
' waits for a keypress via `WaitForKey` afterwards.
Sub ShowInstructions()
    Zanna.Terminal.Clear()

    ' Title
    Zanna.Terminal.SetColor(11, 0)
    Zanna.Terminal.SetPosition(2, 20)
    PRINT "=== INSTRUCTIONS ==="

    ' Controls section
    Zanna.Terminal.SetColor(14, 0)
    Zanna.Terminal.SetPosition(5, 10)
    PRINT "CONTROLS:"
    Zanna.Terminal.SetColor(7, 0)
    Zanna.Terminal.SetPosition(7, 12)
    PRINT "W / Up Arrow    - Move up"
    Zanna.Terminal.SetPosition(8, 12)
    PRINT "S / Down Arrow  - Move down"
    Zanna.Terminal.SetPosition(9, 12)
    PRINT "A / Left Arrow  - Move left"
    Zanna.Terminal.SetPosition(10, 12)
    PRINT "D / Right Arrow - Move right"
    Zanna.Terminal.SetPosition(11, 12)
    PRINT "SPACE           - Fire bullet"
    Zanna.Terminal.SetPosition(12, 12)
    PRINT "Q               - Quit to menu"

    ' Objectives section
    Zanna.Terminal.SetColor(14, 0)
    Zanna.Terminal.SetPosition(15, 10)
    PRINT "OBJECTIVE:"
    Zanna.Terminal.SetColor(7, 0)
    Zanna.Terminal.SetPosition(17, 12)
    PRINT "Destroy the centipede before it reaches you!"
    Zanna.Terminal.SetPosition(18, 12)
    PRINT "Shoot mushrooms to clear a path."
    Zanna.Terminal.SetPosition(19, 12)
    PRINT "Watch out for the spider!"

    ' Scoring section
    Zanna.Terminal.SetColor(14, 0)
    Zanna.Terminal.SetPosition(22, 10)
    PRINT "SCORING:"
    Zanna.Terminal.SetColor(10, 0)
    Zanna.Terminal.SetPosition(24, 12)
    PRINT "Centipede Segment: 10 pts"
    Zanna.Terminal.SetPosition(25, 12)
    PRINT "Mushroom:           1 pt"
    Zanna.Terminal.SetPosition(26, 12)
    PRINT "Spider:            50 pts"

    ' Press any key prompt
    Zanna.Terminal.SetColor(12, 0)
    Zanna.Terminal.SetPosition(29, 15)
    PRINT "Press any key to return to menu..."

    Zanna.Terminal.SetColor(7, 0)
End Sub

' === SHOW HIGH SCORES ===
' Renders the top-10 panel via `Scores.Draw(3)` and a "press any key"
' prompt. The scoreboard data was loaded by `Scores = New ScoreBoard` in
' the main program at the bottom of this file.
Sub ShowHighScores()
    Zanna.Terminal.Clear()
    Scores.Draw(3)

    ' Press any key prompt
    Zanna.Terminal.SetColor(12, 0)
    Zanna.Terminal.SetPosition(22, 15)
    PRINT "Press any key to return to menu..."
    Zanna.Terminal.SetColor(7, 0)
End Sub

' === WAIT FOR KEY ===
' Blocking keypress wait. Used by menu screens that want to pause until
' the user acknowledges. Distinct from `Zanna.Terminal.PollKey()` used in
' the main loop, which is non-blocking.
Sub WaitForKey()
    Dim k As String
    k = Zanna.Terminal.ReadKey()
End Sub

' === GAME INPUT ===
' Terminal arrow keys arrive as ANSI escape sequences: ESC [ A/B/C/D.
' The raw InKey byte reader would otherwise surface the final A or D as
' regular WASD input, making Up act like A and Left act like D. Normalize
' those sequences into stable direction names before the game loop dispatches.
Function ReadGameKey() As String
    Dim k As String
    Dim prefix As String
    Dim code As String

    k = Zanna.Terminal.PollKey()
    If k <> CHR(27) Then Return k

    prefix = Zanna.Terminal.ReadKeyFor(5)
    If prefix <> "[" And prefix <> "O" Then Return k

    code = Zanna.Terminal.ReadKeyFor(5)
    If code = "A" Then Return "UP"
    If code = "B" Then Return "DOWN"
    If code = "C" Then Return "RIGHT"
    If code = "D" Then Return "LEFT"

    Return ""
End Function

' === INITIALIZE NEW LEVEL ===
' Rebuild all per-level state: fresh field (with level-scaled mushroom
' density), fresh centipede (longer and faster), fresh spider (inactive —
' its spawn timer fires inside PlayGame). `CurrentLevel` is stored as a
' module global so `DrawGameScreen` and other helpers can reference it.
'
' Note that the PLAYER is NOT reset here. `PlayGame` creates ThePlayer
' once at start; across levels the same player carries their score and
' remaining lives.
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
' One-time per-level full paint: clear the terminal, draw the HUD, draw
' the field (borders + mushrooms), and stamp the level indicator in the
' corner. The main loop after this only does dirty updates.
Sub DrawGameScreen()
    Zanna.Terminal.Clear()
    ThePlayer.DrawHUD()
    TheField.DrawField()

    ' Level indicator
    Zanna.Terminal.SetPosition(1, FIELD_LEFT + 35)
    Zanna.Terminal.SetColor(14, 0)
    PRINT "LVL:"
    Zanna.Terminal.SetColor(15, 0)
    PRINT CurrentLevel
End Sub

' === GAME OVER SCREEN ===
' Shows the final score, tests for a high-score qualification, and (if
' qualified) inserts the result under the name "YOU". Blocks on
' `WaitForKey` before returning to the menu. Player-name input is
' hardcoded for simplicity — a production version would prompt for a
' 3-letter arcade name.
Sub ShowGameOver()
    Zanna.Terminal.SetPosition(12, FIELD_LEFT + 10)
    Zanna.Terminal.SetColor(12, 0)
    PRINT "*** GAME OVER ***"

    Zanna.Terminal.SetPosition(14, FIELD_LEFT + 8)
    Zanna.Terminal.SetColor(11, 0)
    PRINT "Final Score: "
    Zanna.Terminal.SetColor(15, 0)
    PRINT ThePlayer.Score

    ' Check for high score
    If Scores.IsHighScore(ThePlayer.Score) = 1 Then
        Zanna.Terminal.SetPosition(16, FIELD_LEFT + 5)
        Zanna.Terminal.SetColor(14, 0)
        PRINT "NEW HIGH SCORE!"
        Scores.AddScore("YOU", ThePlayer.Score)
    End If

    Zanna.Terminal.SetPosition(18, FIELD_LEFT + 5)
    Zanna.Terminal.SetColor(8, 0)
    PRINT "Press any key..."
    WaitForKey()
End Sub

' === LEVEL COMPLETE SCREEN ===
' Brief "LEVEL COMPLETE" flash with current score. Uses a fixed 1.5 s
' delay rather than waiting for input — the player shouldn't have to
' confirm; the game just advances.
Sub ShowLevelComplete()
    Zanna.Terminal.SetPosition(12, FIELD_LEFT + 8)
    Zanna.Terminal.SetColor(10, 0)
    PRINT "*** LEVEL COMPLETE ***"

    Zanna.Terminal.SetPosition(14, FIELD_LEFT + 10)
    Zanna.Terminal.SetColor(11, 0)
    PRINT "Score: "
    Zanna.Terminal.SetColor(15, 0)
    PRINT ThePlayer.Score

    Zanna.Time.Clock.Sleep(1500)
End Sub

' === MAIN GAME LOOP ===
' The per-tick state machine. One iteration does:
'   1. Read an input (non-blocking).
'   2. Handle movement / fire / quit.
'   3. Update + collide the bullet: first against mushrooms, then the
'      centipede, then the spider. Earliest hit wins; once the bullet
'      deactivates, later checks are skipped.
'   4. Clear + move + redraw the centipede. If it reached the bottom,
'      lose a life (or game-over).
'   5. Check player-centipede collision.
'   6. Tick the spider: spawn if its timer expired, else move + check
'      player collision.
'   7. Check for level complete (no active segments left).
'   8. Redraw the player.
'   9. Sleep 30 ms — caps at ~33 Hz.
'
' ALLOCATION DISCIPLINE: The player is constructed ONCE here at function
' entry. `InitLevel` is called for level 1 before the loop starts, then
' again after each "level complete" branch. No allocations in the hot
' loop; all mutation happens in-place on the module-scope globals.
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
        key = ReadGameKey()

        ' Handle input
        oldX = ThePlayer.X
        oldY = ThePlayer.Y

        If key = "w" Or key = "W" Or key = "UP" Then
            ThePlayer.MoveUp()
        ElseIf key = "s" Or key = "S" Or key = "DOWN" Then
            ThePlayer.MoveDown()
        ElseIf key = "a" Or key = "A" Or key = "LEFT" Then
            ThePlayer.MoveLeft()
        ElseIf key = "d" Or key = "D" Or key = "RIGHT" Then
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
        Zanna.Time.Clock.Sleep(30)
    Loop
End Sub

' ============================================================================
' MAIN PROGRAM
' ----------------------------------------------------------------------------
' Bootstrap sequence:
'   1. Seed the dimension globals in field.bas / creature.bas / scoreboard.bas.
'      These MUST run before any class is constructed because the array
'      sizes and clamping logic reference them.
'   2. Construct the persistent `Scores` scoreboard.
'   3. Seed the RNG once.
'   4. Enter the menu state machine:
'        [1] -> PlayGame        (new game)
'        [2] -> ShowInstructions (help)
'        [3] -> ShowHighScores   (leaderboard)
'        [Q] -> quit
'      Unknown keys silently re-show the menu.
' ============================================================================

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
    choice = Zanna.Terminal.ReadKey()

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

Zanna.Terminal.Clear()
Zanna.Terminal.SetPosition(1, 1)
PRINT "Thanks for playing CENTIPEDE!"
