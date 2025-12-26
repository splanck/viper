' moves.bas - Move validation for chess
' Checks legal moves, check detection, etc.
' Note: pieces.bas and board.bas are available via AddFile concatenation

CLASS MoveValidator
    ' Check if a square is attacked by the opponent
    FUNCTION IsSquareAttacked(b AS Board, row AS INTEGER, col AS INTEGER, byWhite AS INTEGER) AS INTEGER
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM piece AS INTEGER
        DIM ptype AS INTEGER
        DIM dr AS INTEGER
        DIM dc AS INTEGER
        DIM skipPiece AS INTEGER

        IsSquareAttacked = 0

        ' Check all squares for attacking pieces
        FOR r = 0 TO 7
            FOR c = 0 TO 7
                piece = b.GetPiece(r, c)
                skipPiece = 0

                IF piece = EMPTY THEN skipPiece = 1

                ' Check if piece belongs to attacker
                IF skipPiece = 0 THEN
                    IF byWhite = 1 AND piece < 0 THEN skipPiece = 1
                    IF byWhite = 0 AND piece > 0 THEN skipPiece = 1
                END IF

                IF skipPiece = 0 THEN
                    ptype = piece
                    IF ptype < 0 THEN ptype = 0 - ptype

                    ' Pawn attacks
                    IF ptype = PAWN THEN
                        IF piece > 0 THEN  ' White pawn attacks up-diagonally
                            IF r + 1 = row AND (c - 1 = col OR c + 1 = col) THEN
                                IsSquareAttacked = 1
                                EXIT FUNCTION
                            END IF
                        ELSE  ' Black pawn attacks down-diagonally
                            IF r - 1 = row AND (c - 1 = col OR c + 1 = col) THEN
                                IsSquareAttacked = 1
                                EXIT FUNCTION
                            END IF
                        END IF
                    END IF

                    ' Knight attacks
                    IF ptype = KNIGHT THEN
                        dr = row - r
                        dc = col - c
                        IF dr < 0 THEN dr = 0 - dr
                        IF dc < 0 THEN dc = 0 - dc
                        IF (dr = 2 AND dc = 1) OR (dr = 1 AND dc = 2) THEN
                            IsSquareAttacked = 1
                            EXIT FUNCTION
                        END IF
                    END IF

                    ' King attacks (one square in any direction)
                    IF ptype = KING THEN
                        dr = row - r
                        dc = col - c
                        IF dr < 0 THEN dr = 0 - dr
                        IF dc < 0 THEN dc = 0 - dc
                        IF dr <= 1 AND dc <= 1 AND (dr + dc > 0) THEN
                            IsSquareAttacked = 1
                            EXIT FUNCTION
                        END IF
                    END IF

                    ' Rook/Queen attacks (straight lines)
                    IF ptype = ROOK OR ptype = QUEEN THEN
                        IF r = row OR c = col THEN
                            IF Me.IsPathClear(b, r, c, row, col) = 1 THEN
                                IsSquareAttacked = 1
                                EXIT FUNCTION
                            END IF
                        END IF
                    END IF

                    ' Bishop/Queen attacks (diagonals)
                    IF ptype = BISHOP OR ptype = QUEEN THEN
                        dr = row - r
                        dc = col - c
                        IF dr < 0 THEN dr = 0 - dr
                        IF dc < 0 THEN dc = 0 - dc
                        IF dr = dc AND dr > 0 THEN
                            IF Me.IsPathClear(b, r, c, row, col) = 1 THEN
                                IsSquareAttacked = 1
                                EXIT FUNCTION
                            END IF
                        END IF
                    END IF
                END IF
            NEXT c
        NEXT r
    END FUNCTION

    ' Check if path between two squares is clear
    FUNCTION IsPathClear(b AS Board, fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER) AS INTEGER
        DIM dr AS INTEGER
        DIM dc AS INTEGER
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM steps AS INTEGER
        DIM i AS INTEGER

        IsPathClear = 1

        ' Calculate direction
        dr = 0
        dc = 0
        IF toRow > fromRow THEN dr = 1
        IF toRow < fromRow THEN dr = -1
        IF toCol > fromCol THEN dc = 1
        IF toCol < fromCol THEN dc = -1

        ' Count steps
        steps = toRow - fromRow
        IF steps < 0 THEN steps = 0 - steps
        IF toCol - fromCol > steps THEN steps = toCol - fromCol
        IF fromCol - toCol > steps THEN steps = fromCol - toCol

        ' Check each square along the path (excluding start and end)
        r = fromRow + dr
        c = fromCol + dc
        FOR i = 1 TO steps - 1
            IF b.GetPiece(r, c) <> EMPTY THEN
                IsPathClear = 0
                EXIT FUNCTION
            END IF
            r = r + dr
            c = c + dc
        NEXT i
    END FUNCTION

    ' Check if the current player is in check
    FUNCTION IsInCheck(b AS Board, isWhite AS INTEGER) AS INTEGER
        DIM kingRow AS INTEGER
        DIM kingCol AS INTEGER

        kingRow = b.FindKingRow(isWhite)
        kingCol = b.FindKingCol(isWhite)

        IF kingRow < 0 THEN
            IsInCheck = 0
            EXIT FUNCTION
        END IF

        ' Check if king is attacked by opponent
        IF isWhite = 1 THEN
            IsInCheck = Me.IsSquareAttacked(b, kingRow, kingCol, 0)
        ELSE
            IsInCheck = Me.IsSquareAttacked(b, kingRow, kingCol, 1)
        END IF
    END FUNCTION

    ' Validate a move (basic validation, doesn't check for check)
    FUNCTION IsValidMove(b AS Board, fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER, isWhiteTurn AS INTEGER) AS INTEGER
        DIM piece AS INTEGER
        DIM ptype AS INTEGER
        DIM target AS INTEGER
        DIM dr AS INTEGER
        DIM dc AS INTEGER
        DIM absDr AS INTEGER
        DIM absDc AS INTEGER

        IsValidMove = 0

        ' Basic bounds check
        IF fromRow < 0 OR fromRow > 7 THEN EXIT FUNCTION
        IF fromCol < 0 OR fromCol > 7 THEN EXIT FUNCTION
        IF toRow < 0 OR toRow > 7 THEN EXIT FUNCTION
        IF toCol < 0 OR toCol > 7 THEN EXIT FUNCTION

        ' Can't move to same square
        IF fromRow = toRow AND fromCol = toCol THEN EXIT FUNCTION

        piece = b.GetPiece(fromRow, fromCol)
        target = b.GetPiece(toRow, toCol)

        ' Must have a piece to move
        IF piece = EMPTY THEN EXIT FUNCTION

        ' Must be correct color
        IF isWhiteTurn = 1 AND piece < 0 THEN EXIT FUNCTION
        IF isWhiteTurn = 0 AND piece > 0 THEN EXIT FUNCTION

        ' Can't capture own piece
        IF target <> EMPTY THEN
            IF piece > 0 AND target > 0 THEN EXIT FUNCTION
            IF piece < 0 AND target < 0 THEN EXIT FUNCTION
        END IF

        ptype = piece
        IF ptype < 0 THEN ptype = 0 - ptype

        dr = toRow - fromRow
        dc = toCol - fromCol
        absDr = dr
        absDc = dc
        IF absDr < 0 THEN absDr = 0 - absDr
        IF absDc < 0 THEN absDc = 0 - absDc

        ' Validate by piece type
        IF ptype = PAWN THEN
            IF piece > 0 THEN  ' White pawn
                ' Forward move
                IF dc = 0 AND target = EMPTY THEN
                    IF dr = 1 THEN IsValidMove = 1
                    IF dr = 2 AND fromRow = 1 AND b.GetPiece(2, fromCol) = EMPTY THEN IsValidMove = 1
                END IF
                ' Capture
                IF dr = 1 AND absDc = 1 THEN
                    IF target < 0 THEN IsValidMove = 1
                    ' En passant
                    IF toCol = b.EnPassantFile AND fromRow = 4 THEN IsValidMove = 1
                END IF
            ELSE  ' Black pawn
                IF dc = 0 AND target = EMPTY THEN
                    IF dr = -1 THEN IsValidMove = 1
                    IF dr = -2 AND fromRow = 6 AND b.GetPiece(5, fromCol) = EMPTY THEN IsValidMove = 1
                END IF
                IF dr = -1 AND absDc = 1 THEN
                    IF target > 0 THEN IsValidMove = 1
                    IF toCol = b.EnPassantFile AND fromRow = 3 THEN IsValidMove = 1
                END IF
            END IF
        END IF

        IF ptype = KNIGHT THEN
            IF (absDr = 2 AND absDc = 1) OR (absDr = 1 AND absDc = 2) THEN
                IsValidMove = 1
            END IF
        END IF

        IF ptype = BISHOP THEN
            IF absDr = absDc AND absDr > 0 THEN
                IF Me.IsPathClear(b, fromRow, fromCol, toRow, toCol) = 1 THEN
                    IsValidMove = 1
                END IF
            END IF
        END IF

        IF ptype = ROOK THEN
            IF (fromRow = toRow OR fromCol = toCol) AND (absDr + absDc > 0) THEN
                IF Me.IsPathClear(b, fromRow, fromCol, toRow, toCol) = 1 THEN
                    IsValidMove = 1
                END IF
            END IF
        END IF

        IF ptype = QUEEN THEN
            IF fromRow = toRow OR fromCol = toCol OR absDr = absDc THEN
                IF Me.IsPathClear(b, fromRow, fromCol, toRow, toCol) = 1 THEN
                    IsValidMove = 1
                END IF
            END IF
        END IF

        IF ptype = KING THEN
            IF absDr <= 1 AND absDc <= 1 THEN
                IsValidMove = 1
            END IF
            ' Castling
            IF fromCol = 4 AND absDr = 0 AND absDc = 2 THEN
                IF piece > 0 AND b.WhiteKingMoved = 0 THEN
                    IF toCol = 6 AND b.WhiteRookHMoved = 0 THEN
                        IF b.GetPiece(0, 5) = EMPTY AND b.GetPiece(0, 6) = EMPTY THEN
                            IF Me.IsInCheck(b, 1) = 0 THEN
                                IF Me.IsSquareAttacked(b, 0, 5, 0) = 0 THEN
                                    IsValidMove = 1
                                END IF
                            END IF
                        END IF
                    END IF
                    IF toCol = 2 AND b.WhiteRookAMoved = 0 THEN
                        IF b.GetPiece(0, 1) = EMPTY AND b.GetPiece(0, 2) = EMPTY AND b.GetPiece(0, 3) = EMPTY THEN
                            IF Me.IsInCheck(b, 1) = 0 THEN
                                IF Me.IsSquareAttacked(b, 0, 3, 0) = 0 THEN
                                    IsValidMove = 1
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
                IF piece < 0 AND b.BlackKingMoved = 0 THEN
                    IF toCol = 6 AND b.BlackRookHMoved = 0 THEN
                        IF b.GetPiece(7, 5) = EMPTY AND b.GetPiece(7, 6) = EMPTY THEN
                            IF Me.IsInCheck(b, 0) = 0 THEN
                                IF Me.IsSquareAttacked(b, 7, 5, 1) = 0 THEN
                                    IsValidMove = 1
                                END IF
                            END IF
                        END IF
                    END IF
                    IF toCol = 2 AND b.BlackRookAMoved = 0 THEN
                        IF b.GetPiece(7, 1) = EMPTY AND b.GetPiece(7, 2) = EMPTY AND b.GetPiece(7, 3) = EMPTY THEN
                            IF Me.IsInCheck(b, 0) = 0 THEN
                                IF Me.IsSquareAttacked(b, 7, 3, 1) = 0 THEN
                                    IsValidMove = 1
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        END IF
    END FUNCTION

    ' Check if a move leaves the king in check
    FUNCTION LeavesKingInCheck(b AS Board, fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER, isWhiteTurn AS INTEGER) AS INTEGER
        DIM savedFrom AS INTEGER
        DIM savedTo AS INTEGER
        DIM result AS INTEGER

        ' Save the pieces
        savedFrom = b.GetPiece(fromRow, fromCol)
        savedTo = b.GetPiece(toRow, toCol)

        ' Make the move temporarily
        b.SetPiece(toRow, toCol, savedFrom)
        b.SetPiece(fromRow, fromCol, EMPTY)

        ' Check if in check
        result = Me.IsInCheck(b, isWhiteTurn)

        ' Restore the pieces
        b.SetPiece(fromRow, fromCol, savedFrom)
        b.SetPiece(toRow, toCol, savedTo)

        LeavesKingInCheck = result
    END FUNCTION

    ' Full move validation including check
    FUNCTION IsLegalMove(b AS Board, fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER, isWhiteTurn AS INTEGER) AS INTEGER
        IsLegalMove = 0

        ' First check basic validity
        IF Me.IsValidMove(b, fromRow, fromCol, toRow, toCol, isWhiteTurn) = 0 THEN
            EXIT FUNCTION
        END IF

        ' Then check it doesn't leave king in check
        IF Me.LeavesKingInCheck(b, fromRow, fromCol, toRow, toCol, isWhiteTurn) = 1 THEN
            EXIT FUNCTION
        END IF

        IsLegalMove = 1
    END FUNCTION

    ' Check if player has any legal moves
    FUNCTION HasLegalMoves(b AS Board, isWhiteTurn AS INTEGER) AS INTEGER
        DIM fromRow AS INTEGER
        DIM fromCol AS INTEGER
        DIM toRow AS INTEGER
        DIM toCol AS INTEGER
        DIM piece AS INTEGER

        DIM skipFrom AS INTEGER

        HasLegalMoves = 0

        FOR fromRow = 0 TO 7
            FOR fromCol = 0 TO 7
                piece = b.GetPiece(fromRow, fromCol)
                skipFrom = 0

                IF piece = EMPTY THEN skipFrom = 1

                ' Check correct color
                IF skipFrom = 0 THEN
                    IF isWhiteTurn = 1 AND piece < 0 THEN skipFrom = 1
                    IF isWhiteTurn = 0 AND piece > 0 THEN skipFrom = 1
                END IF

                IF skipFrom = 0 THEN
                    ' Try all destination squares
                    FOR toRow = 0 TO 7
                        FOR toCol = 0 TO 7
                            IF Me.IsLegalMove(b, fromRow, fromCol, toRow, toCol, isWhiteTurn) = 1 THEN
                                HasLegalMoves = 1
                                EXIT FUNCTION
                            END IF
                        NEXT toCol
                    NEXT toRow
                END IF
            NEXT fromCol
        NEXT fromRow
    END FUNCTION
END CLASS
