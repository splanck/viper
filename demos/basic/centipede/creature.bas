' ═══════════════════════════════════════════════════════════
' CREATURE.BAS - Centipede and spider entities
' ═══════════════════════════════════════════════════════════

' Constants as globals (parser doesn't support Const in array dims)
Dim MAX_SEGMENTS As Integer
Dim SPIDER_POINTS As Integer
Dim SEGMENT_POINTS As Integer

Sub InitCreatureConstants()
    MAX_SEGMENTS = 12
    SPIDER_POINTS = 50
    SEGMENT_POINTS = 10
End Sub

' Centipede segment
Class Segment
    Dim X As Integer
    Dim Y As Integer
    Dim DirX As Integer      ' -1 = left, 1 = right
    Dim Active As Integer    ' 1 = alive, 0 = dead
    Dim IsHead As Integer    ' 1 = head segment

    Sub New()
        Me.X = 0
        Me.Y = 0
        Me.DirX = 1
        Me.Active = 0
        Me.IsHead = 0
    End Sub
End Class

' The centipede - a chain of segments
Class Centipede
    Dim Segments(12) As Segment  ' MAX_SEGMENTS = 12
    Dim SegmentCount As Integer
    Dim MoveTimer As Integer
    Dim Speed As Integer      ' Ticks between moves

    Sub New()
        Dim i As Integer
        For i = 0 To MAX_SEGMENTS - 1
            Me.Segments(i) = New Segment
        Next i
        Me.SegmentCount = 0
        Me.MoveTimer = 0
        Me.Speed = 3
    End Sub

    ' Initialize centipede for a level
    Sub Init(level As Integer, startX As Integer)
        Dim i As Integer
        Dim length As Integer

        ' Longer centipede at higher levels
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

    ' Count active segments
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

    ' Move all segments, returns 1 if centipede reached player zone
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
                ' Try to move horizontally
                newX = seg.X + seg.DirX
                newY = seg.Y
                blocked = 0

                ' Check boundaries and mushrooms
                If newX < 0 Or newX >= FIELD_WIDTH Then
                    blocked = 1
                ElseIf field.GetMushroom(newX, newY) > 0 Then
                    blocked = 1
                End If

                If blocked = 1 Then
                    ' Move down and reverse direction
                    seg.Y = seg.Y + 1
                    seg.DirX = -seg.DirX
                    If seg.Y >= FIELD_HEIGHT Then
                        reachedBottom = 1
                    End If
                Else
                    seg.X = newX
                End If

                Me.Segments(i) = seg
            End If
        Next i

        Move = reachedBottom
    End Function

    ' Kill segment at index, returns points scored
    Function KillSegment(index As Integer, field As GameField) As Integer
        If index >= 0 And index < Me.SegmentCount Then
            If Me.Segments(index).Active = 1 Then
                Me.Segments(index).Active = 0
                ' Leave a mushroom where segment died
                field.SetMushroom(Me.Segments(index).X, Me.Segments(index).Y, 4)
                ' If not the head, the next segment becomes a head
                If index < Me.SegmentCount - 1 Then
                    Me.Segments(index + 1).IsHead = 1
                End If
                KillSegment = SEGMENT_POINTS
                Exit Function
            End If
        End If
        KillSegment = 0
    End Function

    ' Check if any segment is at position
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

    ' Draw all segments
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
                    Viper.Terminal.SetColor(12, 0)  ' Red head
                    PRINT "O"
                Else
                    Viper.Terminal.SetColor(10, 0)  ' Green body
                    PRINT "o"
                End If
            End If
        Next i
    End Sub

    ' Clear all segments from screen
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

' Spider enemy
Class Spider
    Dim X As Integer
    Dim Y As Integer
    Dim DirX As Integer
    Dim DirY As Integer
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

    ' Spawn spider at random edge
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

    ' Move spider erratically
    Sub Move(field As GameField)
        If Me.Active = 0 Then Exit Sub

        Me.MoveTimer = Me.MoveTimer + 1
        If Me.MoveTimer < 2 Then Exit Sub
        Me.MoveTimer = 0

        ' Erratic movement
        Me.X = Me.X + Me.DirX
        If RandInt(3) = 0 Then
            Me.Y = Me.Y + Me.DirY
        End If

        ' Randomly change vertical direction
        If RandInt(5) = 0 Then
            Me.DirY = -Me.DirY
        End If

        ' Keep in bounds
        If Me.Y < FIELD_HEIGHT - PLAYER_ZONE Then
            Me.Y = FIELD_HEIGHT - PLAYER_ZONE
            Me.DirY = 1
        End If
        If Me.Y >= FIELD_HEIGHT Then
            Me.Y = FIELD_HEIGHT - 1
            Me.DirY = -1
        End If

        ' Deactivate if left screen
        If Me.X < 0 Or Me.X >= FIELD_WIDTH Then
            Me.Active = 0
        End If

        ' Eat mushrooms it passes through
        If field.GetMushroom(Me.X, Me.Y) > 0 Then
            field.SetMushroom(Me.X, Me.Y, 0)
        End If
    End Sub

    ' Check if at position
    Function IsAt(x As Integer, y As Integer) As Integer
        If Me.Active = 1 And Me.X = x And Me.Y = y Then
            IsAt = 1
        Else
            IsAt = 0
        End If
    End Function

    ' Kill spider
    Sub Kill()
        Me.Active = 0
    End Sub

    ' Draw spider
    Sub Draw()
        Dim screenX As Integer
        Dim screenY As Integer

        If Me.Active = 1 Then
            screenX = FIELD_LEFT + Me.X
            screenY = FIELD_TOP + Me.Y

            Viper.Terminal.SetPosition(screenY, screenX)
            Viper.Terminal.SetColor(13, 0)  ' Magenta
            PRINT "X"
        End If
    End Sub

    ' Clear spider
    Sub Clear(field As GameField)
        If Me.Active = 1 Then
            field.ClearPosition(Me.X, Me.Y)
        End If
    End Sub
End Class
