' ============================================================================
' MODULE: creature.bas
' PURPOSE: Defines the two enemy entities: the Centipede (a chain of
'          segments that snakes down the field) and the Spider (a single
'          erratic creature that patrols the player zone).
'
' WHERE-THIS-FITS: Second in the load order (after field.bas). References
'          `FIELD_WIDTH`, `FIELD_HEIGHT`, and `PLAYER_ZONE` from field.bas,
'          plus `RandInt`. Consumed by centipede.bas (the main game loop
'          ticks centipede + spider each frame and tests their positions
'          against the player).
'
' KEY-DESIGN-CHOICES:
'   * CENTIPEDE AS A FIXED ARRAY. Unlike a classic linked-list centipede,
'     segments live in a pre-allocated `Segments(12)` array. The head is
'     index 0; subsequent segments trail in index order. Killing a
'     segment sets its `Active` flag to 0 but leaves the slot in place —
'     this avoids array shuffling and preserves the centipede's visible
'     "broken into two pieces" behaviour.
'   * BOUNCE-AND-DROP MOVEMENT. The canonical Centipede AI is:
'       1. Walk horizontally in the current direction.
'       2. If blocked (wall or mushroom), drop down one row and reverse.
'     Each segment makes this decision INDEPENDENTLY based on what's in
'     front of *it*, which is why the centipede can split into multiple
'     trains when segments in the middle get killed and the survivors
'     each hit mushrooms at different times. This is implemented in
'     `Move`: the per-segment loop tests the cell ahead and decides.
'   * KILLED SEGMENT -> MUSHROOM. `KillSegment` plants a full-health
'     mushroom where the segment died. This is the core feedback loop of
'     Centipede: shoot the centipede and you make the board harder
'     (more obstacles). It rewards accuracy (shoot at a spot you can
'     reach later) and punishes spray shooting.
'   * HEAD PROMOTION. When you kill a non-tail segment, the segment
'     immediately behind it is marked `IsHead = 1`. This causes two
'     independent heads to emerge from a single shot — visible in the
'     game as two split chains moving in different directions.
'   * SPIDER IS A SEPARATE CLASS. Its AI is a random-walk inside the
'     player zone. It spawns at a random edge, moves in semi-random
'     direction every few ticks, and eats any mushroom it passes
'     through. The spider is the Centipede risk/reward mechanic — it
'     can kill you on contact but shooting it is worth 50 points and
'     clears mushrooms in its path.
'   * SPEED SCALING. `Init(level, ...)` reduces `Speed` as level rises so
'     higher levels make the centipede tick faster. `Speed = 4 - (level /
'     3)` with a floor of 1 gives noticeable speedup up to level 9.
'
' HOW-TO-READ: Start at `Class Segment` for the per-segment state, then
'   `Class Centipede.Move` to see the bounce-and-drop AI. Then `KillSegment`
'   to see the mushroom-feedback + head-promotion mechanics. Finally the
'   Spider class for the erratic-walker pattern.
' ============================================================================

' Shared constants. Kept as Dims because the parser does not support Const
' in array-size declarations (see field.bas for the same pattern).
Dim MAX_SEGMENTS As Integer
Dim SPIDER_POINTS As Integer
Dim SEGMENT_POINTS As Integer

' Seed the creature-side constants. Called once from the main program.
' Separating initialization from declaration lets the numbers sit next to
' each other for easy tuning.
Sub InitCreatureConstants()
    MAX_SEGMENTS = 12   ' Cap on centipede length. Array is sized to match.
    SPIDER_POINTS = 50  ' Per spider kill.
    SEGMENT_POINTS = 10 ' Per centipede segment kill.
End Sub

' ===========================================================================
' Class Segment
'   One link in the centipede chain. Holds its field coords, the horizontal
'   direction it's currently travelling, an active flag, and a head marker.
'   Kept as a plain data record — all the AI lives in `Centipede.Move`.
' ===========================================================================
Class Segment
    Dim X As Integer        ' Field column (0 .. FIELD_WIDTH - 1).
    Dim Y As Integer        ' Field row    (0 .. FIELD_HEIGHT - 1).
    Dim DirX As Integer     ' -1 = moving left, +1 = moving right.
    Dim Active As Integer   ' 1 = alive and should be drawn / collided; 0 = dead.
    Dim IsHead As Integer   ' 1 if this segment renders as the capital O head glyph.

    Sub New()
        Me.X = 0
        Me.Y = 0
        Me.DirX = 1
        Me.Active = 0
        Me.IsHead = 0
    End Sub
End Class

' ===========================================================================
' Class Centipede
'   The centipede as a whole: a pre-allocated array of `Segment`s plus the
'   per-tick movement logic. Each level constructs a new Centipede (via
'   `New Centipede` in centipede.bas `InitLevel`) and immediately calls
'   `Init` to configure its length and speed for that level.
' ===========================================================================
Class Centipede
    Dim Segments(12) As Segment  ' Slots; real length is `SegmentCount`.
    Dim SegmentCount As Integer  ' How many slots are in use (0..MAX_SEGMENTS).
    Dim MoveTimer As Integer     ' Ticks since last movement pass.
    Dim Speed As Integer         ' Move once every `Speed` ticks.

    ' Pre-allocate all 12 segment slots so we never malloc during gameplay.
    ' `Init` fills them in. `SegmentCount` starts at 0 so a fresh Centipede
    ' is "empty" until `Init` populates it.
    Sub New()
        Dim i As Integer
        For i = 0 To MAX_SEGMENTS - 1
            Me.Segments(i) = New Segment
        Next i
        Me.SegmentCount = 0
        Me.MoveTimer = 0
        Me.Speed = 3
    End Sub

    ' Configure the centipede for a level:
    '   * Length: 8 + level, capped at MAX_SEGMENTS.
    '   * Speed:  4 - (level / 3), floored at 1. Lower = faster.
    '   * Position: one horizontal row at `y = 0` starting at `startX` and
    '     trailing to the left (segment i is at `startX - i`). This places
    '     the head on the right with the body trailing behind.
    '   * Direction: all segments start moving right (`DirX = 1`). Once
    '     they hit the right wall the bounce-and-drop in `Move` takes over.
    Sub Init(level As Integer, startX As Integer)
        Dim i As Integer
        Dim length As Integer

        length = 8 + level
        If length > MAX_SEGMENTS Then length = MAX_SEGMENTS

        Me.SegmentCount = length
        Me.Speed = 4 - (level / 3)
        If Me.Speed < 1 Then Me.Speed = 1
        Me.MoveTimer = 0

        For i = 0 To length - 1
            Me.Segments(i).X = startX - i
            Me.Segments(i).Y = 0
            Me.Segments(i).DirX = 1
            Me.Segments(i).Active = 1
            If i = 0 Then
                Me.Segments(i).IsHead = 1
            Else
                Me.Segments(i).IsHead = 0
            End If
        Next i
    End Sub

    ' Count segments still alive. Used by the main loop to detect "level
    ' complete" (all segments dead). O(n) but n <= 12.
    Function ActiveCount() As Integer
        Dim count As Integer
        Dim i As Integer
        count = 0
        For i = 0 To Me.SegmentCount - 1
            If Me.Segments(i).Active = 1 Then
                count = count + 1
            End If
        Next i
        ActiveCount = count
    End Function

    ' Tick the centipede. Advances the MoveTimer; when it hits `Speed`, runs
    ' one movement pass over every active segment. Returns 1 if any segment
    ' crossed the bottom of the field (meaning the player loses a life).
    '
    ' The per-segment movement rule is the classic Centipede AI:
    '   1. Try to move horizontally in the current direction.
    '   2. If that cell is out-of-bounds OR has a mushroom, "bounce":
    '        - Drop down one row.
    '        - Reverse horizontal direction.
    '        - If now past the bottom, flag `reachedBottom` for the caller.
    '   3. Otherwise, commit the horizontal move.
    '
    ' IMPORTANT: the BASIC value-semantic assignment `seg = Me.Segments(i)`
    ' copies the segment. Any mutations go through `seg.X = ...` etc. and
    ' the final `Me.Segments(i) = seg` writes the modified copy back. This
    ' looks redundant but is required: without the writeback the changes
    ' would be thrown away.
    Function Move(field As GameField) As Integer
        Dim i As Integer
        Dim seg As Segment
        Dim newX As Integer
        Dim newY As Integer
        Dim blocked As Integer
        Dim reachedBottom As Integer

        Me.MoveTimer = Me.MoveTimer + 1
        If Me.MoveTimer < Me.Speed Then
            Move = 0
            Exit Function
        End If
        Me.MoveTimer = 0
        reachedBottom = 0

        For i = 0 To Me.SegmentCount - 1
            seg = Me.Segments(i)
            If seg.Active = 1 Then
                newX = seg.X + seg.DirX
                newY = seg.Y
                blocked = 0

                ' Wall or mushroom blocks the horizontal step.
                If newX < 0 Or newX >= FIELD_WIDTH Then
                    blocked = 1
                ElseIf field.GetMushroom(newX, newY) > 0 Then
                    blocked = 1
                End If

                If blocked = 1 Then
                    ' Drop down one row and reverse direction.
                    seg.Y = seg.Y + 1
                    seg.DirX = -seg.DirX
                    If seg.Y >= FIELD_HEIGHT Then
                        reachedBottom = 1
                    End If
                Else
                    seg.X = newX
                End If

                Me.Segments(i) = seg  ' Commit the mutated copy back.
            End If
        Next i

        Move = reachedBottom
    End Function

    ' Kill the segment at the given index. Returns the point bonus.
    '
    ' Three side effects besides flipping Active -> 0:
    '   1. Plant a full-health mushroom at the dead segment's position.
    '      This is the core Centipede feedback loop — shooting the
    '      centipede adds obstacles to the board.
    '   2. If the dead segment was not the last one in the chain, promote
    '      the next segment to "head" status so the surviving tail
    '      becomes a new independent chain with its own rendered head.
    '   3. Return the per-segment point value (SEGMENT_POINTS = 10) so the
    '      caller can add it to the player's score.
    ' Out-of-range or already-dead segments return 0 with no effect.
    Function KillSegment(index As Integer, field As GameField) As Integer
        If index >= 0 And index < Me.SegmentCount Then
            If Me.Segments(index).Active = 1 Then
                Me.Segments(index).Active = 0
                ' Feedback loop: killed segment -> new mushroom.
                field.SetMushroom(Me.Segments(index).X, Me.Segments(index).Y, 4)
                ' Head promotion for the trailing sub-chain.
                If index < Me.SegmentCount - 1 Then
                    Me.Segments(index + 1).IsHead = 1
                End If
                KillSegment = SEGMENT_POINTS
                Exit Function
            End If
        End If
        KillSegment = 0
    End Function

    ' Find the index of the first active segment at (x, y), or -1 if none.
    ' Used for both bullet/centipede collision and player/centipede
    ' collision. O(n) but n is small.
    Function SegmentAt(x As Integer, y As Integer) As Integer
        Dim i As Integer
        For i = 0 To Me.SegmentCount - 1
            If Me.Segments(i).Active = 1 Then
                If Me.Segments(i).X = x And Me.Segments(i).Y = y Then
                    SegmentAt = i
                    Exit Function
                End If
            End If
        Next i
        SegmentAt = -1
    End Function

    ' Render every active segment at its current position. Head segments
    ' draw as capital "O" in red; body segments draw as lowercase "o" in
    ' green. The visual head/body split helps the player spot the "fronts"
    ' of each sub-chain.
    Sub Draw()
        Dim i As Integer
        Dim seg As Segment
        Dim screenX As Integer
        Dim screenY As Integer

        For i = 0 To Me.SegmentCount - 1
            seg = Me.Segments(i)
            If seg.Active = 1 Then
                screenX = FIELD_LEFT + seg.X
                screenY = FIELD_TOP + seg.Y

                Viper.Terminal.SetPosition(screenY, screenX)
                If seg.IsHead = 1 Then
                    Viper.Terminal.SetColor(12, 0)  ' Red head.
                    PRINT "O"
                Else
                    Viper.Terminal.SetColor(10, 0)  ' Green body.
                    PRINT "o"
                End If
            End If
        Next i
    End Sub

    ' Wipe every active segment from the screen. Called at the start of
    ' each tick so the redraw after `Move` doesn't leave ghost trails. Uses
    ' `field.ClearPosition` which will restore any mushroom that was
    ' already at that cell, keeping terrain intact.
    Sub Clear(field As GameField)
        Dim i As Integer
        Dim seg As Segment

        For i = 0 To Me.SegmentCount - 1
            seg = Me.Segments(i)
            If seg.Active = 1 Then
                field.ClearPosition(seg.X, seg.Y)
            End If
        Next i
    End Sub
End Class

' ===========================================================================
' Class Spider
'   The roaming secondary enemy. Spawns at a random side of the field,
'   walks erratically inside the player zone, eats mushrooms it passes
'   over, and dies if shot (50 pts) or walked into (player dies).
' ===========================================================================
Class Spider
    Dim X As Integer
    Dim Y As Integer
    Dim DirX As Integer   ' -1 / +1 — constant between spawns.
    Dim DirY As Integer   ' -1 / +1 — flips randomly during `Move`.
    Dim Active As Integer
    Dim MoveTimer As Integer

    Sub New()
        Me.X = 0
        Me.Y = 0
        Me.DirX = 1
        Me.DirY = 1
        Me.Active = 0
        Me.MoveTimer = 0
    End Sub

    ' Spawn the spider at a random left or right edge, somewhere inside the
    ' player zone. Picks an initial DirY randomly so it doesn't always
    ' walk the same pattern. This is called when the `SpiderSpawnTimer` in
    ' centipede.bas expires.
    Sub Spawn()
        Me.Y = FIELD_HEIGHT - RandInt(PLAYER_ZONE) - 1
        If RandInt(2) = 0 Then
            Me.X = 0
            Me.DirX = 1
        Else
            Me.X = FIELD_WIDTH - 1
            Me.DirX = -1
        End If
        Me.DirY = 1
        If RandInt(2) = 0 Then
            Me.DirY = -1
        End If
        Me.Active = 1
        Me.MoveTimer = 0
    End Sub

    ' Erratic movement: always step horizontally, step vertically one-third
    ' of the time, randomly flip vertical direction one-fifth of the time.
    ' Kept inside the player zone by clamping Y. Walks off the edge at
    ' X < 0 or X >= FIELD_WIDTH -> auto-deactivate. Eats any mushroom it
    ' passes over (sets it to 0). The ticks-every-2 cadence makes the
    ' spider move at roughly half the player's speed.
    Sub Move(field As GameField)
        If Me.Active = 0 Then Exit Sub

        Me.MoveTimer = Me.MoveTimer + 1
        If Me.MoveTimer < 2 Then Exit Sub
        Me.MoveTimer = 0

        ' Horizontal always, vertical sometimes.
        Me.X = Me.X + Me.DirX
        If RandInt(3) = 0 Then
            Me.Y = Me.Y + Me.DirY
        End If

        ' Randomly flip vertical direction (20% of ticks).
        If RandInt(5) = 0 Then
            Me.DirY = -Me.DirY
        End If

        ' Clamp to the player zone: if the spider wanders above it, snap
        ' back and force a downward motion. If it walks off the bottom,
        ' snap back and force upward.
        If Me.Y < FIELD_HEIGHT - PLAYER_ZONE Then
            Me.Y = FIELD_HEIGHT - PLAYER_ZONE
            Me.DirY = 1
        End If
        If Me.Y >= FIELD_HEIGHT Then
            Me.Y = FIELD_HEIGHT - 1
            Me.DirY = -1
        End If

        ' Exit off-screen -> deactivate. The spawn timer will eventually
        ' trigger a fresh spider.
        If Me.X < 0 Or Me.X >= FIELD_WIDTH Then
            Me.Active = 0
        End If

        ' Eat any mushroom we just walked onto. Clears obstacles for the
        ' player and keeps the spider from getting stuck behind them.
        If field.GetMushroom(Me.X, Me.Y) > 0 Then
            field.SetMushroom(Me.X, Me.Y, 0)
        End If
    End Sub

    ' Quick "am I at this cell?" test used for both bullet collision and
    ' player collision.
    Function IsAt(x As Integer, y As Integer) As Integer
        If Me.Active = 1 And Me.X = x And Me.Y = y Then
            IsAt = 1
        Else
            IsAt = 0
        End If
    End Function

    ' Flip the active flag to 0. The next `Spawn` will replace this
    ' spider's state entirely.
    Sub Kill()
        Me.Active = 0
    End Sub

    ' Render the spider as a magenta "X" at its current screen position.
    ' No-op when inactive.
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer

        If Me.Active = 1 Then
            screenX = FIELD_LEFT + Me.X
            screenY = FIELD_TOP + Me.Y

            Viper.Terminal.SetPosition(screenY, screenX)
            Viper.Terminal.SetColor(13, 0)  ' Magenta.
            PRINT "X"
        End If
    End Sub

    ' Erase the spider from its current position, restoring any underlying
    ' mushroom. Paired with `Draw` each frame to produce clean movement.
    Sub Clear(field As GameField)
        If Me.Active = 1 Then
            field.ClearPosition(Me.X, Me.Y)
        End If
    End Sub
End Class
