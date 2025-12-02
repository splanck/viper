' board.bas - Chess board representation and rendering
' Uses ANSI colors and box-drawing characters
' Note: pieces.bas constants are available via AddFile concatenation

CLASS Board
    ' Board representation: positive = white, negative = black
    DIM Squares(8, 8) AS INTEGER
    DIM WhiteKingMoved AS INTEGER
    DIM BlackKingMoved AS INTEGER
    DIM WhiteRookAMoved AS INTEGER  ' a1 rook
    DIM WhiteRookHMoved AS INTEGER  ' h1 rook
    DIM BlackRookAMoved AS INTEGER  ' a8 rook
    DIM BlackRookHMoved AS INTEGER  ' h8 rook
    DIM EnPassantFile AS INTEGER    ' -1 if none, 0-7 for file
    DIM LastMoveFromRow AS INTEGER
    DIM LastMoveFromCol AS INTEGER
    DIM LastMoveToRow AS INTEGER
    DIM LastMoveToCol AS INTEGER

    SUB New()
        Me.Reset()
    END SUB

    SUB Reset()
        DIM r AS INTEGER
        DIM c AS INTEGER

        ' Clear the board
        FOR r = 0 TO 7
            FOR c = 0 TO 7
                Me.Squares(r, c) = EMPTY
            NEXT c
        NEXT r

        ' Set up white pieces (positive, row 0-1)
        Me.Squares(0, 0) = ROOK
        Me.Squares(0, 1) = KNIGHT
        Me.Squares(0, 2) = BISHOP
        Me.Squares(0, 3) = QUEEN
        Me.Squares(0, 4) = KING
        Me.Squares(0, 5) = BISHOP
        Me.Squares(0, 6) = KNIGHT
        Me.Squares(0, 7) = ROOK
        FOR c = 0 TO 7
            Me.Squares(1, c) = PAWN
        NEXT c

        ' Set up black pieces (negative, row 6-7)
        Me.Squares(7, 0) = 0 - ROOK
        Me.Squares(7, 1) = 0 - KNIGHT
        Me.Squares(7, 2) = 0 - BISHOP
        Me.Squares(7, 3) = 0 - QUEEN
        Me.Squares(7, 4) = 0 - KING
        Me.Squares(7, 5) = 0 - BISHOP
        Me.Squares(7, 6) = 0 - KNIGHT
        Me.Squares(7, 7) = 0 - ROOK
        FOR c = 0 TO 7
            Me.Squares(6, c) = 0 - PAWN
        NEXT c

        ' Reset castling flags
        Me.WhiteKingMoved = 0
        Me.BlackKingMoved = 0
        Me.WhiteRookAMoved = 0
        Me.WhiteRookHMoved = 0
        Me.BlackRookAMoved = 0
        Me.BlackRookHMoved = 0
        Me.EnPassantFile = -1
        Me.LastMoveFromRow = -1
        Me.LastMoveFromCol = -1
        Me.LastMoveToRow = -1
        Me.LastMoveToCol = -1
    END SUB

    ' Get piece at position
    FUNCTION GetPiece(row AS INTEGER, col AS INTEGER) AS INTEGER
        IF row < 0 OR row > 7 OR col < 0 OR col > 7 THEN
            GetPiece = EMPTY
        ELSE
            GetPiece = Me.Squares(row, col)
        END IF
    END FUNCTION

    ' Set piece at position
    SUB SetPiece(row AS INTEGER, col AS INTEGER, piece AS INTEGER)
        IF row >= 0 AND row <= 7 AND col >= 0 AND col <= 7 THEN
            Me.Squares(row, col) = piece
        END IF
    END SUB

    ' Find king position
    FUNCTION FindKingRow(isWhite AS INTEGER) AS INTEGER
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM target AS INTEGER

        IF isWhite = 1 THEN
            target = KING
        ELSE
            target = 0 - KING
        END IF

        FOR r = 0 TO 7
            FOR c = 0 TO 7
                IF Me.Squares(r, c) = target THEN
                    FindKingRow = r
                    EXIT FUNCTION
                END IF
            NEXT c
        NEXT r
        FindKingRow = -1
    END FUNCTION

    FUNCTION FindKingCol(isWhite AS INTEGER) AS INTEGER
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM target AS INTEGER

        IF isWhite = 1 THEN
            target = KING
        ELSE
            target = 0 - KING
        END IF

        FOR r = 0 TO 7
            FOR c = 0 TO 7
                IF Me.Squares(r, c) = target THEN
                    FindKingCol = c
                    EXIT FUNCTION
                END IF
            NEXT c
        NEXT r
        FindKingCol = -1
    END FUNCTION

    ' Draw the board with ANSI colors
    SUB Draw(selectedRow AS INTEGER, selectedCol AS INTEGER)
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM piece AS INTEGER
        DIM bgColor AS INTEGER
        DIM fgColor AS INTEGER
        DIM displayRow AS INTEGER
        DIM helper AS PieceHelper
        DIM sym AS STRING

        helper = NEW PieceHelper()

        ' Clear screen and position cursor
        COLOR CLR_WHITE, CLR_BLACK
        CLS

        ' Title
        LOCATE 1, 20
        COLOR CLR_BRIGHT_CYAN, CLR_BLACK
        PRINT "=== VIPER CHESS ==="

        ' Column labels
        LOCATE 3, 5
        COLOR CLR_BRIGHT_WHITE, CLR_BLACK
        PRINT "  a   b   c   d   e   f   g   h"

        ' Top border
        LOCATE 4, 4
        COLOR CLR_WHITE, CLR_BLACK
        PRINT CHR(218);  ' Top-left corner
        FOR c = 0 TO 7
            PRINT CHR(196); CHR(196); CHR(196);  ' Horizontal line
            IF c < 7 THEN PRINT CHR(194);  ' T-junction
        NEXT c
        PRINT CHR(191)  ' Top-right corner

        ' Draw rows (8 to 1, so row 7 at top)
        FOR r = 7 TO 0 STEP -1
            displayRow = 5 + (7 - r) * 2

            ' Row label and left border
            LOCATE displayRow, 2
            COLOR CLR_BRIGHT_WHITE, CLR_BLACK
            PRINT STR$(r + 1);
            LOCATE displayRow, 4
            COLOR CLR_WHITE, CLR_BLACK
            PRINT CHR(179);  ' Vertical line

            ' Draw each square
            FOR c = 0 TO 7
                ' Determine background color
                IF r = selectedRow AND c = selectedCol THEN
                    bgColor = SQ_HIGHLIGHT
                ELSEIF (r + c) MOD 2 = 1 THEN
                    bgColor = SQ_LIGHT
                ELSE
                    bgColor = SQ_DARK
                END IF

                ' Get piece and determine foreground color
                piece = Me.Squares(r, c)
                IF piece > 0 THEN
                    fgColor = CLR_BRIGHT_WHITE
                ELSEIF piece < 0 THEN
                    fgColor = CLR_BLACK
                ELSE
                    fgColor = bgColor
                END IF

                ' Draw the square
                COLOR fgColor, bgColor
                sym = helper.GetSymbol(piece)
                PRINT " "; sym; " ";

                ' Separator
                COLOR CLR_WHITE, CLR_BLACK
                PRINT CHR(179);
            NEXT c

            ' Row label on right
            COLOR CLR_BRIGHT_WHITE, CLR_BLACK
            PRINT " "; STR$(r + 1)

            ' Draw horizontal separator (except after last row)
            IF r > 0 THEN
                LOCATE displayRow + 1, 4
                COLOR CLR_WHITE, CLR_BLACK
                PRINT CHR(195);  ' Left T-junction
                FOR c = 0 TO 7
                    PRINT CHR(196); CHR(196); CHR(196);
                    IF c < 7 THEN PRINT CHR(197);  ' Cross
                NEXT c
                PRINT CHR(180)  ' Right T-junction
            END IF
        NEXT r

        ' Bottom border
        LOCATE 21, 4
        COLOR CLR_WHITE, CLR_BLACK
        PRINT CHR(192);  ' Bottom-left corner
        FOR c = 0 TO 7
            PRINT CHR(196); CHR(196); CHR(196);
            IF c < 7 THEN PRINT CHR(193);  ' Inverted T-junction
        NEXT c
        PRINT CHR(217)  ' Bottom-right corner

        ' Column labels at bottom
        LOCATE 22, 5
        COLOR CLR_BRIGHT_WHITE, CLR_BLACK
        PRINT "  a   b   c   d   e   f   g   h"

        ' Show last move if any
        IF Me.LastMoveFromRow >= 0 THEN
            LOCATE 3, 45
            COLOR CLR_BRIGHT_YELLOW, CLR_BLACK
            PRINT "Last move: ";
            PRINT CHR(97 + Me.LastMoveFromCol); STR$(Me.LastMoveFromRow + 1);
            PRINT " -> ";
            PRINT CHR(97 + Me.LastMoveToCol); STR$(Me.LastMoveToRow + 1)
        END IF

        COLOR CLR_WHITE, CLR_BLACK
    END SUB

    ' Make a move on the board
    SUB MakeMove(fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER)
        DIM piece AS INTEGER
        DIM ptype AS INTEGER
        DIM captured AS INTEGER

        piece = Me.Squares(fromRow, fromCol)
        ptype = piece
        IF ptype < 0 THEN ptype = 0 - ptype

        ' Handle en passant capture
        IF ptype = PAWN AND toCol = Me.EnPassantFile THEN
            IF piece > 0 AND fromRow = 4 AND toRow = 5 THEN
                Me.Squares(4, toCol) = EMPTY
            END IF
            IF piece < 0 AND fromRow = 3 AND toRow = 2 THEN
                Me.Squares(3, toCol) = EMPTY
            END IF
        END IF

        ' Reset en passant
        Me.EnPassantFile = -1

        ' Set en passant if pawn double move
        IF ptype = PAWN THEN
            IF fromRow = 1 AND toRow = 3 THEN
                Me.EnPassantFile = fromCol
            END IF
            IF fromRow = 6 AND toRow = 4 THEN
                Me.EnPassantFile = fromCol
            END IF
        END IF

        ' Handle castling
        IF ptype = KING THEN
            IF piece > 0 THEN Me.WhiteKingMoved = 1
            IF piece < 0 THEN Me.BlackKingMoved = 1

            ' Kingside castling
            IF fromCol = 4 AND toCol = 6 THEN
                Me.Squares(fromRow, 5) = Me.Squares(fromRow, 7)
                Me.Squares(fromRow, 7) = EMPTY
            END IF
            ' Queenside castling
            IF fromCol = 4 AND toCol = 2 THEN
                Me.Squares(fromRow, 3) = Me.Squares(fromRow, 0)
                Me.Squares(fromRow, 0) = EMPTY
            END IF
        END IF

        ' Track rook moves for castling
        IF ptype = ROOK THEN
            IF fromRow = 0 AND fromCol = 0 THEN Me.WhiteRookAMoved = 1
            IF fromRow = 0 AND fromCol = 7 THEN Me.WhiteRookHMoved = 1
            IF fromRow = 7 AND fromCol = 0 THEN Me.BlackRookAMoved = 1
            IF fromRow = 7 AND fromCol = 7 THEN Me.BlackRookHMoved = 1
        END IF

        ' Move the piece
        Me.Squares(toRow, toCol) = piece
        Me.Squares(fromRow, fromCol) = EMPTY

        ' Pawn promotion (auto-queen for now)
        IF ptype = PAWN THEN
            IF piece > 0 AND toRow = 7 THEN
                Me.Squares(toRow, toCol) = QUEEN
            END IF
            IF piece < 0 AND toRow = 0 THEN
                Me.Squares(toRow, toCol) = 0 - QUEEN
            END IF
        END IF

        ' Store last move
        Me.LastMoveFromRow = fromRow
        Me.LastMoveFromCol = fromCol
        Me.LastMoveToRow = toRow
        Me.LastMoveToCol = toCol
    END SUB
END CLASS
