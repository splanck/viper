' ============================================================================
' MODULE: player.bas
' PURPOSE: The player avatar (a wall-clamped mover) and the single-bullet
'          shooting model. Also owns the HUD (score + lives display).
'
' WHERE-THIS-FITS: Third in the load order (after field.bas and
'          creature.bas). Uses FIELD_WIDTH, FIELD_HEIGHT, FIELD_LEFT,
'          FIELD_TOP, and PLAYER_ZONE from field.bas. Consumed directly by
'          centipede.bas, which ticks the player and checks bullet
'          collisions every frame.
'
' KEY-DESIGN-CHOICES:
'   * SINGLE-BULLET MODEL. Only one bullet can be in flight at a time.
'     `Fire()` no-ops if `BulletActive` is already 1. This matches the
'     original arcade Centipede and forces the player to aim carefully
'     rather than spamming. A multi-bullet variant would be a list or
'     parallel-array of bullets; the single-bullet model is simpler and
'     enforces the arcade rhythm.
'   * PLAYER ZONE CLAMPING. Vertical movement is restricted so the player
'     cannot leave the bottom PLAYER_ZONE rows. This is the mirror of
'     `GenerateMushrooms` refusing to spawn in the zone — together they
'     guarantee the player has a predictable dead zone to manoeuvre in.
'     Horizontal movement is clamped to the full field width.
'   * FIELD vs. SCREEN COORDS. The player stores `X`, `Y` in field coords
'     (0-based, relative to the playfield). `Draw` converts to screen
'     coords by adding FIELD_LEFT / FIELD_TOP. Keeping game logic in
'     field coords means collision tests against centipede/spider don't
'     need a translation step.
'   * "TRY, THEN UNDO IF ILLEGAL" NOT USED. Because `MoveLeft/Right/Up/Down`
'     check bounds before mutating, we don't need the rotate-3-more-times
'     pattern seen in vtris. The player never has to undo a move.
'
' HOW-TO-READ: Start at `Sub New` / `Reset` for the starting state, then
'   the four `Move*` subs (trivial clamping). `Fire` and `UpdateBullet`
'   are the bullet state machine. Drawing at the bottom.
' ============================================================================

Class Player
    Dim X As Integer        ' Field column.
    Dim Y As Integer        ' Field row.
    Dim Lives As Integer
    Dim Score As Integer

    ' Bullet state. A single in-flight bullet is represented by three
    ' fields: its coords + an "active" flag. Inactive bullets have
    ' undefined coords — the flag is the source of truth.
    Dim BulletX As Integer
    Dim BulletY As Integer
    Dim BulletActive As Integer

    ' Construct a fresh player at the bottom centre of the field with
    ' three lives and no bullet in flight.
    Sub New()
        Me.X = FIELD_WIDTH / 2
        Me.Y = FIELD_HEIGHT - 2
        Me.Lives = 3
        Me.Score = 0
        Me.BulletActive = 0
    End Sub

    ' Respawn after death: position and bullet state back to defaults.
    ' Score and lives are preserved — the caller (LoseLife) has already
    ' decremented lives if needed.
    Sub Reset()
        Me.X = FIELD_WIDTH / 2
        Me.Y = FIELD_HEIGHT - 2
        Me.BulletActive = 0
    End Sub

    ' Step one cell left, clamped at column 0. No interaction with
    ' mushrooms — the player can stand on mushrooms; only the centipede
    ' treats them as blocking.
    Sub MoveLeft()
        If Me.X > 0 Then
            Me.X = Me.X - 1
        End If
    End Sub

    ' Step one cell right, clamped at the right wall.
    Sub MoveRight()
        If Me.X < FIELD_WIDTH - 1 Then
            Me.X = Me.X + 1
        End If
    End Sub

    ' Step one cell up, clamped at the top of the player zone. This is
    ' what keeps the player penned into the bottom PLAYER_ZONE rows. Note
    ' the asymmetric bound: `>` not `>=`, because the boundary row IS
    ' inside the zone.
    Sub MoveUp()
        If Me.Y > FIELD_HEIGHT - PLAYER_ZONE Then
            Me.Y = Me.Y - 1
        End If
    End Sub

    ' Step one cell down, clamped at the bottom of the field.
    Sub MoveDown()
        If Me.Y < FIELD_HEIGHT - 1 Then
            Me.Y = Me.Y + 1
        End If
    End Sub

    ' Fire a bullet upward from the player's current position. The bullet
    ' starts one row above the player (so it doesn't hit the player's own
    ' cell on the first tick). No-op if a bullet is already in flight —
    ' this is the single-bullet enforcement point.
    Sub Fire()
        If Me.BulletActive = 0 Then
            Me.BulletX = Me.X
            Me.BulletY = Me.Y - 1
            Me.BulletActive = 1
        End If
    End Sub

    ' Advance the bullet one row up. Deactivates and returns 1 if the
    ' bullet flew off the top of the field. The main loop uses the return
    ' value to distinguish "bullet expired naturally" from "bullet is
    ' still in flight" without a separate query.
    Function UpdateBullet() As Integer
        UpdateBullet = 0
        If Me.BulletActive = 1 Then
            Me.BulletY = Me.BulletY - 1
            If Me.BulletY < 0 Then
                Me.BulletActive = 0
                UpdateBullet = 1
            End If
        End If
    End Function

    ' Kill the bullet immediately. Called when the bullet hit something
    ' (mushroom, centipede segment, spider).
    Sub DeactivateBullet()
        Me.BulletActive = 0
    End Sub

    ' Add points to the running score. Simple accumulator — no high-score
    ' logic here; that lives in scoreboard.bas and is consulted at
    ' game-over.
    Sub AddScore(points As Integer)
        Me.Score = Me.Score + points
    End Sub

    ' Decrement lives. If the player ran out, returns 1 (game over);
    ' otherwise resets position and returns 0. The caller transitions to
    ' the game-over screen when this returns 1.
    Function LoseLife() As Integer
        Me.Lives = Me.Lives - 1
        If Me.Lives <= 0 Then
            LoseLife = 1
        Else
            LoseLife = 0
            Me.Reset()
        End If
    End Function

    ' Render the player ship as a bright-white "A" glyph. Converts field
    ' coords to screen coords before positioning.
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer

        screenX = FIELD_LEFT + Me.X
        screenY = FIELD_TOP + Me.Y

        Viper.Terminal.SetPosition(screenY, screenX)
        Viper.Terminal.SetColor(15, 0)  ' Bright white.
        PRINT "A"
    End Sub

    ' Wipe the player from the screen at its current position, restoring
    ' any mushroom that was there. Called before a move so the redraw
    ' doesn't leave a ghost trail.
    Sub Clear(field As GameField)
        field.ClearPosition(Me.X, Me.Y)
    End Sub

    ' Render the bullet as a yellow "|" at its current position. No-op if
    ' the bullet is inactive.
    Sub DrawBullet()
        Dim screenX As Integer
        Dim screenY As Integer

        If Me.BulletActive = 1 Then
            screenX = FIELD_LEFT + Me.BulletX
            screenY = FIELD_TOP + Me.BulletY

            Viper.Terminal.SetPosition(screenY, screenX)
            Viper.Terminal.SetColor(14, 0)  ' Yellow.
            PRINT "|"
        End If
    End Sub

    ' Erase the bullet from its current cell. Called before advancing the
    ' bullet so we don't leave a dotted trail behind it.
    Sub ClearBullet(field As GameField)
        If Me.BulletActive = 1 Then
            field.ClearPosition(Me.BulletX, Me.BulletY)
        End If
    End Sub

    ' Redraw the top-of-screen HUD. Two groups:
    '   * SCORE: label + number.
    '   * LIVES: label + one "A" glyph per life.
    ' The trailing "   " clears any "A" glyphs left over from a previous
    ' higher life count (e.g., dropping from 3 -> 2 needs to erase the
    ' third A). This is the same "print trailing spaces to clear stale
    ' characters" idiom used in vtris's score panel.
    Sub DrawHUD()
        Viper.Terminal.SetPosition(1, FIELD_LEFT)
        Viper.Terminal.SetColor(11, 0)
        PRINT "SCORE: "
        Viper.Terminal.SetColor(15, 0)
        PRINT Me.Score

        Viper.Terminal.SetPosition(1, FIELD_LEFT + 20)
        Viper.Terminal.SetColor(12, 0)
        PRINT "LIVES: "
        Viper.Terminal.SetColor(15, 0)
        Dim i As Integer
        For i = 1 To Me.Lives
            PRINT "A "
        Next i
        PRINT "   "  ' Clears any leftover life glyphs.
    End Sub
End Class
