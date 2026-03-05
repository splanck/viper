' ═══════════════════════════════════════════════════════════
' PLAYER.BAS - Player ship and bullet management
' ═══════════════════════════════════════════════════════════

' Player class - controls ship and shooting
Class Player
    Dim X As Integer
    Dim Y As Integer
    Dim Lives As Integer
    Dim Score As Integer

    ' Bullet state
    Dim BulletX As Integer
    Dim BulletY As Integer
    Dim BulletActive As Integer

    Sub New()
        Me.X = FIELD_WIDTH / 2
        Me.Y = FIELD_HEIGHT - 2
        Me.Lives = 3
        Me.Score = 0
        Me.BulletActive = 0
    End Sub

    ' Reset player position (after death)
    Sub Reset()
        Me.X = FIELD_WIDTH / 2
        Me.Y = FIELD_HEIGHT - 2
        Me.BulletActive = 0
    End Sub

    ' Move player left
    Sub MoveLeft()
        If Me.X > 0 Then
            Me.X = Me.X - 1
        End If
    End Sub

    ' Move player right
    Sub MoveRight()
        If Me.X < FIELD_WIDTH - 1 Then
            Me.X = Me.X + 1
        End If
    End Sub

    ' Move player up (limited to player zone)
    Sub MoveUp()
        If Me.Y > FIELD_HEIGHT - PLAYER_ZONE Then
            Me.Y = Me.Y - 1
        End If
    End Sub

    ' Move player down
    Sub MoveDown()
        If Me.Y < FIELD_HEIGHT - 1 Then
            Me.Y = Me.Y + 1
        End If
    End Sub

    ' Fire bullet if none active
    Sub Fire()
        If Me.BulletActive = 0 Then
            Me.BulletX = Me.X
            Me.BulletY = Me.Y - 1
            Me.BulletActive = 1
        End If
    End Sub

    ' Update bullet position, returns 1 if bullet hit top
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

    ' Deactivate bullet (hit something)
    Sub DeactivateBullet()
        Me.BulletActive = 0
    End Sub

    ' Add to score
    Sub AddScore(points As Integer)
        Me.Score = Me.Score + points
    End Sub

    ' Lose a life, returns True if game over
    Function LoseLife() As Integer
        Me.Lives = Me.Lives - 1
        If Me.Lives <= 0 Then
            LoseLife = 1
        Else
            LoseLife = 0
            Me.Reset()
        End If
    End Function

    ' Draw player ship
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer

        screenX = FIELD_LEFT + Me.X
        screenY = FIELD_TOP + Me.Y

        Viper.Terminal.SetPosition(screenY, screenX)
        Viper.Terminal.SetColor(15, 0)  ' Bright white
        PRINT "A"
    End Sub

    ' Clear player from old position
    Sub Clear(field As GameField)
        field.ClearPosition(Me.X, Me.Y)
    End Sub

    ' Draw bullet
    Sub DrawBullet()
        Dim screenX As Integer
        Dim screenY As Integer

        If Me.BulletActive = 1 Then
            screenX = FIELD_LEFT + Me.BulletX
            screenY = FIELD_TOP + Me.BulletY

            Viper.Terminal.SetPosition(screenY, screenX)
            Viper.Terminal.SetColor(14, 0)  ' Yellow
            PRINT "|"
        End If
    End Sub

    ' Clear bullet from old position
    Sub ClearBullet(field As GameField)
        If Me.BulletActive = 1 Then
            field.ClearPosition(Me.BulletX, Me.BulletY)
        End If
    End Sub

    ' Draw HUD (score, lives)
    Sub DrawHUD()
        ' Score
        Viper.Terminal.SetPosition(1, FIELD_LEFT)
        Viper.Terminal.SetColor(11, 0)
        PRINT "SCORE: "
        Viper.Terminal.SetColor(15, 0)
        PRINT Me.Score

        ' Lives
        Viper.Terminal.SetPosition(1, FIELD_LEFT + 20)
        Viper.Terminal.SetColor(12, 0)
        PRINT "LIVES: "
        Viper.Terminal.SetColor(15, 0)
        Dim i As Integer
        For i = 1 To Me.Lives
            PRINT "A "
        Next i
        PRINT "   "  ' Clear extra lives display
    End Sub
End Class
