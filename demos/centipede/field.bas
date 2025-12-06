' ═══════════════════════════════════════════════════════════
' FIELD.BAS - Game field and mushroom management
' ═══════════════════════════════════════════════════════════

' Game field dimensions (used as globals, not Const due to parser limitations)
Dim FIELD_WIDTH As Integer
Dim FIELD_HEIGHT As Integer
Dim FIELD_TOP As Integer
Dim FIELD_LEFT As Integer
Dim PLAYER_ZONE As Integer

' Helper function: random integer from 0 to max-1
Function RandInt(max As Integer) As Integer
    RandInt = Int(Rnd() * max)
End Function

Sub InitFieldConstants()
    FIELD_WIDTH = 40
    FIELD_HEIGHT = 24
    FIELD_TOP = 2
    FIELD_LEFT = 20
    PLAYER_ZONE = 4
End Sub

' Mushroom states: 0=empty, 1-4=health (4=full, 1=almost destroyed)
Class GameField
    Dim Mushrooms(960) As Integer  ' 40 * 24
    Dim MushroomCount As Integer

    Sub New()
        Dim i As Integer
        For i = 0 To (FIELD_WIDTH * FIELD_HEIGHT) - 1
            Me.Mushrooms(i) = 0
        Next i
        Me.MushroomCount = 0
    End Sub

    ' Get mushroom health at position (0 = empty)
    Function GetMushroom(x As Integer, y As Integer) As Integer
        If x < 0 Or x >= FIELD_WIDTH Or y < 0 Or y >= FIELD_HEIGHT Then
            GetMushroom = 0
        Else
            GetMushroom = Me.Mushrooms(y * FIELD_WIDTH + x)
        End If
    End Function

    ' Set mushroom health at position
    Sub SetMushroom(x As Integer, y As Integer, health As Integer)
        If x >= 0 And x < FIELD_WIDTH And y >= 0 And y < FIELD_HEIGHT Then
            Dim oldHealth As Integer
            oldHealth = Me.Mushrooms(y * FIELD_WIDTH + x)
            Me.Mushrooms(y * FIELD_WIDTH + x) = health

            ' Update count
            If oldHealth = 0 And health > 0 Then
                Me.MushroomCount = Me.MushroomCount + 1
            ElseIf oldHealth > 0 And health = 0 Then
                Me.MushroomCount = Me.MushroomCount - 1
            End If
        End If
    End Sub

    ' Damage mushroom, returns True if destroyed
    Function DamageMushroom(x As Integer, y As Integer) As Integer
        Dim health As Integer
        health = Me.GetMushroom(x, y)
        If health > 0 Then
            health = health - 1
            Me.SetMushroom(x, y, health)
            If health = 0 Then
                DamageMushroom = 1  ' Destroyed
            Else
                DamageMushroom = 0  ' Damaged but not destroyed
            End If
        Else
            DamageMushroom = 0
        End If
    End Function

    ' Place random mushrooms for a level
    Sub GenerateMushrooms(level As Integer)
        Dim count As Integer
        Dim placed As Integer
        Dim x As Integer
        Dim y As Integer
        Dim attempts As Integer

        ' More mushrooms at higher levels
        count = 20 + (level * 5)
        If count > 60 Then count = 60

        placed = 0
        attempts = 0

        Do While placed < count And attempts < 500
            x = RandInt(FIELD_WIDTH)
            y = RandInt(FIELD_HEIGHT - PLAYER_ZONE - 1)

            If Me.GetMushroom(x, y) = 0 Then
                Me.SetMushroom(x, y, 4)  ' Full health
                placed = placed + 1
            End If
            attempts = attempts + 1
        Loop
    End Sub

    ' Draw a single mushroom at field position
    Sub DrawMushroom(x As Integer, y As Integer)
        Dim health As Integer
        Dim screenX As Integer
        Dim screenY As Integer

        health = Me.GetMushroom(x, y)
        screenX = FIELD_LEFT + x
        screenY = FIELD_TOP + y

        Viper.Terminal.SetPosition(screenY, screenX)

        If health = 0 Then
            Viper.Terminal.SetColor(0, 0)
            PRINT " "
        ElseIf health = 4 Then
            Viper.Terminal.SetColor(10, 0)  ' Bright green
            PRINT "@"
        ElseIf health = 3 Then
            Viper.Terminal.SetColor(2, 0)   ' Green
            PRINT "@"
        ElseIf health = 2 Then
            Viper.Terminal.SetColor(3, 0)   ' Cyan
            PRINT "o"
        Else
            Viper.Terminal.SetColor(8, 0)   ' Dark gray
            PRINT "."
        End If
    End Sub

    ' Draw the entire field
    Sub DrawField()
        Dim x As Integer
        Dim y As Integer

        ' Draw border
        Viper.Terminal.SetColor(8, 0)
        For y = FIELD_TOP - 1 To FIELD_TOP + FIELD_HEIGHT
            Viper.Terminal.SetPosition(y, FIELD_LEFT - 1)
            PRINT "|"
            Viper.Terminal.SetPosition(y, FIELD_LEFT + FIELD_WIDTH)
            PRINT "|"
        Next y

        ' Draw all mushrooms
        For y = 0 To FIELD_HEIGHT - 1
            For x = 0 To FIELD_WIDTH - 1
                Me.DrawMushroom(x, y)
            Next x
        Next y
    End Sub

    ' Clear a position on screen
    Sub ClearPosition(x As Integer, y As Integer)
        Dim screenX As Integer
        Dim screenY As Integer
        Dim health As Integer

        screenX = FIELD_LEFT + x
        screenY = FIELD_TOP + y

        ' If there's a mushroom here, redraw it; otherwise clear
        health = Me.GetMushroom(x, y)
        If health > 0 Then
            Me.DrawMushroom(x, y)
        Else
            Viper.Terminal.SetPosition(screenY, screenX)
            Viper.Terminal.SetColor(0, 0)
            PRINT " "
        End If
    End Sub
End Class
