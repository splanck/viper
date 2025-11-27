REM ============================================================================
REM  CHESS AI - Minimax with alpha-beta pruning
REM ============================================================================

REM ============================================================================
REM  PIECE SQUARE VALUE
REM ============================================================================
FUNCTION GetPieceSquareValue(pType AS INTEGER, pColor AS INTEGER, row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM value AS INTEGER
    DIM r AS INTEGER

    value = 0

    REM Flip row for black pieces
    IF pColor = 1 THEN
        r = row
    ELSE
        r = 7 - row
    END IF

    REM Pawn position bonuses (simplified)
    IF pType = 1 THEN
        REM Center control and advancement bonus
        IF r >= 3 AND r <= 5 THEN
            IF col >= 2 AND col <= 5 THEN
                value = 20
            ELSE
                value = 10
            END IF
        END IF
        IF r = 6 THEN
            value = 50
        END IF
    END IF

    REM Knight position bonuses
    IF pType = 2 THEN
        REM Knights are better in center
        IF row >= 2 AND row <= 5 AND col >= 2 AND col <= 5 THEN
            value = 20
        END IF
        REM Penalty for edge
        IF col = 0 OR col = 7 OR row = 0 OR row = 7 THEN
            value = -30
        END IF
    END IF

    REM Bishop position
    IF pType = 3 THEN
        IF row >= 2 AND row <= 5 AND col >= 2 AND col <= 5 THEN
            value = 10
        END IF
    END IF

    REM King position (encourage castling side)
    IF pType = 6 THEN
        IF r <= 1 THEN
            IF col <= 2 OR col >= 6 THEN
                value = 20
            END IF
        ELSE IF r >= 2 THEN
            value = -20
        END IF
    END IF

    GetPieceSquareValue = value
END FUNCTION

REM ============================================================================
REM  EVALUATE POSITION
REM ============================================================================
FUNCTION Evaluate() AS INTEGER
    DIM score AS INTEGER
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM pieceValue AS INTEGER
    DIM posValue AS INTEGER

    score = 0

    FOR row = 0 TO 7
        FOR col = 0 TO 7
            piece = board(row, col)
            IF piece.GetType() > 0 THEN
                pieceValue = piece.GetValue()
                posValue = GetPieceSquareValue(piece.GetType(), piece.GetColor(), row, col)

                IF piece.GetColor() = 1 THEN
                    score = score + pieceValue + posValue
                ELSE
                    score = score - pieceValue - posValue
                END IF
            END IF
        NEXT col
    NEXT row

    Evaluate = score
END FUNCTION

REM ============================================================================
REM  MINIMAX WITH ALPHA-BETA
REM ============================================================================
FUNCTION Minimax(depth AS INTEGER, alpha AS INTEGER, beta AS INTEGER, maximizing AS INTEGER) AS INTEGER
    DIM moves AS Viper.Collections.List
    DIM move AS Move
    DIM i AS INTEGER
    DIM eval AS INTEGER
    DIM bestEval AS INTEGER
    DIM clr AS INTEGER

    nodesSearched = nodesSearched + 1

    IF maximizing = 1 THEN
        clr = 1
    ELSE
        clr = -1
    END IF

    moves = GenerateLegalMoves(clr)

    REM Terminal conditions
    IF moves.Count = 0 THEN
        IF IsInCheck(clr) = 1 THEN
            IF maximizing = 1 THEN
                Minimax = -100000 + (aiDepth - depth)
            ELSE
                Minimax = 100000 - (aiDepth - depth)
            END IF
        ELSE
            Minimax = 0
        END IF
        EXIT FUNCTION
    END IF

    IF depth = 0 THEN
        Minimax = Evaluate()
        EXIT FUNCTION
    END IF

    REM Save state for restoration
    DIM savedEnPassant AS INTEGER
    DIM savedWCK AS INTEGER
    DIM savedWCQ AS INTEGER
    DIM savedBCK AS INTEGER
    DIM savedBCQ AS INTEGER

    IF maximizing = 1 THEN
        bestEval = -200000

        FOR i = 0 TO moves.Count - 1
            move = moves.get_Item(i)

            savedEnPassant = enPassantCol
            savedWCK = whiteCanCastleKing
            savedWCQ = whiteCanCastleQueen
            savedBCK = blackCanCastleKing
            savedBCQ = blackCanCastleQueen

            MakeMove(move)
            eval = Minimax(depth - 1, alpha, beta, 0)
            UnmakeMove(move)

            enPassantCol = savedEnPassant
            whiteCanCastleKing = savedWCK
            whiteCanCastleQueen = savedWCQ
            blackCanCastleKing = savedBCK
            blackCanCastleQueen = savedBCQ

            IF eval > bestEval THEN
                bestEval = eval
            END IF

            IF eval > alpha THEN
                alpha = eval
            END IF

            IF beta <= alpha THEN
                EXIT FOR
            END IF
        NEXT i

        Minimax = bestEval
    ELSE
        bestEval = 200000

        FOR i = 0 TO moves.Count - 1
            move = moves.get_Item(i)

            savedEnPassant = enPassantCol
            savedWCK = whiteCanCastleKing
            savedWCQ = whiteCanCastleQueen
            savedBCK = blackCanCastleKing
            savedBCQ = blackCanCastleQueen

            MakeMove(move)
            eval = Minimax(depth - 1, alpha, beta, 1)
            UnmakeMove(move)

            enPassantCol = savedEnPassant
            whiteCanCastleKing = savedWCK
            whiteCanCastleQueen = savedWCQ
            blackCanCastleKing = savedBCK
            blackCanCastleQueen = savedBCQ

            IF eval < bestEval THEN
                bestEval = eval
            END IF

            IF eval < beta THEN
                beta = eval
            END IF

            IF beta <= alpha THEN
                EXIT FOR
            END IF
        NEXT i

        Minimax = bestEval
    END IF
END FUNCTION

REM ============================================================================
REM  GET BEST MOVE
REM ============================================================================
FUNCTION GetBestMove(clr AS INTEGER) AS Move
    DIM moves AS Viper.Collections.List
    DIM bestMove AS Move
    DIM move AS Move
    DIM bestScore AS INTEGER
    DIM score AS INTEGER
    DIM i AS INTEGER
    DIM maximizing AS INTEGER

    moves = GenerateLegalMoves(clr)

    IF moves.Count = 0 THEN
        GetBestMove = NEW Move()
        EXIT FUNCTION
    END IF

    IF clr = 1 THEN
        maximizing = 1
        bestScore = -200000
    ELSE
        maximizing = 0
        bestScore = 200000
    END IF

    bestMove = moves.get_Item(0)
    nodesSearched = 0

    PRINT "AI thinking (depth "; aiDepth; ")..."

    REM Save state
    DIM savedEnPassant AS INTEGER
    DIM savedWCK AS INTEGER
    DIM savedWCQ AS INTEGER
    DIM savedBCK AS INTEGER
    DIM savedBCQ AS INTEGER

    FOR i = 0 TO moves.Count - 1
        move = moves.get_Item(i)

        savedEnPassant = enPassantCol
        savedWCK = whiteCanCastleKing
        savedWCQ = whiteCanCastleQueen
        savedBCK = blackCanCastleKing
        savedBCQ = blackCanCastleQueen

        MakeMove(move)

        IF maximizing = 1 THEN
            score = Minimax(aiDepth - 1, -200000, 200000, 0)
        ELSE
            score = Minimax(aiDepth - 1, -200000, 200000, 1)
        END IF

        UnmakeMove(move)

        enPassantCol = savedEnPassant
        whiteCanCastleKing = savedWCK
        whiteCanCastleQueen = savedWCQ
        blackCanCastleKing = savedBCK
        blackCanCastleQueen = savedBCQ

        IF maximizing = 1 THEN
            IF score > bestScore THEN
                bestScore = score
                bestMove = move
            ELSE IF score = bestScore THEN
                IF RND() > 0.5 THEN
                    bestMove = move
                END IF
            END IF
        ELSE
            IF score < bestScore THEN
                bestScore = score
                bestMove = move
            ELSE IF score = bestScore THEN
                IF RND() > 0.5 THEN
                    bestMove = move
                END IF
            END IF
        END IF
    NEXT i

    PRINT "Searched "; nodesSearched; " positions"

    GetBestMove = bestMove
END FUNCTION
