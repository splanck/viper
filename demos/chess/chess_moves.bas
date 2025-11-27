REM ============================================================================
REM  CHESS MOVES - Move generation and validation
REM ============================================================================

REM ============================================================================
REM  CHECK IF SQUARE IS ATTACKED
REM ============================================================================
FUNCTION IsSquareAttacked(row AS INTEGER, col AS INTEGER, byColor AS INTEGER) AS INTEGER
    DIM r AS INTEGER
    DIM c AS INTEGER
    DIM dr AS INTEGER
    DIM dc AS INTEGER
    DIM piece AS Piece
    DIM pType AS INTEGER
    DIM i AS INTEGER
    DIM attacked AS INTEGER

    attacked = 0

    REM Check pawn attacks
    IF byColor = 1 AND attacked = 0 THEN
        IF IsValidSquare(row - 1, col - 1) = 1 THEN
            piece = board(row - 1, col - 1)
            IF piece.GetType() = 1 AND piece.GetColor() = 1 THEN
                attacked = 1
            END IF
        END IF
        IF attacked = 0 AND IsValidSquare(row - 1, col + 1) = 1 THEN
            piece = board(row - 1, col + 1)
            IF piece.GetType() = 1 AND piece.GetColor() = 1 THEN
                attacked = 1
            END IF
        END IF
    ELSE IF byColor = -1 AND attacked = 0 THEN
        IF IsValidSquare(row + 1, col - 1) = 1 THEN
            piece = board(row + 1, col - 1)
            IF piece.GetType() = 1 AND piece.GetColor() = -1 THEN
                attacked = 1
            END IF
        END IF
        IF attacked = 0 AND IsValidSquare(row + 1, col + 1) = 1 THEN
            piece = board(row + 1, col + 1)
            IF piece.GetType() = 1 AND piece.GetColor() = -1 THEN
                attacked = 1
            END IF
        END IF
    END IF

    REM Check knight attacks
    IF attacked = 0 THEN
        DIM knightDr(8) AS INTEGER
        DIM knightDc(8) AS INTEGER
        knightDr(0) = -2 : knightDc(0) = -1
        knightDr(1) = -2 : knightDc(1) = 1
        knightDr(2) = -1 : knightDc(2) = -2
        knightDr(3) = -1 : knightDc(3) = 2
        knightDr(4) = 1 : knightDc(4) = -2
        knightDr(5) = 1 : knightDc(5) = 2
        knightDr(6) = 2 : knightDc(6) = -1
        knightDr(7) = 2 : knightDc(7) = 1

        i = 0
        WHILE i <= 7 AND attacked = 0
            r = row + knightDr(i)
            c = col + knightDc(i)
            IF IsValidSquare(r, c) = 1 THEN
                piece = board(r, c)
                IF piece.GetType() = 2 AND piece.GetColor() = byColor THEN
                    attacked = 1
                END IF
            END IF
            i = i + 1
        WEND
    END IF

    REM Check king attacks
    IF attacked = 0 THEN
        dr = -1
        WHILE dr <= 1 AND attacked = 0
            dc = -1
            WHILE dc <= 1 AND attacked = 0
                IF dr <> 0 OR dc <> 0 THEN
                    r = row + dr
                    c = col + dc
                    IF IsValidSquare(r, c) = 1 THEN
                        piece = board(r, c)
                        IF piece.GetType() = 6 AND piece.GetColor() = byColor THEN
                            attacked = 1
                        END IF
                    END IF
                END IF
                dc = dc + 1
            WEND
            dr = dr + 1
        WEND
    END IF

    REM Check rook/queen attacks (straight lines)
    IF attacked = 0 THEN
        DIM straightDr(4) AS INTEGER
        DIM straightDc(4) AS INTEGER
        straightDr(0) = 0 : straightDc(0) = 1
        straightDr(1) = 0 : straightDc(1) = -1
        straightDr(2) = 1 : straightDc(2) = 0
        straightDr(3) = -1 : straightDc(3) = 0

        i = 0
        WHILE i <= 3 AND attacked = 0
            dr = straightDr(i)
            dc = straightDc(i)
            r = row + dr
            c = col + dc

            DIM blocked AS INTEGER
            blocked = 0
            WHILE IsValidSquare(r, c) = 1 AND attacked = 0 AND blocked = 0
                piece = board(r, c)
                IF piece.GetType() > 0 THEN
                    IF piece.GetColor() = byColor THEN
                        pType = piece.GetType()
                        IF pType = 4 OR pType = 5 THEN
                            attacked = 1
                        END IF
                    END IF
                    blocked = 1
                END IF
                r = r + dr
                c = c + dc
            WEND
            i = i + 1
        WEND
    END IF

    REM Check bishop/queen attacks (diagonals)
    IF attacked = 0 THEN
        DIM diagDr(4) AS INTEGER
        DIM diagDc(4) AS INTEGER
        diagDr(0) = 1 : diagDc(0) = 1
        diagDr(1) = 1 : diagDc(1) = -1
        diagDr(2) = -1 : diagDc(2) = 1
        diagDr(3) = -1 : diagDc(3) = -1

        i = 0
        WHILE i <= 3 AND attacked = 0
            dr = diagDr(i)
            dc = diagDc(i)
            r = row + dr
            c = col + dc

            DIM diagBlocked AS INTEGER
            diagBlocked = 0
            WHILE IsValidSquare(r, c) = 1 AND attacked = 0 AND diagBlocked = 0
                piece = board(r, c)
                IF piece.GetType() > 0 THEN
                    IF piece.GetColor() = byColor THEN
                        pType = piece.GetType()
                        IF pType = 3 OR pType = 5 THEN
                            attacked = 1
                        END IF
                    END IF
                    diagBlocked = 1
                END IF
                r = r + dr
                c = c + dc
            WEND
            i = i + 1
        WEND
    END IF

    IsSquareAttacked = attacked
END FUNCTION

REM ============================================================================
REM  CHECK IF KING IS IN CHECK
REM ============================================================================
FUNCTION IsInCheck(clr AS INTEGER) AS INTEGER
    DIM enemyColor AS INTEGER

    REM FindKing sets foundKingRow and foundKingCol globals
    FindKing(clr)

    IF foundKingRow < 0 THEN
        IsInCheck = 0
    ELSE
        IF clr = 1 THEN
            enemyColor = -1
        ELSE
            enemyColor = 1
        END IF

        IsInCheck = IsSquareAttacked(foundKingRow, foundKingCol, enemyColor)
    END IF
END FUNCTION

REM ============================================================================
REM  GENERATE MOVES FOR A PIECE
REM ============================================================================
SUB GeneratePieceMoves(row AS INTEGER, col AS INTEGER, moves AS OBJECT)
    DIM piece AS Piece
    DIM pType AS INTEGER
    DIM pColor AS INTEGER
    DIM r AS INTEGER
    DIM c AS INTEGER
    DIM dr AS INTEGER
    DIM dc AS INTEGER
    DIM move AS Move
    DIM targetPiece AS Piece
    DIM i AS INTEGER

    piece = board(row, col)
    pType = piece.GetType()
    pColor = piece.GetColor()

    IF pType = 0 THEN
        EXIT SUB
    END IF

    REM Pawn moves
    IF pType = 1 THEN
        DIM direction AS INTEGER
        DIM startRow AS INTEGER
        DIM promotionRow AS INTEGER

        IF pColor = 1 THEN
            direction = 1
            startRow = 1
            promotionRow = 7
        ELSE
            direction = -1
            startRow = 6
            promotionRow = 0
        END IF

        REM Forward move
        r = row + direction
        IF IsValidSquare(r, col) = 1 AND IsEmptySquare(r, col) = 1 THEN
            move = NEW Move()
            move.Init(row, col, r, col)

            IF r = promotionRow THEN
                move.SetPromotion(5)
            END IF

            moves.Add(move)

            REM Double move from start
            IF row = startRow THEN
                r = row + direction * 2
                IF IsEmptySquare(r, col) = 1 THEN
                    move = NEW Move()
                    move.Init(row, col, r, col)
                    move.SetFlag(5)
                    moves.Add(move)
                END IF
            END IF
        END IF

        REM Captures
        DIM capCol AS INTEGER
        FOR capCol = col - 1 TO col + 1 STEP 2
            r = row + direction
            IF IsValidSquare(r, capCol) = 1 THEN
                IF IsEnemyPiece(r, capCol, pColor) = 1 THEN
                    targetPiece = board(r, capCol)
                    move = NEW Move()
                    move.Init(row, col, r, capCol)
                    move.SetCapture(targetPiece.GetType(), targetPiece.GetColor())

                    IF r = promotionRow THEN
                        move.SetPromotion(5)
                    END IF

                    moves.Add(move)
                END IF

                REM En passant
                IF enPassantCol = capCol AND r = row + direction THEN
                    IF pColor = 1 AND row = 4 THEN
                        move = NEW Move()
                        move.Init(row, col, r, capCol)
                        move.SetCapture(1, -1)
                        move.SetFlag(3)
                        moves.Add(move)
                    ELSE IF pColor = -1 AND row = 3 THEN
                        move = NEW Move()
                        move.Init(row, col, r, capCol)
                        move.SetCapture(1, 1)
                        move.SetFlag(3)
                        moves.Add(move)
                    END IF
                END IF
            END IF
        NEXT capCol
    END IF

    REM Knight moves
    IF pType = 2 THEN
        DIM knightDr(8) AS INTEGER
        DIM knightDc(8) AS INTEGER
        knightDr(0) = -2 : knightDc(0) = -1
        knightDr(1) = -2 : knightDc(1) = 1
        knightDr(2) = -1 : knightDc(2) = -2
        knightDr(3) = -1 : knightDc(3) = 2
        knightDr(4) = 1 : knightDc(4) = -2
        knightDr(5) = 1 : knightDc(5) = 2
        knightDr(6) = 2 : knightDc(6) = -1
        knightDr(7) = 2 : knightDc(7) = 1

        FOR i = 0 TO 7
            r = row + knightDr(i)
            c = col + knightDc(i)

            IF IsValidSquare(r, c) = 1 THEN
                IF IsFriendlyPiece(r, c, pColor) = 0 THEN
                    move = NEW Move()
                    move.Init(row, col, r, c)

                    IF IsEnemyPiece(r, c, pColor) = 1 THEN
                        targetPiece = board(r, c)
                        move.SetCapture(targetPiece.GetType(), targetPiece.GetColor())
                    END IF

                    moves.Add(move)
                END IF
            END IF
        NEXT i
    END IF

    REM Bishop moves (diagonals)
    IF pType = 3 OR pType = 5 THEN
        DIM diagDr(4) AS INTEGER
        DIM diagDc(4) AS INTEGER
        diagDr(0) = 1 : diagDc(0) = 1
        diagDr(1) = 1 : diagDc(1) = -1
        diagDr(2) = -1 : diagDc(2) = 1
        diagDr(3) = -1 : diagDc(3) = -1

        DIM d AS INTEGER
        FOR d = 0 TO 3
            dr = diagDr(d)
            dc = diagDc(d)
            r = row + dr
            c = col + dc

            DIM diagDone AS INTEGER
            diagDone = 0
            WHILE IsValidSquare(r, c) = 1 AND diagDone = 0
                IF IsFriendlyPiece(r, c, pColor) = 1 THEN
                    diagDone = 1
                ELSE
                    move = NEW Move()
                    move.Init(row, col, r, c)

                    IF IsEnemyPiece(r, c, pColor) = 1 THEN
                        targetPiece = board(r, c)
                        move.SetCapture(targetPiece.GetType(), targetPiece.GetColor())
                        moves.Add(move)
                        diagDone = 1
                    ELSE
                        moves.Add(move)
                        r = r + dr
                        c = c + dc
                    END IF
                END IF
            WEND
        NEXT d
    END IF

    REM Rook moves (straight lines)
    IF pType = 4 OR pType = 5 THEN
        DIM straightDr(4) AS INTEGER
        DIM straightDc(4) AS INTEGER
        straightDr(0) = 0 : straightDc(0) = 1
        straightDr(1) = 0 : straightDc(1) = -1
        straightDr(2) = 1 : straightDc(2) = 0
        straightDr(3) = -1 : straightDc(3) = 0

        DIM s AS INTEGER
        FOR s = 0 TO 3
            dr = straightDr(s)
            dc = straightDc(s)
            r = row + dr
            c = col + dc

            DIM straightDone AS INTEGER
            straightDone = 0
            WHILE IsValidSquare(r, c) = 1 AND straightDone = 0
                IF IsFriendlyPiece(r, c, pColor) = 1 THEN
                    straightDone = 1
                ELSE
                    move = NEW Move()
                    move.Init(row, col, r, c)

                    IF IsEnemyPiece(r, c, pColor) = 1 THEN
                        targetPiece = board(r, c)
                        move.SetCapture(targetPiece.GetType(), targetPiece.GetColor())
                        moves.Add(move)
                        straightDone = 1
                    ELSE
                        moves.Add(move)
                        r = r + dr
                        c = c + dc
                    END IF
                END IF
            WEND
        NEXT s
    END IF

    REM King moves
    IF pType = 6 THEN
        FOR dr = -1 TO 1
            FOR dc = -1 TO 1
                IF dr <> 0 OR dc <> 0 THEN
                    r = row + dr
                    c = col + dc

                    IF IsValidSquare(r, c) = 1 THEN
                        IF IsFriendlyPiece(r, c, pColor) = 0 THEN
                            move = NEW Move()
                            move.Init(row, col, r, c)

                            IF IsEnemyPiece(r, c, pColor) = 1 THEN
                                targetPiece = board(r, c)
                                move.SetCapture(targetPiece.GetType(), targetPiece.GetColor())
                            END IF

                            moves.Add(move)
                        END IF
                    END IF
                END IF
            NEXT dc
        NEXT dr

        REM Castling
        DIM enemyColor AS INTEGER
        IF pColor = 1 THEN
            enemyColor = -1
        ELSE
            enemyColor = 1
        END IF

        IF piece.GetMoved() = 0 AND IsInCheck(pColor) = 0 THEN
            REM Kingside castling
            DIM canCastleKing AS INTEGER
            IF pColor = 1 THEN
                canCastleKing = whiteCanCastleKing
            ELSE
                canCastleKing = blackCanCastleKing
            END IF

            IF canCastleKing = 1 THEN
                IF IsEmptySquare(row, 5) = 1 AND IsEmptySquare(row, 6) = 1 THEN
                    IF IsSquareAttacked(row, 5, enemyColor) = 0 AND IsSquareAttacked(row, 6, enemyColor) = 0 THEN
                        move = NEW Move()
                        move.Init(row, col, row, 6)
                        move.SetFlag(1)
                        moves.Add(move)
                    END IF
                END IF
            END IF

            REM Queenside castling
            DIM canCastleQueen AS INTEGER
            IF pColor = 1 THEN
                canCastleQueen = whiteCanCastleQueen
            ELSE
                canCastleQueen = blackCanCastleQueen
            END IF

            IF canCastleQueen = 1 THEN
                IF IsEmptySquare(row, 1) = 1 AND IsEmptySquare(row, 2) = 1 AND IsEmptySquare(row, 3) = 1 THEN
                    IF IsSquareAttacked(row, 2, enemyColor) = 0 AND IsSquareAttacked(row, 3, enemyColor) = 0 THEN
                        move = NEW Move()
                        move.Init(row, col, row, 2)
                        move.SetFlag(2)
                        moves.Add(move)
                    END IF
                END IF
            END IF
        END IF
    END IF
END SUB

REM ============================================================================
REM  GENERATE ALL LEGAL MOVES
REM ============================================================================
FUNCTION GenerateLegalMoves(clr AS INTEGER) AS Viper.Collections.List
    DIM moves AS Viper.Collections.List
    DIM legalMoves AS Viper.Collections.List
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM move AS Move
    DIM i AS INTEGER

    moves = NEW Viper.Collections.List()
    legalMoves = NEW Viper.Collections.List()

    REM Generate pseudo-legal moves
    FOR row = 0 TO 7
        FOR col = 0 TO 7
            piece = board(row, col)
            IF piece.GetType() > 0 AND piece.GetColor() = clr THEN
                GeneratePieceMoves(row, col, moves)
            END IF
        NEXT col
    NEXT row

    REM Filter to legal moves (don't leave king in check)
    FOR i = 0 TO moves.Count - 1
        move = moves.get_Item(i)

        REM Make move
        MakeMove(move)

        REM Check if king is in check
        IF IsInCheck(clr) = 0 THEN
            legalMoves.Add(move)
        END IF

        REM Unmake move
        UnmakeMove(move)
    NEXT i

    GenerateLegalMoves = legalMoves
END FUNCTION

REM ============================================================================
REM  MAKE MOVE
REM ============================================================================
SUB MakeMove(move AS Move)
    DIM fromRow AS INTEGER
    DIM fromCol AS INTEGER
    DIM toRow AS INTEGER
    DIM toCol AS INTEGER
    DIM piece AS Piece
    DIM flag AS INTEGER

    fromRow = move.GetFromRow()
    fromCol = move.GetFromCol()
    toRow = move.GetToRow()
    toCol = move.GetToCol()
    flag = move.GetFlag()

    piece = board(fromRow, fromCol)

    REM Handle special moves
    IF flag = 1 THEN
        REM Kingside castle
        board(toRow, toCol) = piece
        board(fromRow, fromCol) = NEW Piece()
        board(fromRow, fromCol).Init(0, 0)

        DIM rook AS Piece
        rook = board(toRow, 7)
        board(toRow, 5) = rook
        board(toRow, 7) = NEW Piece()
        board(toRow, 7).Init(0, 0)
        rook.SetMoved()
    ELSE IF flag = 2 THEN
        REM Queenside castle
        board(toRow, toCol) = piece
        board(fromRow, fromCol) = NEW Piece()
        board(fromRow, fromCol).Init(0, 0)

        DIM qRook AS Piece
        qRook = board(toRow, 0)
        board(toRow, 3) = qRook
        board(toRow, 0) = NEW Piece()
        board(toRow, 0).Init(0, 0)
        qRook.SetMoved()
    ELSE IF flag = 3 THEN
        REM En passant
        board(toRow, toCol) = piece
        board(fromRow, fromCol) = NEW Piece()
        board(fromRow, fromCol).Init(0, 0)
        board(fromRow, toCol) = NEW Piece()
        board(fromRow, toCol).Init(0, 0)
    ELSE IF flag = 4 THEN
        REM Promotion
        DIM promotedPiece AS Piece
        promotedPiece = NEW Piece()
        promotedPiece.Init(move.GetPromotionType(), piece.GetColor())
        promotedPiece.SetMoved()

        board(toRow, toCol) = promotedPiece
        board(fromRow, fromCol) = NEW Piece()
        board(fromRow, fromCol).Init(0, 0)
    ELSE
        REM Normal move
        board(toRow, toCol) = piece
        board(fromRow, fromCol) = NEW Piece()
        board(fromRow, fromCol).Init(0, 0)
    END IF

    piece.SetMoved()

    REM Update castling rights
    IF piece.GetType() = 6 THEN
        IF piece.GetColor() = 1 THEN
            whiteCanCastleKing = 0
            whiteCanCastleQueen = 0
        ELSE
            blackCanCastleKing = 0
            blackCanCastleQueen = 0
        END IF
    END IF

    IF piece.GetType() = 4 THEN
        IF piece.GetColor() = 1 THEN
            IF fromCol = 0 THEN
                whiteCanCastleQueen = 0
            ELSE IF fromCol = 7 THEN
                whiteCanCastleKing = 0
            END IF
        ELSE
            IF fromCol = 0 THEN
                blackCanCastleQueen = 0
            ELSE IF fromCol = 7 THEN
                blackCanCastleKing = 0
            END IF
        END IF
    END IF

    REM Update en passant square
    IF flag = 5 THEN
        enPassantCol = toCol
    ELSE
        enPassantCol = -1
    END IF
END SUB

REM ============================================================================
REM  UNMAKE MOVE
REM ============================================================================
SUB UnmakeMove(move AS Move)
    DIM fromRow AS INTEGER
    DIM fromCol AS INTEGER
    DIM toRow AS INTEGER
    DIM toCol AS INTEGER
    DIM piece AS Piece
    DIM flag AS INTEGER

    fromRow = move.GetFromRow()
    fromCol = move.GetFromCol()
    toRow = move.GetToRow()
    toCol = move.GetToCol()
    flag = move.GetFlag()

    IF flag = 4 THEN
        REM Promotion - restore pawn
        DIM pawn AS Piece
        pawn = NEW Piece()
        pawn.Init(1, board(toRow, toCol).GetColor())
        board(fromRow, fromCol) = pawn
    ELSE
        piece = board(toRow, toCol)
        board(fromRow, fromCol) = piece
    END IF

    REM Restore captured piece or empty square
    IF move.GetCapturedType() > 0 AND flag <> 3 THEN
        DIM captured AS Piece
        captured = NEW Piece()
        captured.Init(move.GetCapturedType(), move.GetCapturedColor())
        board(toRow, toCol) = captured
    ELSE
        board(toRow, toCol) = NEW Piece()
        board(toRow, toCol).Init(0, 0)
    END IF

    REM Handle special moves
    IF flag = 1 THEN
        REM Undo kingside castle
        DIM kRook AS Piece
        kRook = board(toRow, 5)
        board(toRow, 7) = kRook
        board(toRow, 5) = NEW Piece()
        board(toRow, 5).Init(0, 0)
    ELSE IF flag = 2 THEN
        REM Undo queenside castle
        DIM qRook AS Piece
        qRook = board(toRow, 3)
        board(toRow, 0) = qRook
        board(toRow, 3) = NEW Piece()
        board(toRow, 3).Init(0, 0)
    ELSE IF flag = 3 THEN
        REM Restore en passant captured pawn
        DIM epPawn AS Piece
        epPawn = NEW Piece()
        epPawn.Init(1, move.GetCapturedColor())
        board(fromRow, toCol) = epPawn
    END IF
END SUB
