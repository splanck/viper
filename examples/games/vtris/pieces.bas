' ============================================================================
' MODULE: pieces.bas
' PURPOSE: Defines the seven standard Tetris tetrominoes (I, O, T, S, Z, J, L)
'          and the operations that act on a single piece in isolation:
'          shape initialisation, clockwise rotation, and translation.
'
' WHERE-THIS-FITS: This is the leaf module of the vTRIS demo. It has no
'          dependencies on the board or the scoreboard — a `Piece` knows its
'          shape, color, position, and rotation state, and nothing else. The
'          legality of a position (collision, out-of-bounds) is the Board's
'          responsibility, queried via `Board.CanPlace(piece)`. This split
'          lets you change rotation strategies (e.g., SRS wall-kicks) by
'          editing only this file.
'
' KEY-DESIGN-CHOICES:
'   * Tetrominoes are stored as a 4x4 bitmap of integers (0 = empty,
'     1 = filled). 4x4 is the smallest square that contains every standard
'     Tetris piece in every rotation, including the I-piece.
'   * Rotation uses the matrix identity `new[i][j] = old[3-j][i]` — a
'     transpose followed by a horizontal flip. This is the conventional
'     "rotate 90 clockwise on a square" operation. The result is computed
'     into a scratch `TempShape` field rather than a fresh local array so
'     the hot path allocates nothing per rotation.
'   * The piece's identity is its numeric `PieceType` (0..6). `InitShape`
'     reads this once at construction; `PieceColor` is set at the same time
'     using the standard CGA-style palette indices.
'   * `MoveLeft`/`MoveRight`/`MoveDown` only mutate coordinates. The caller
'     is responsible for asking the board whether the new position is legal,
'     and for undoing the move if it is not. This keeps the piece pure.
'
' HOW-TO-READ: Start at `Sub New` to see how a piece is born (type-driven
'   color + shape). Then read `InitShape` for the seven canonical layouts.
'   Then `RotateClockwise` to see the matrix transform in action. The three
'   movement helpers at the bottom are trivial.
' ============================================================================

Class Piece
    ' Shape grid. Cell (i, j) is 1 when the piece occupies row i, column j of
    ' the 4x4 bounding box. Indexing convention: i = row (top-to-bottom),
    ' j = column (left-to-right). The same orientation is used by the board.
    Dim Shape(4, 4) As Integer

    ' Pre-allocated scratch buffer used by RotateClockwise. Lifting this out
    ' of the function avoids a per-rotation allocation inside the game loop.
    Dim TempShape(4, 4) As Integer

    ' CGA-style palette index used when drawing this piece.
    Dim PieceColor As Integer

    ' Numeric identity (0=I, 1=O, 2=T, 3=S, 4=Z, 5=J, 6=L). Set once at
    ' construction; treat as immutable thereafter.
    Dim PieceType As Integer

    ' Current rotation state, 0..3. Pure bookkeeping today — collision logic
    ' does not consult it directly because `Shape` is the source of truth —
    ' but it is exposed so future wall-kick code (e.g., SRS) can branch on it.
    Dim Rotation As Integer

    ' Top-left coordinate of the 4x4 bounding box on the board's grid.
    ' Movement methods mutate these directly.
    Dim PosX As Integer
    Dim PosY As Integer

    ' --------------------------------------------------------------------
    ' Sub New(pType)
    '   Construct a piece of the given type, positioned near the top-centre
    '   of the board (PosX = 3, PosY = 0). This matches the standard Tetris
    '   spawn rule of "row 0, column where the piece's bounding box starts
    '   on the third board column from the left." `SpawnPiece` in vtris.bas
    '   later overrides PosX to 4 for visual centring; both call sites work
    '   because the constructor's positioning is a sensible default rather
    '   than a contract.
    '
    '   The shape grid is zeroed before `InitShape` is called so that future
    '   piece types added to `InitShape` do not need their own pre-zero loop.
    ' --------------------------------------------------------------------
    Sub New(pType As Integer)
        Me.PieceType = pType
        Me.Rotation = 0
        Me.PosX = 3  ' Start in middle of board
        Me.PosY = 0

        ' Zero the shape grid before InitShape stamps in the live cells.
        Dim i As Integer, j As Integer
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = 0
            Next j
        Next i

        ' Map piece type to a stable colour. The choices roughly follow the
        ' canonical Tetris palette (I = cyan, O = yellow, T = magenta, etc.)
        ' but are rounded to the nearest CGA index since BASIC's COLOR uses
        ' an integer palette rather than RGB triples.
        If pType = 0 Then Me.PieceColor = 6      ' I = Cyan
        If pType = 1 Then Me.PieceColor = 3      ' O = Yellow
        If pType = 2 Then Me.PieceColor = 5      ' T = Magenta
        If pType = 3 Then Me.PieceColor = 2      ' S = Green
        If pType = 4 Then Me.PieceColor = 1      ' Z = Red
        If pType = 5 Then Me.PieceColor = 4      ' J = Blue
        If pType = 6 Then Me.PieceColor = 7      ' L = White

        ' Stamp in the piece-specific filled cells.
        Me.InitShape()
    End Sub

    ' --------------------------------------------------------------------
    ' Sub InitShape()
    '   Stamps the seven canonical tetromino layouts into the 4x4 shape
    '   grid. Each piece is drawn in a "spawn orientation" that lines up
    '   with how Tetris players expect a piece to appear at the top of the
    '   well. The grid uses (row, col) indexing so the first index moves
    '   downward in screen space.
    '
    '   We use seven independent `If` statements rather than a `Select Case`
    '   so the file remains easy to copy-paste from when adding new piece
    '   variants (e.g., for educational mods that introduce pentominoes).
    '   The runtime cost is negligible: this runs once per spawn, not per
    '   frame.
    ' --------------------------------------------------------------------
    Sub InitShape()
        ' I-piece: a horizontal bar in row 1.
        '   . . . .
        '   # # # #
        '   . . . .
        '   . . . .
        If Me.PieceType = 0 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(2, 1) = 1
            Me.Shape(3, 1) = 1
        End If

        ' O-piece: 2x2 square in the upper-left quadrant. Rotation is a
        ' no-op for this piece (which is a fun detail to point out when
        ' explaining the rotation matrix to a beginner).
        If Me.PieceType = 1 Then
            Me.Shape(0, 1) = 1
            Me.Shape(0, 2) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If

        ' T-piece: bump on top, three across the middle.
        If Me.PieceType = 2 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If

        ' S-piece: zig-zag, top-right + bottom-left.
        If Me.PieceType = 3 Then
            Me.Shape(0, 1) = 1
            Me.Shape(0, 2) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(1, 1) = 1
        End If

        ' Z-piece: zig-zag, mirror of S.
        If Me.PieceType = 4 Then
            Me.Shape(0, 0) = 1
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(1, 2) = 1
        End If

        ' J-piece: hook turning left.
        If Me.PieceType = 5 Then
            Me.Shape(0, 1) = 1
            Me.Shape(1, 1) = 1
            Me.Shape(2, 0) = 1
            Me.Shape(2, 1) = 1
        End If

        ' L-piece: hook turning right.
        If Me.PieceType = 6 Then
            Me.Shape(0, 0) = 1
            Me.Shape(1, 0) = 1
            Me.Shape(2, 0) = 1
            Me.Shape(2, 1) = 1
        End If
    End Sub

    ' --------------------------------------------------------------------
    ' Sub RotateClockwise()
    '   Rotate the shape 90 degrees clockwise within its 4x4 bounding box.
    '
    '   The mathematical identity for "rotate 90 CW on a square matrix" is:
    '
    '       new[i][j] = old[N - 1 - j][i]
    '
    '   With N = 4, that becomes `new[i][j] = old[3 - j][i]`. Intuitively:
    '   the new row i is the old column i read bottom-to-top.
    '
    '   We need a temporary because the destination overlaps the source.
    '   Rather than allocate a fresh 4x4 array per call, we reuse the
    '   pre-allocated `TempShape` field. The three nested loops below cost
    '   16 reads + 16 writes + 16 reads + 16 writes — roughly 64 array
    '   accesses, all on a tiny matrix, so this is essentially free.
    '
    '   Bookkeeping: `Rotation` is incremented mod 4 using bitwise AND with
    '   3 (cheaper than `MOD` and equivalent for non-negative integers).
    '
    '   COLLISION HANDLING IS NOT THE PIECE'S JOB. The caller (vtris.bas
    '   `GameLoop`) checks `Board.CanPlace` after the rotation; if the new
    '   orientation collides, it rotates three more times to undo. A more
    '   advanced implementation would try wall-kick offsets here.
    ' --------------------------------------------------------------------
    Sub RotateClockwise()
        Dim i As Integer, j As Integer

        ' Snapshot the current shape into the scratch buffer.
        For i = 0 To 3
            For j = 0 To 3
                Me.TempShape(i, j) = Me.Shape(i, j)
            Next j
        Next i

        ' Zero the live shape so the rotation can write into it freely.
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = 0
            Next j
        Next i

        ' Apply the 90-CW transform: new[i][j] = old[3 - j][i].
        For i = 0 To 3
            For j = 0 To 3
                Me.Shape(i, j) = Me.TempShape(3 - j, i)
            Next j
        Next i

        ' Update the rotation counter (0..3). `AND 3` is a 2-bit mask that
        ' wraps the value, equivalent to `MOD 4` for non-negative ints.
        Me.Rotation = (Me.Rotation + 1) And 3
    End Sub

    ' --------------------------------------------------------------------
    ' Movement helpers. These are coordinate-only — they do not consult
    ' the board, so they cannot fail. The caller checks legality with
    ' `Board.CanPlace` after the move and undoes the move if needed.
    ' --------------------------------------------------------------------

    ' Slide the piece one cell to the left (decrement PosX).
    Sub MoveLeft()
        Me.PosX = Me.PosX - 1
    End Sub

    ' Slide the piece one cell to the right (increment PosX).
    Sub MoveRight()
        Me.PosX = Me.PosX + 1
    End Sub

    ' Slide the piece one cell down (increment PosY). Used both for the
    ' auto-drop tick and for the soft/hard-drop player actions.
    Sub MoveDown()
        Me.PosY = Me.PosY + 1
    End Sub
End Class
