' ============================================================================
' MODULE: field.bas
' PURPOSE: Models the playfield — a 40x24 grid of mushrooms — plus the global
'          dimensions every other module consults to know where the world
'          begins and ends.
'
' WHERE-THIS-FITS: This is the foundation module. `centipede.bas` loads it
'          first so the `FIELD_WIDTH` / `FIELD_HEIGHT` / `FIELD_TOP` /
'          `FIELD_LEFT` / `PLAYER_ZONE` globals are populated before
'          `creature.bas` or `player.bas` reference them. The dimensions
'          are stored as globals (not Const) because this BASIC parser
'          does not accept Const inside array-size declarations.
'
' KEY-DESIGN-CHOICES:
'   * ROW-MAJOR FLAT ARRAY. `Mushrooms(960)` holds 40 * 24 = 960 cells
'     indexed as `y * FIELD_WIDTH + x`. Flat arrays are faster than 2D
'     Dims in this BASIC runtime (single multiply vs. bounds-checked
'     two-axis indexing) and they fit naturally into the single array
'     type BASIC provides.
'   * MUSHROOM HEALTH LADDER. A mushroom is 0 (empty) or 1..4 (damaged ->
'     full). Each bullet hit decrements by one. The render routine
'     (`DrawMushroom`) picks a colour by health so the player sees the
'     mushroom fading as it takes damage — bright green full, gray near
'     death. This is the classic Centipede "mushrooms are destructible
'     terrain" mechanic.
'   * MUSHROOM COUNT TRACKED. `MushroomCount` mirrors the number of
'     non-zero cells so other systems can query "how many mushrooms are
'     left?" in O(1). Only `SetMushroom` mutates it, keeping the
'     invariant with a before/after health comparison.
'   * PLAYER ZONE IS BOTTOM-ANCHORED. `PLAYER_ZONE = 4` reserves the
'     bottom four rows for the player and prevents mushrooms from
'     spawning there (`GenerateMushrooms` uses `FIELD_HEIGHT - PLAYER_ZONE
'     - 1` as its upper bound). This guarantees the player always has a
'     clear corridor at the start of a level.
'   * SCREEN ORIGIN OFFSET. `FIELD_LEFT = 20` / `FIELD_TOP = 2` place the
'     playfield away from the terminal corner so the HUD and score panel
'     have room. Every draw call that translates field coords to screen
'     coords adds these offsets.
'
' HOW-TO-READ: Start at `InitFieldConstants` to see the dimensions. Then
'   `GetMushroom` / `SetMushroom` for the flat-array indexing trick. Then
'   `DamageMushroom` for the destructible-terrain semantics. Rendering
'   helpers are at the bottom.
' ============================================================================

' Game field dimensions (used as globals, not Const due to parser limitations
' around Const inside array-size declarations).
Dim FIELD_WIDTH As Integer
Dim FIELD_HEIGHT As Integer
Dim FIELD_TOP As Integer
Dim FIELD_LEFT As Integer
Dim PLAYER_ZONE As Integer

' Random integer from 0 (inclusive) to max (exclusive). Thin wrapper over
' `Rnd()` + `Int()` that reads more like conventional random-int APIs at
' the call sites. Used by `GenerateMushrooms`, spider spawn logic, and the
' erratic-movement code in creature.bas.
Function RandInt(max As Integer) As Integer
    RandInt = Int(Rnd() * max)
End Function

' Seed the five field-dimension globals. Called exactly once from the main
' program before any GameField is constructed. Kept separate from `Dim`s
' so the numbers live next to each other and are easy to tune.
Sub InitFieldConstants()
    FIELD_WIDTH = 40
    FIELD_HEIGHT = 24
    FIELD_TOP = 2
    FIELD_LEFT = 20
    PLAYER_ZONE = 4
End Sub

' ===========================================================================
' Class GameField
'   Owns the mushroom grid and the rendering logic for both mushrooms and
'   the surrounding border.
'
'   Mushroom cell values:
'     0 = empty (no mushroom)
'     4 = full health (freshly placed or regrown)
'     3 = one hit taken (still blocking, different colour)
'     2 = two hits taken
'     1 = almost dead (next hit destroys)
'   A bullet or spider pass-through damages the cell by 1.
' ===========================================================================
Class GameField
    ' Flat row-major cell storage. Cell (x, y) lives at index
    ' `y * FIELD_WIDTH + x`. Size 960 = 40 * 24.
    Dim Mushrooms(960) As Integer

    ' Running count of non-zero cells. Invariant: equals the number of
    ' mushrooms currently on the field. Updated only by `SetMushroom`.
    Dim MushroomCount As Integer

    ' Construct an empty field. Zeroes every cell and resets the count.
    ' Called at the start of each level.
    Sub New()
        Dim i As Integer
        For i = 0 To (FIELD_WIDTH * FIELD_HEIGHT) - 1
            Me.Mushrooms(i) = 0
        Next i
        Me.MushroomCount = 0
    End Sub

    ' Read mushroom health at (x, y), or 0 for out-of-bounds queries.
    ' Returning 0 for out-of-bounds is deliberate: it lets collision tests
    ' treat "outside the field" as "empty" without a separate bounds check
    ' on the caller side. (The caller still checks field boundaries for
    ' movement legality — this is just to make GetMushroom safe to call
    ' speculatively.)
    Function GetMushroom(x As Integer, y As Integer) As Integer
        If x < 0 Or x >= FIELD_WIDTH Or y < 0 Or y >= FIELD_HEIGHT Then
            GetMushroom = 0
        Else
            GetMushroom = Me.Mushrooms(y * FIELD_WIDTH + x)
        End If
    End Function

    ' Write mushroom health at (x, y). Also maintains `MushroomCount` by
    ' comparing old vs new state: a 0 -> non-zero transition bumps the
    ' count, a non-zero -> 0 transition decrements it. Out-of-bounds
    ' writes are silently ignored so the function is safe to call with
    ' unchecked coordinates.
    Sub SetMushroom(x As Integer, y As Integer, health As Integer)
        If x >= 0 And x < FIELD_WIDTH And y >= 0 And y < FIELD_HEIGHT Then
            Dim oldHealth As Integer
            oldHealth = Me.Mushrooms(y * FIELD_WIDTH + x)
            Me.Mushrooms(y * FIELD_WIDTH + x) = health

            ' Maintain the count invariant.
            If oldHealth = 0 And health > 0 Then
                Me.MushroomCount = Me.MushroomCount + 1
            ElseIf oldHealth > 0 And health = 0 Then
                Me.MushroomCount = Me.MushroomCount - 1
            End If
        End If
    End Sub

    ' Damage the mushroom at (x, y) by one point. Returns 1 if the hit
    ' destroyed the mushroom (health went from 1 to 0), else 0. No-op on
    ' empty cells. The caller uses the return to decide whether to award
    ' the "destroyed mushroom" point bonus.
    Function DamageMushroom(x As Integer, y As Integer) As Integer
        Dim health As Integer
        health = Me.GetMushroom(x, y)
        If health > 0 Then
            health = health - 1
            Me.SetMushroom(x, y, health)
            If health = 0 Then
                DamageMushroom = 1  ' Destroyed.
            Else
                DamageMushroom = 0  ' Damaged but still blocking.
            End If
        Else
            DamageMushroom = 0
        End If
    End Function

    ' Scatter random mushrooms for a new level. The count scales with the
    ' level (20 + level*5, capped at 60) so each successive level starts
    ' with a more cluttered board. Mushrooms are never placed in the
    ' player zone (bottom PLAYER_ZONE + 1 rows) so the player has a clear
    ' landing pad at start. The placement loop retries on collision and
    ' caps total attempts at 500 to avoid an infinite loop on a dense
    ' board.
    Sub GenerateMushrooms(level As Integer)
        Dim count As Integer
        Dim placed As Integer
        Dim x As Integer
        Dim y As Integer
        Dim attempts As Integer

        count = 20 + (level * 5)
        If count > 60 Then count = 60

        placed = 0
        attempts = 0

        Do While placed < count And attempts < 500
            x = RandInt(FIELD_WIDTH)
            y = RandInt(FIELD_HEIGHT - PLAYER_ZONE - 1)

            If Me.GetMushroom(x, y) = 0 Then
                Me.SetMushroom(x, y, 4)  ' Full health.
                placed = placed + 1
            End If
            attempts = attempts + 1
        Loop
    End Sub

    ' Render a single mushroom at field coords (x, y). The colour and glyph
    ' depend on the mushroom's current health so the player sees visual
    ' decay as mushrooms take hits. Empty cells render as a single space —
    ' this also serves as the "clear" primitive for other objects.
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
            Viper.Terminal.SetColor(10, 0)  ' Bright green = full health.
            PRINT "@"
        ElseIf health = 3 Then
            Viper.Terminal.SetColor(2, 0)   ' Green.
            PRINT "@"
        ElseIf health = 2 Then
            Viper.Terminal.SetColor(3, 0)   ' Cyan.
            PRINT "o"
        Else
            Viper.Terminal.SetColor(8, 0)   ' Dark gray = about to die.
            PRINT "."
        End If
    End Sub

    ' Full redraw of the field: left and right vertical borders first, then
    ' every cell of the mushroom grid. Called once per level init to paint
    ' the initial state; after that, per-cell updates happen via
    ' `DrawMushroom` and `ClearPosition` to avoid redrawing the whole
    ' field every frame.
    Sub DrawField()
        Dim x As Integer
        Dim y As Integer

        ' Vertical borders flank the playfield from one row above to one
        ' row below, giving a visual frame.
        Viper.Terminal.SetColor(8, 0)
        For y = FIELD_TOP - 1 To FIELD_TOP + FIELD_HEIGHT
            Viper.Terminal.SetPosition(y, FIELD_LEFT - 1)
            PRINT "|"
            Viper.Terminal.SetPosition(y, FIELD_LEFT + FIELD_WIDTH)
            PRINT "|"
        Next y

        ' Paint every cell. Empty cells render as spaces so the previous
        ' frame's contents are cleared.
        For y = 0 To FIELD_HEIGHT - 1
            For x = 0 To FIELD_WIDTH - 1
                Me.DrawMushroom(x, y)
            Next x
        Next y
    End Sub

    ' Clear a single field cell on-screen. If the cell still holds a
    ' mushroom, redraw the mushroom (so moving actors don't accidentally
    ' erase terrain). Otherwise paint a space. Used by actors (player,
    ' bullet, centipede segments, spider) to wipe their old position
    ' before moving.
    Sub ClearPosition(x As Integer, y As Integer)
        Dim screenX As Integer
        Dim screenY As Integer
        Dim health As Integer

        screenX = FIELD_LEFT + x
        screenY = FIELD_TOP + y

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
