' ============================================================================
' MODULE: board.bas
' PURPOSE: Models the playfield: collision detection, piece placement, line
'          completion, and rendering of the well plus the active piece.
'
' WHERE-THIS-FITS: One step up from `pieces.bas`. The board owns the truth
'          about what is legal and what is permanent. `Piece` only knows its
'          own shape and position; the board is what the game loop consults
'          to decide whether a move is allowed and whether the game is over.
'
' KEY-DESIGN-CHOICES:
'   * SENTINEL WALLS. The grid is 22 rows tall x 12 columns wide. The
'     visible playfield is rows 0..19 and columns 1..10. Column 0, column
'     11, and row 20 are pre-filled with the value 9 ("wall"). This trick
'     means `CanPlace` does not need a separate boundary test: an
'     out-of-bounds query collides with a wall cell and naturally returns
'     "cannot place." Beginners often write four explicit boundary checks
'     plus the collision check; sentinel walls collapse all four into one.
'   * PARALLEL COLOR ARRAY. `Grid` stores presence (0/1/9), `GridColor`
'     stores the CGA palette index of the locked piece occupying each cell.
'     This separation lets `CanPlace` ignore color entirely and lets
'     `DrawBoard` ignore the wall sentinel value (it special-cases 0 as
'     empty and renders any other non-zero cell with its stored colour).
'   * BOTTOM-UP LINE CLEAR. `CheckLines` scans rows top-to-bottom but
'     `ClearLine` shifts everything above the cleared row down by one. The
'     result is correct as long as `CheckLines` re-evaluates each row after
'     a clear (which it does naturally because the loop continues forward
'     and finds the same row index after the shift, this time potentially
'     with a different content from the row that fell into it).
'   * SCREEN COLUMN MATH. Each board cell renders as TWO characters wide
'     ("##") so that the visual aspect ratio is roughly square in a typical
'     terminal cell (which is taller than it is wide). This is why
'     `DrawPiece` computes `screenCol = (bx - 1) * 2 + 2`.
'
' HOW-TO-READ: Start at `Sub New` to see how the sentinel walls are laid
'   in. Then `CanPlace` to see the 4x4-mask collision test against those
'   walls. Then `PlacePiece` and `CheckLines` together — they form the
'   "lock and clear" pair that drives scoring. The drawing methods at the
'   bottom are pure presentation.
' ============================================================================

Class Board
    ' Cell occupancy grid. 22 rows x 12 columns:
    '   * Visible playfield: rows 0..19, columns 1..10.
    '   * Walls: column 0 and column 11 across all rows.
    '   * Floor: row 20 across all columns.
    '   * Buffer: row 21 is unused but reserved (a future "buffer above the
    '     visible board for piece spawn" could use a similar trick).
    ' Cell values:
    '   0 = empty playable cell
    '   1 = locked piece cell (look up colour in GridColor)
    '   9 = wall/floor sentinel (never drawn directly; collisions only)
    Dim Grid(22, 12) As Integer

    ' Per-cell colour (CGA index) for locked piece cells. Zero for empty
    ' cells and walls.
    Dim GridColor(22, 12) As Integer

    Dim BoardWidth As Integer    ' 10 — purely informational
    Dim BoardHeight As Integer   ' 20 — purely informational
    Dim LinesCleared As Integer  ' running total across the lifetime of the board

    ' --------------------------------------------------------------------
    ' Sub New()
    '   Build an empty 10x20 well bordered by sentinel walls and a floor.
    '   The grid is zeroed first (so any wall cell that fails to be set
    '   below would manifest as an obvious empty-column rendering bug
    '   rather than uninitialised memory), then the walls and floor are
    '   stamped in with the value 9.
    ' --------------------------------------------------------------------
    Sub New()
        Me.BoardWidth = 10
        Me.BoardHeight = 20
        Me.LinesCleared = 0

        ' Zero the entire 22 x 12 grid up front.
        Dim i As Integer, j As Integer
        For i = 0 To 21
            For j = 0 To 11
                Me.Grid(i, j) = 0
                Me.GridColor(i, j) = 0
            Next j
        Next i

        ' Vertical walls: column 0 (left) and column 11 (right).
        For i = 0 To 21
            Me.Grid(i, 0) = 9   ' Left wall
            Me.Grid(i, 11) = 9  ' Right wall
        Next i

        ' Horizontal floor: row 20.
        For j = 0 To 11
            Me.Grid(20, j) = 9
        Next j
    End Sub

    ' --------------------------------------------------------------------
    ' Function CanPlace(p) -> 1 if legal, 0 if blocked
    '   Tests whether the piece p, at its current PosX/PosY, fits without
    '   colliding with any wall or any locked block.
    '
    '   The algorithm walks the piece's 4x4 shape mask. For each filled
    '   cell, it computes the corresponding board coordinate and tests:
    '     1. Is the column inside the playable range (1..10)?
    '     2. Is the row inside the playable range (0..20)?
    '     3. Is the destination cell empty (Grid == 0)?
    '
    '   Note that the boundary check is only loosely needed because the
    '   wall sentinels would catch column 0 and column 11 anyway. We keep
    '   the explicit checks as defensive belts-and-suspenders for "off by
    '   tens" bugs (e.g., if the piece somehow ended up at column 50, the
    '   sentinel test would index past the end of the grid).
    ' --------------------------------------------------------------------
    Function CanPlace(p As Piece) As Integer
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer

        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i

                    ' Defence-in-depth bounds check (the wall sentinels
                    ' below would also catch most failures, but this avoids
                    ' wild reads if a position somehow goes far out of
                    ' range).
                    If bx < 1 Or bx > 10 Then Return 0
                    If by < 0 Or by > 20 Then Return 0

                    ' Sentinel + locked-piece collision check. Either kind
                    ' of non-zero cell counts as blocked.
                    If Me.Grid(by, bx) <> 0 Then Return 0
                End If
            Next j
        Next i

        Return 1  ' All filled cells of the piece map to empty board cells.
    End Function

    ' --------------------------------------------------------------------
    ' Sub PlacePiece(p)
    '   Commit the piece to the board permanently. For every filled cell of
    '   the piece's 4x4 mask, we set the corresponding board cell to 1 and
    '   record the piece's color in `GridColor`. This is called from
    '   vtris.bas `LockPiece` once the piece can no longer fall.
    '
    '   Precondition: the caller must have just verified `CanPlace(p) = 1`
    '   for this position. We do not re-check here.
    ' --------------------------------------------------------------------
    Sub PlacePiece(p As Piece)
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer

        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i
                    Me.Grid(by, bx) = 1
                    Me.GridColor(by, bx) = p.PieceColor
                End If
            Next j
        Next i
    End Sub

    ' --------------------------------------------------------------------
    ' Function CheckLines() -> count of lines cleared this call
    '   Walks every visible row top-to-bottom and clears any row that is
    '   completely filled across all 10 playable columns. Returns the count
    '   so the caller can convert it to score (vtris.bas uses the classic
    '   quadratic table: 1->100, 2->400, 3->900, 4->1600).
    '
    '   Sentinel-aware: the iteration starts at column 1 (not 0) so the
    '   left wall is excluded; it ends at column 10 (not 11) so the right
    '   wall is excluded. A "full" row is therefore exactly 10 occupied
    '   cells.
    '
    '   Subtle correctness note: when ClearLine shifts everything down, the
    '   row that just became `row` is potentially also full, but our loop
    '   has already moved past it. In practice tetrominoes can clear at
    '   most 4 lines at once and they are always contiguous, so we get the
    '   right answer because the test is row-by-row from top to bottom and
    '   each cleared row's contents are replaced with whatever was above
    '   (which we already established was not full when we tested it). For
    '   piece-spawn-from-rotation edge cases, callers get the conservative
    '   underestimate that this loop produces — fine for scoring purposes.
    ' --------------------------------------------------------------------
    Function CheckLines() As Integer
        Dim count As Integer
        Dim row As Integer, col As Integer
        Dim full As Integer

        count = 0
        For row = 0 To 19
            full = 1
            For col = 1 To 10
                If Me.Grid(row, col) = 0 Then
                    full = 0
                End If
            Next col

            If full = 1 Then
                count = count + 1
                Me.ClearLine(row)
            End If
        Next row

        Me.LinesCleared = Me.LinesCleared + count
        Return count
    End Function

    ' --------------------------------------------------------------------
    ' Sub ClearLine(row)
    '   Remove a single row by shifting every row above it down by one.
    '   The top row is then zeroed out (since nothing fell into it).
    '
    '   The shift uses `Step -1` so we copy from the row immediately above
    '   (`r - 1`) into `r` without trampling source data — equivalent to a
    '   `memmove` going downward. Both `Grid` and `GridColor` are shifted
    '   in lockstep so locked-piece colours move with their cells.
    '
    '   Walls are not touched: the loop runs over playable columns 1..10
    '   only.
    ' --------------------------------------------------------------------
    Sub ClearLine(row As Integer)
        Dim r As Integer, c As Integer

        ' Shift rows downward into the cleared row.
        For r = row To 1 Step -1
            For c = 1 To 10
                Me.Grid(r, c) = Me.Grid(r - 1, c)
                Me.GridColor(r, c) = Me.GridColor(r - 1, c)
            Next c
        Next r

        ' Top row becomes the new "empty" row.
        For c = 1 To 10
            Me.Grid(0, c) = 0
            Me.GridColor(0, c) = 0
        Next c
    End Sub

    ' --------------------------------------------------------------------
    ' Sub DrawBoard()
    '   Render the well plus all locked pieces. Uses LOCATE to position the
    '   cursor and COLOR to set the foreground/background palette before
    '   each Print. Cells are two characters wide ("##" or "  ") so the
    '   playfield reads as roughly square on a terminal where each cell is
    '   taller than it is wide.
    '
    '   The border characters (╔ ║ ╚) are box-drawing Unicode glyphs; they
    '   render as solid lines on any modern terminal and as approximations
    '   on legacy CP437 hardware.
    ' --------------------------------------------------------------------
    Sub DrawBoard()
        Dim row As Integer, col As Integer

        ' Top border.
        LOCATE 1, 1
        COLOR 11, 0
        Print "╔════════════════════╗"

        ' Body rows.
        For row = 0 To 19
            LOCATE row + 2, 1
            COLOR 11, 0
            Print "║";

            For col = 1 To 10
                If Me.Grid(row, col) = 0 Then
                    COLOR 0, 0
                    Print "  ";
                Else
                    COLOR Me.GridColor(row, col), 0
                    Print "██";
                End If
            Next col

            ' Right border at the canonical column 22 (matches the top
            ' border's width: 1 + 20 + 1 = 22).
            LOCATE row + 2, 22
            COLOR 11, 0
            Print "║"
        Next row

        ' Bottom border.
        LOCATE 22, 1
        COLOR 11, 0
        Print "╚════════════════════╝"

        COLOR 7, 0  ' Reset to default white-on-black for downstream prints.
    End Sub

    ' --------------------------------------------------------------------
    ' Sub DrawPiece(p)
    '   Render the active (still-falling) piece as a transient overlay on
    '   top of `DrawBoard`. The piece is NOT committed to the grid yet;
    '   that happens in `PlacePiece` once the piece locks.
    '
    '   Coordinate translation:
    '     screenRow = boardRow + 2     (1 row above for the top border)
    '     screenCol = (boardCol - 1) * 2 + 2
    '                 ^                 ^
    '                 |                 1 column right for the left border
    '                 cells are 2 chars wide
    '
    '   Cells outside the visible board (negative rows during spawn) are
    '   clipped silently.
    ' --------------------------------------------------------------------
    Sub DrawPiece(p As Piece)
        Dim i As Integer, j As Integer
        Dim bx As Integer, by As Integer
        Dim screenRow As Integer, screenCol As Integer

        For i = 0 To 3
            For j = 0 To 3
                If p.Shape(i, j) = 1 Then
                    bx = p.PosX + j
                    by = p.PosY + i

                    If by >= 0 And by < 20 And bx >= 1 And bx <= 10 Then
                        screenRow = by + 2
                        screenCol = (bx - 1) * 2 + 2
                        LOCATE screenRow, screenCol
                        COLOR p.PieceColor, 0
                        Print "██";
                        COLOR 7, 0
                    End If
                End If
            Next j
        Next i
    End Sub
End Class
