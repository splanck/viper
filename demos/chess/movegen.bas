'==============================================================================
' CHESS MOVE GENERATION
' Generate all legal moves for current position
'==============================================================================

' Move storage structure (parallel arrays since List only holds objects)
DIM gMoves(255) AS INTEGER        ' Encoded move (from | to<<6 | flags<<12 | promo<<16)
DIM gMoveScores(255) AS INTEGER   ' Move scores for ordering
DIM gMoveCount AS INTEGER

' Encode a move using addition (works since fields don't overlap)
FUNCTION EncodeMove(fromSq AS INTEGER, toSq AS INTEGER, flags AS INTEGER, promo AS INTEGER) AS INTEGER
    EncodeMove = fromSq + (toSq * 64) + (flags * 4096) + (promo * 65536)
END FUNCTION

' Decode move components using MOD (no bitwise AND needed)
FUNCTION MoveFrom(m AS INTEGER) AS INTEGER
    MoveFrom = m MOD 64
END FUNCTION

FUNCTION MoveTo(m AS INTEGER) AS INTEGER
    MoveTo = (m \ 64) MOD 64
END FUNCTION

FUNCTION MoveFlags(m AS INTEGER) AS INTEGER
    MoveFlags = (m \ 4096) MOD 16
END FUNCTION

FUNCTION MovePromo(m AS INTEGER) AS INTEGER
    MovePromo = (m \ 65536) MOD 8
END FUNCTION

' Convert move to algebraic notation
FUNCTION MoveToAlg(m AS INTEGER) AS STRING
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM flags AS INTEGER
    DIM promo AS INTEGER
    DIM sb AS Viper.Text.StringBuilder

    sb = NEW Viper.Text.StringBuilder()
    fromSq = MoveFrom(m)
    toSq = MoveTo(m)
    flags = MoveFlags(m)
    promo = MovePromo(m)

    sb.Append(SquareToAlg(fromSq))
    sb.Append(SquareToAlg(toSq))

    IF promo > 0 THEN
        IF promo = 5 THEN       ' QUEEN
            sb.Append("q")
        ELSEIF promo = 4 THEN   ' ROOK
            sb.Append("r")
        ELSEIF promo = 3 THEN   ' BISHOP
            sb.Append("b")
        ELSEIF promo = 2 THEN   ' KNIGHT
            sb.Append("n")
        END IF
    END IF

    MoveToAlg = sb.ToString()
END FUNCTION

' Parse move from algebraic notation
FUNCTION AlgToMove(alg AS STRING) AS INTEGER
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM promo AS INTEGER
    DIM flags AS INTEGER
    DIM piece AS INTEGER
    DIM captured AS INTEGER

    IF LEN(alg) < 4 THEN
        AlgToMove = 0
        EXIT FUNCTION
    END IF

    fromSq = AlgToSquare(LEFT$(alg, 2))
    toSq = AlgToSquare(MID$(alg, 3, 2))

    IF fromSq < 0 OR toSq < 0 THEN
        AlgToMove = 0
        EXIT FUNCTION
    END IF

    promo = 0
    IF LEN(alg) >= 5 THEN
        DIM promoChar AS STRING
        promoChar = LCASE$(MID$(alg, 5, 1))
        IF promoChar = "q" THEN
            promo = QUEEN
        ELSEIF promoChar = "r" THEN
            promo = ROOK
        ELSEIF promoChar = "b" THEN
            promo = BISHOP
        ELSEIF promoChar = "n" THEN
            promo = KNIGHT
        END IF
    END IF

    ' Determine flags
    flags = MOVE_NORMAL
    piece = GetPiece(fromSq)
    captured = GetPiece(toSq)

    IF captured <> EMPTY THEN
        flags = MOVE_CAPTURE
    END IF

    IF piece = KING THEN
        IF fromSq = SQ(4, 0) AND toSq = SQ(6, 0) THEN flags = MOVE_CASTLE_KS
        IF fromSq = SQ(4, 0) AND toSq = SQ(2, 0) THEN flags = MOVE_CASTLE_QS
        IF fromSq = SQ(4, 7) AND toSq = SQ(6, 7) THEN flags = MOVE_CASTLE_KS
        IF fromSq = SQ(4, 7) AND toSq = SQ(2, 7) THEN flags = MOVE_CASTLE_QS
    END IF

    IF piece = PAWN THEN
        IF toSq = gEpSquare THEN
            flags = MOVE_EP
        ELSEIF ABS(RANK_OF(toSq) - RANK_OF(fromSq)) = 2 THEN
            flags = MOVE_DOUBLE_PAWN
        ELSEIF promo > 0 THEN
            flags = MOVE_PROMOTION
        END IF
    END IF

    AlgToMove = EncodeMove(fromSq, toSq, flags, promo)
END FUNCTION

' Add move to move list
SUB AddMove(fromSq AS INTEGER, toSq AS INTEGER, flags AS INTEGER, promo AS INTEGER)
    IF gMoveCount < MAX_MOVES THEN
        gMoves(gMoveCount) = EncodeMove(fromSq, toSq, flags, promo)
        gMoveScores(gMoveCount) = 0
        gMoveCount = gMoveCount + 1
    END IF
END SUB

' Check if a square is attacked by given side
FUNCTION IsAttacked(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM tf AS INTEGER
    DIM tr AS INTEGER
    DIM tSq AS INTEGER
    DIM df AS INTEGER
    DIM dr AS INTEGER
    DIM piece AS INTEGER
    DIM d AS INTEGER
    DIM i AS INTEGER

    f = FILE_OF(sq)
    r = RANK_OF(sq)

    ' Pawn attacks
    IF byColor = WHITE THEN
        IF r > 0 THEN
            IF f > 0 THEN
                tSq = SQ(f - 1, r - 1)
                IF GetPiece(tSq) = PAWN AND GetColor(tSq) = WHITE THEN
                    IsAttacked = 1
                    EXIT FUNCTION
                END IF
            END IF
            IF f < 7 THEN
                tSq = SQ(f + 1, r - 1)
                IF GetPiece(tSq) = PAWN AND GetColor(tSq) = WHITE THEN
                    IsAttacked = 1
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    ELSE
        IF r < 7 THEN
            IF f > 0 THEN
                tSq = SQ(f - 1, r + 1)
                IF GetPiece(tSq) = PAWN AND GetColor(tSq) = BLACK THEN
                    IsAttacked = 1
                    EXIT FUNCTION
                END IF
            END IF
            IF f < 7 THEN
                tSq = SQ(f + 1, r + 1)
                IF GetPiece(tSq) = PAWN AND GetColor(tSq) = BLACK THEN
                    IsAttacked = 1
                    EXIT FUNCTION
                END IF
            END IF
        END IF
    END IF

    ' Knight attacks
    FOR i = 0 TO 7
        SELECT CASE i
            CASE 0: df = 1: dr = 2
            CASE 1: df = 2: dr = 1
            CASE 2: df = 2: dr = -1
            CASE 3: df = 1: dr = -2
            CASE 4: df = -1: dr = -2
            CASE 5: df = -2: dr = -1
            CASE 6: df = -2: dr = 1
            CASE 7: df = -1: dr = 2
        END SELECT
        tf = f + df
        tr = r + dr
        IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
            tSq = SQ(tf, tr)
            IF GetPiece(tSq) = KNIGHT AND GetColor(tSq) = byColor THEN
                IsAttacked = 1
                EXIT FUNCTION
            END IF
        END IF
    NEXT i

    ' King attacks
    FOR dr = -1 TO 1
        FOR df = -1 TO 1
            IF df <> 0 OR dr <> 0 THEN
                tf = f + df
                tr = r + dr
                IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                    tSq = SQ(tf, tr)
                    IF GetPiece(tSq) = KING AND GetColor(tSq) = byColor THEN
                        IsAttacked = 1
                        EXIT FUNCTION
                    END IF
                END IF
            END IF
        NEXT df
    NEXT dr

    ' Rook/Queen attacks (straight lines)
    FOR d = 0 TO 3
        SELECT CASE d
            CASE 0: df = 0: dr = 1
            CASE 1: df = 1: dr = 0
            CASE 2: df = 0: dr = -1
            CASE 3: df = -1: dr = 0
        END SELECT
        tf = f + df
        tr = r + dr
        DO WHILE tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7
            tSq = SQ(tf, tr)
            piece = GetPiece(tSq)
            IF piece <> EMPTY THEN
                IF GetColor(tSq) = byColor THEN
                    IF piece = ROOK OR piece = QUEEN THEN
                        IsAttacked = 1
                        EXIT FUNCTION
                    END IF
                END IF
                EXIT DO
            END IF
            tf = tf + df
            tr = tr + dr
        LOOP
    NEXT d

    ' Bishop/Queen attacks (diagonal lines)
    FOR d = 0 TO 3
        SELECT CASE d
            CASE 0: df = 1: dr = 1
            CASE 1: df = 1: dr = -1
            CASE 2: df = -1: dr = -1
            CASE 3: df = -1: dr = 1
        END SELECT
        tf = f + df
        tr = r + dr
        DO WHILE tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7
            tSq = SQ(tf, tr)
            piece = GetPiece(tSq)
            IF piece <> EMPTY THEN
                IF GetColor(tSq) = byColor THEN
                    IF piece = BISHOP OR piece = QUEEN THEN
                        IsAttacked = 1
                        EXIT FUNCTION
                    END IF
                END IF
                EXIT DO
            END IF
            tf = tf + df
            tr = tr + dr
        LOOP
    NEXT d

    IsAttacked = 0
END FUNCTION

' Check if current side is in check
FUNCTION IsInCheck() AS INTEGER
    DIM kingPos AS INTEGER
    DIM enemy AS INTEGER

    kingPos = gKingSquare(gSideToMove)
    IF gSideToMove = WHITE THEN
        enemy = BLACK
    ELSE
        enemy = WHITE
    END IF

    IsInCheck = IsAttacked(kingPos, enemy)
END FUNCTION

' Generate pawn moves
SUB GenPawnMoves(sq AS INTEGER, color AS INTEGER)
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM dir AS INTEGER
    DIM startRank AS INTEGER
    DIM promoRank AS INTEGER
    DIM tSq AS INTEGER
    DIM p AS INTEGER

    f = FILE_OF(sq)
    r = RANK_OF(sq)

    IF color = WHITE THEN
        dir = 1
        startRank = 1
        promoRank = 7
    ELSE
        dir = -1
        startRank = 6
        promoRank = 0
    END IF

    ' Forward one
    tSq = SQ(f, r + dir)
    IF VALID_SQ(tSq) = 1 AND IsEmpty(tSq) = 1 THEN
        IF r + dir = promoRank THEN
            FOR p = QUEEN TO KNIGHT STEP -1
                AddMove(sq, tSq, MOVE_PROMOTION, p)
            NEXT p
        ELSE
            AddMove(sq, tSq, MOVE_NORMAL, 0)
        END IF

        ' Forward two from start
        IF r = startRank THEN
            tSq = SQ(f, r + dir * 2)
            IF IsEmpty(tSq) = 1 THEN
                AddMove(sq, tSq, MOVE_DOUBLE_PAWN, 0)
            END IF
        END IF
    END IF

    ' Captures
    IF f > 0 THEN
        tSq = SQ(f - 1, r + dir)
        IF VALID_SQ(tSq) = 1 THEN
            IF IsEnemy(tSq, color) = 1 THEN
                IF r + dir = promoRank THEN
                    FOR p = QUEEN TO KNIGHT STEP -1
                        AddMove(sq, tSq, MOVE_PROMOTION, p)
                    NEXT p
                ELSE
                    AddMove(sq, tSq, MOVE_CAPTURE, 0)
                END IF
            ELSEIF tSq = gEpSquare THEN
                AddMove(sq, tSq, MOVE_EP, 0)
            END IF
        END IF
    END IF

    IF f < 7 THEN
        tSq = SQ(f + 1, r + dir)
        IF VALID_SQ(tSq) = 1 THEN
            IF IsEnemy(tSq, color) = 1 THEN
                IF r + dir = promoRank THEN
                    FOR p = QUEEN TO KNIGHT STEP -1
                        AddMove(sq, tSq, MOVE_PROMOTION, p)
                    NEXT p
                ELSE
                    AddMove(sq, tSq, MOVE_CAPTURE, 0)
                END IF
            ELSEIF tSq = gEpSquare THEN
                AddMove(sq, tSq, MOVE_EP, 0)
            END IF
        END IF
    END IF
END SUB

' Generate knight moves
SUB GenKnightMoves(sq AS INTEGER, color AS INTEGER)
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM tf AS INTEGER
    DIM tr AS INTEGER
    DIM tSq AS INTEGER
    DIM df AS INTEGER
    DIM dr AS INTEGER
    DIM i AS INTEGER
    DIM flags AS INTEGER

    f = FILE_OF(sq)
    r = RANK_OF(sq)

    FOR i = 0 TO 7
        SELECT CASE i
            CASE 0: df = 1: dr = 2
            CASE 1: df = 2: dr = 1
            CASE 2: df = 2: dr = -1
            CASE 3: df = 1: dr = -2
            CASE 4: df = -1: dr = -2
            CASE 5: df = -2: dr = -1
            CASE 6: df = -2: dr = 1
            CASE 7: df = -1: dr = 2
        END SELECT

        tf = f + df
        tr = r + dr

        IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
            tSq = SQ(tf, tr)
            IF IsFriendly(tSq, color) = 0 THEN
                IF IsEnemy(tSq, color) = 1 THEN
                    flags = MOVE_CAPTURE
                ELSE
                    flags = MOVE_NORMAL
                END IF
                AddMove(sq, tSq, flags, 0)
            END IF
        END IF
    NEXT i
END SUB

' Generate sliding moves (bishop, rook, queen)
SUB GenSlidingMoves(sq AS INTEGER, color AS INTEGER, piece AS INTEGER)
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM tf AS INTEGER
    DIM tr AS INTEGER
    DIM tSq AS INTEGER
    DIM df AS INTEGER
    DIM dr AS INTEGER
    DIM d AS INTEGER
    DIM startDir AS INTEGER
    DIM endDir AS INTEGER
    DIM flags AS INTEGER

    f = FILE_OF(sq)
    r = RANK_OF(sq)

    ' Bishop: diagonals (dirs 4-7), Rook: straights (dirs 0-3), Queen: all
    IF piece = BISHOP THEN
        startDir = 4
        endDir = 7
    ELSEIF piece = ROOK THEN
        startDir = 0
        endDir = 3
    ELSE
        startDir = 0
        endDir = 7
    END IF

    FOR d = startDir TO endDir
        SELECT CASE d
            CASE 0: df = 0: dr = 1
            CASE 1: df = 1: dr = 0
            CASE 2: df = 0: dr = -1
            CASE 3: df = -1: dr = 0
            CASE 4: df = 1: dr = 1
            CASE 5: df = 1: dr = -1
            CASE 6: df = -1: dr = -1
            CASE 7: df = -1: dr = 1
        END SELECT

        tf = f + df
        tr = r + dr

        DO WHILE tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7
            tSq = SQ(tf, tr)

            IF IsFriendly(tSq, color) = 1 THEN
                EXIT DO
            END IF

            IF IsEnemy(tSq, color) = 1 THEN
                AddMove(sq, tSq, MOVE_CAPTURE, 0)
                EXIT DO
            END IF

            AddMove(sq, tSq, MOVE_NORMAL, 0)

            tf = tf + df
            tr = tr + dr
        LOOP
    NEXT d
END SUB

' Generate king moves
SUB GenKingMoves(sq AS INTEGER, color AS INTEGER)
    DIM f AS INTEGER
    DIM r AS INTEGER
    DIM tf AS INTEGER
    DIM tr AS INTEGER
    DIM tSq AS INTEGER
    DIM df AS INTEGER
    DIM dr AS INTEGER
    DIM flags AS INTEGER
    DIM enemy AS INTEGER

    f = FILE_OF(sq)
    r = RANK_OF(sq)

    IF color = WHITE THEN
        enemy = BLACK
    ELSE
        enemy = WHITE
    END IF

    ' Normal moves
    FOR dr = -1 TO 1
        FOR df = -1 TO 1
            IF df <> 0 OR dr <> 0 THEN
                tf = f + df
                tr = r + dr
                IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                    tSq = SQ(tf, tr)
                    IF IsFriendly(tSq, color) = 0 THEN
                        IF IsEnemy(tSq, color) = 1 THEN
                            flags = MOVE_CAPTURE
                        ELSE
                            flags = MOVE_NORMAL
                        END IF
                        AddMove(sq, tSq, flags, 0)
                    END IF
                END IF
            END IF
        NEXT df
    NEXT dr

    ' Castling
    IF color = WHITE THEN
        IF sq = SQ(4, 0) THEN
            ' Kingside
            IF HasFlag(gCastling, CASTLE_WK) = 1 THEN
                IF IsEmpty(SQ(5, 0)) = 1 THEN
                    IF IsEmpty(SQ(6, 0)) = 1 THEN
                        IF GetPiece(SQ(7, 0)) = ROOK THEN
                            IF GetColor(SQ(7, 0)) = WHITE THEN
                                IF IsAttacked(SQ(4, 0), enemy) = 0 THEN
                                    IF IsAttacked(SQ(5, 0), enemy) = 0 THEN
                                        IF IsAttacked(SQ(6, 0), enemy) = 0 THEN
                                            AddMove(sq, SQ(6, 0), MOVE_CASTLE_KS, 0)
                                        END IF
                                    END IF
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
            ' Queenside
            IF HasFlag(gCastling, CASTLE_WQ) = 1 THEN
                IF IsEmpty(SQ(3, 0)) = 1 THEN
                    IF IsEmpty(SQ(2, 0)) = 1 THEN
                        IF IsEmpty(SQ(1, 0)) = 1 THEN
                            IF GetPiece(SQ(0, 0)) = ROOK THEN
                                IF GetColor(SQ(0, 0)) = WHITE THEN
                                    IF IsAttacked(SQ(4, 0), enemy) = 0 THEN
                                        IF IsAttacked(SQ(3, 0), enemy) = 0 THEN
                                            IF IsAttacked(SQ(2, 0), enemy) = 0 THEN
                                                AddMove(sq, SQ(2, 0), MOVE_CASTLE_QS, 0)
                                            END IF
                                        END IF
                                    END IF
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        END IF
    ELSEIF color = BLACK THEN
        IF sq = SQ(4, 7) THEN
            ' Kingside
            IF HasFlag(gCastling, CASTLE_BK) = 1 THEN
                IF IsEmpty(SQ(5, 7)) = 1 THEN
                    IF IsEmpty(SQ(6, 7)) = 1 THEN
                        IF GetPiece(SQ(7, 7)) = ROOK THEN
                            IF GetColor(SQ(7, 7)) = BLACK THEN
                                IF IsAttacked(SQ(4, 7), enemy) = 0 THEN
                                    IF IsAttacked(SQ(5, 7), enemy) = 0 THEN
                                        IF IsAttacked(SQ(6, 7), enemy) = 0 THEN
                                            AddMove(sq, SQ(6, 7), MOVE_CASTLE_KS, 0)
                                        END IF
                                    END IF
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
            ' Queenside
            IF HasFlag(gCastling, CASTLE_BQ) = 1 THEN
                IF IsEmpty(SQ(3, 7)) = 1 THEN
                    IF IsEmpty(SQ(2, 7)) = 1 THEN
                        IF IsEmpty(SQ(1, 7)) = 1 THEN
                            IF GetPiece(SQ(0, 7)) = ROOK THEN
                                IF GetColor(SQ(0, 7)) = BLACK THEN
                                    IF IsAttacked(SQ(4, 7), enemy) = 0 THEN
                                        IF IsAttacked(SQ(3, 7), enemy) = 0 THEN
                                            IF IsAttacked(SQ(2, 7), enemy) = 0 THEN
                                                AddMove(sq, SQ(2, 7), MOVE_CASTLE_QS, 0)
                                            END IF
                                        END IF
                                    END IF
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        END IF
    END IF
END SUB

' Generate all pseudo-legal moves
SUB GenAllMoves()
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM color AS INTEGER

    gMoveCount = 0
    color = gSideToMove

    FOR sq = 0 TO 63
        IF gBoard(sq) <> 0 AND GetColor(sq) = color THEN
            piece = GetPiece(sq)
            IF piece = PAWN THEN
                GenPawnMoves(sq, color)
            ELSEIF piece = KNIGHT THEN
                GenKnightMoves(sq, color)
            ELSEIF piece = BISHOP THEN
                GenSlidingMoves(sq, color, BISHOP)
            ELSEIF piece = ROOK THEN
                GenSlidingMoves(sq, color, ROOK)
            ELSEIF piece = QUEEN THEN
                GenSlidingMoves(sq, color, QUEEN)
            ELSEIF piece = KING THEN
                GenKingMoves(sq, color)
            END IF
        END IF
    NEXT sq
END SUB

' Make a move on the board (returns data needed for unmake)
' Returns encoded undo info: captured | ep<<8 | castling<<16 | halfmove<<24
FUNCTION MakeMove(m AS INTEGER) AS INTEGER
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM flags AS INTEGER
    DIM promo AS INTEGER
    DIM piece AS INTEGER
    DIM color AS INTEGER
    DIM captured AS INTEGER
    DIM undoInfo AS INTEGER
    DIM rookFrom AS INTEGER
    DIM rookTo AS INTEGER
    DIM epCapSq AS INTEGER

    fromSq = MoveFrom(m)
    toSq = MoveTo(m)
    flags = MoveFlags(m)
    promo = MovePromo(m)
    piece = GetPiece(fromSq)
    color = GetColor(fromSq)
    captured = GetPiece(toSq)

    ' Store undo info (use addition since fields don't overlap)
    undoInfo = captured + (gEpSquare + 1) * 256 + gCastling * 65536 + gHalfMoveClock * 16777216

    ' Update half-move clock
    IF piece = PAWN THEN
        gHalfMoveClock = 0
    ELSEIF captured <> EMPTY THEN
        gHalfMoveClock = 0
    ELSE
        gHalfMoveClock = gHalfMoveClock + 1
    END IF

    ' Handle special moves
    IF flags = MOVE_CASTLE_KS THEN
        ' Move king
        ClearSquare(fromSq)
        SetPiece(toSq, KING, color)
        ' Move rook
        IF color = WHITE THEN
            rookFrom = SQ(7, 0)
            rookTo = SQ(5, 0)
        ELSE
            rookFrom = SQ(7, 7)
            rookTo = SQ(5, 7)
        END IF
        ClearSquare(rookFrom)
        SetPiece(rookTo, ROOK, color)

    ELSEIF flags = MOVE_CASTLE_QS THEN
        ' Move king
        ClearSquare(fromSq)
        SetPiece(toSq, KING, color)
        ' Move rook
        IF color = WHITE THEN
            rookFrom = SQ(0, 0)
            rookTo = SQ(3, 0)
        ELSE
            rookFrom = SQ(0, 7)
            rookTo = SQ(3, 7)
        END IF
        ClearSquare(rookFrom)
        SetPiece(rookTo, ROOK, color)

    ELSEIF flags = MOVE_EP THEN
        ' Move pawn
        ClearSquare(fromSq)
        SetPiece(toSq, PAWN, color)
        ' Remove captured pawn
        IF color = WHITE THEN
            epCapSq = toSq - 8
        ELSE
            epCapSq = toSq + 8
        END IF
        ClearSquare(epCapSq)

    ELSEIF flags = MOVE_PROMOTION THEN
        ClearSquare(fromSq)
        SetPiece(toSq, promo, color)

    ELSE
        ' Normal move or capture
        ClearSquare(fromSq)
        SetPiece(toSq, piece, color)
    END IF

    ' Update en passant square
    IF flags = MOVE_DOUBLE_PAWN THEN
        IF color = WHITE THEN
            gEpSquare = toSq - 8
        ELSE
            gEpSquare = toSq + 8
        END IF
    ELSE
        gEpSquare = -1
    END IF

    ' Update castling rights
    IF piece = KING THEN
        IF color = WHITE THEN
            gCastling = ClearFlag(ClearFlag(gCastling, CASTLE_WK), CASTLE_WQ)
        ELSE
            gCastling = ClearFlag(ClearFlag(gCastling, CASTLE_BK), CASTLE_BQ)
        END IF
    END IF

    IF piece = ROOK THEN
        IF fromSq = SQ(0, 0) THEN gCastling = ClearFlag(gCastling, CASTLE_WQ)
        IF fromSq = SQ(7, 0) THEN gCastling = ClearFlag(gCastling, CASTLE_WK)
        IF fromSq = SQ(0, 7) THEN gCastling = ClearFlag(gCastling, CASTLE_BQ)
        IF fromSq = SQ(7, 7) THEN gCastling = ClearFlag(gCastling, CASTLE_BK)
    END IF

    ' Rook captured
    IF toSq = SQ(0, 0) THEN gCastling = ClearFlag(gCastling, CASTLE_WQ)
    IF toSq = SQ(7, 0) THEN gCastling = ClearFlag(gCastling, CASTLE_WK)
    IF toSq = SQ(0, 7) THEN gCastling = ClearFlag(gCastling, CASTLE_BQ)
    IF toSq = SQ(7, 7) THEN gCastling = ClearFlag(gCastling, CASTLE_BK)

    ' Update full move number
    IF gSideToMove = BLACK THEN
        gFullMoveNumber = gFullMoveNumber + 1
    END IF

    ' Switch side to move
    IF gSideToMove = WHITE THEN
        gSideToMove = BLACK
    ELSE
        gSideToMove = WHITE
    END IF

    MakeMove = undoInfo
END FUNCTION

' Unmake a move
SUB UnmakeMove(m AS INTEGER, undoInfo AS INTEGER)
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM flags AS INTEGER
    DIM promo AS INTEGER
    DIM piece AS INTEGER
    DIM color AS INTEGER
    DIM captured AS INTEGER
    DIM rookFrom AS INTEGER
    DIM rookTo AS INTEGER
    DIM epCapSq AS INTEGER

    fromSq = MoveFrom(m)
    toSq = MoveTo(m)
    flags = MoveFlags(m)
    promo = MovePromo(m)
    captured = undoInfo MOD 256

    ' Switch side back
    IF gSideToMove = WHITE THEN
        gSideToMove = BLACK
    ELSE
        gSideToMove = WHITE
    END IF

    color = gSideToMove

    ' Restore state (use MOD instead of AND for masking)
    gEpSquare = ((undoInfo \ 256) MOD 256) - 1
    gCastling = (undoInfo \ 65536) MOD 256
    gHalfMoveClock = (undoInfo \ 16777216) MOD 256

    ' Undo full move number
    IF color = BLACK THEN
        gFullMoveNumber = gFullMoveNumber - 1
    END IF

    ' Get piece type (for promotions, it's in promo)
    IF flags = MOVE_PROMOTION THEN
        piece = PAWN
    ELSE
        piece = GetPiece(toSq)
    END IF

    ' Handle special moves
    SELECT CASE flags
        CASE MOVE_CASTLE_KS
            ' Move king back
            ClearSquare(toSq)
            SetPiece(fromSq, KING, color)
            ' Move rook back
            IF color = WHITE THEN
                rookFrom = SQ(7, 0)
                rookTo = SQ(5, 0)
            ELSE
                rookFrom = SQ(7, 7)
                rookTo = SQ(5, 7)
            END IF
            ClearSquare(rookTo)
            SetPiece(rookFrom, ROOK, color)

        CASE MOVE_CASTLE_QS
            ' Move king back
            ClearSquare(toSq)
            SetPiece(fromSq, KING, color)
            ' Move rook back
            IF color = WHITE THEN
                rookFrom = SQ(0, 0)
                rookTo = SQ(3, 0)
            ELSE
                rookFrom = SQ(0, 7)
                rookTo = SQ(3, 7)
            END IF
            ClearSquare(rookTo)
            SetPiece(rookFrom, ROOK, color)

        CASE MOVE_EP
            ' Move pawn back
            ClearSquare(toSq)
            SetPiece(fromSq, PAWN, color)
            ' Restore captured pawn
            IF color = WHITE THEN
                epCapSq = toSq - 8
                SetPiece(epCapSq, PAWN, BLACK)
            ELSE
                epCapSq = toSq + 8
                SetPiece(epCapSq, PAWN, WHITE)
            END IF

        CASE MOVE_PROMOTION
            ClearSquare(toSq)
            SetPiece(fromSq, PAWN, color)
            IF captured <> EMPTY THEN
                IF color = WHITE THEN
                    SetPiece(toSq, captured, BLACK)
                ELSE
                    SetPiece(toSq, captured, WHITE)
                END IF
            END IF

        CASE ELSE
            ' Normal move or capture
            ClearSquare(toSq)
            SetPiece(fromSq, piece, color)
            IF captured <> EMPTY THEN
                IF color = WHITE THEN
                    SetPiece(toSq, captured, BLACK)
                ELSE
                    SetPiece(toSq, captured, WHITE)
                END IF
            END IF
    END SELECT
END SUB

' Check if move is legal (doesn't leave king in check)
FUNCTION IsLegalMove(m AS INTEGER) AS INTEGER
    DIM undoInfo AS INTEGER
    DIM legal AS INTEGER

    undoInfo = MakeMove(m)

    ' Check if own king is in check after move (side has switched, so check opponent's king = our king)
    IF gSideToMove = WHITE THEN
        legal = NOT IsAttacked(gKingSquare(BLACK), WHITE)
    ELSE
        legal = NOT IsAttacked(gKingSquare(WHITE), BLACK)
    END IF

    UnmakeMove(m, undoInfo)

    IsLegalMove = legal
END FUNCTION

' Generate all legal moves
SUB GenLegalMoves()
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM m AS INTEGER

    GenAllMoves()

    ' Filter out illegal moves
    j = 0
    FOR i = 0 TO gMoveCount - 1
        m = gMoves(i)
        IF IsLegalMove(m) = 1 THEN
            gMoves(j) = m
            gMoveScores(j) = gMoveScores(i)
            j = j + 1
        END IF
    NEXT i
    gMoveCount = j
END SUB
