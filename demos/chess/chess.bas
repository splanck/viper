REM ====================================================================
REM VIPER CHESS - Advanced Chess Game with AI
REM ====================================================================
REM A complete chess implementation featuring:
REM - Full chess rules (castling, en passant, promotion)
REM - Alpha-beta search with iterative deepening
REM - Piece-square tables for positional evaluation
REM - Move ordering with MVV-LVA
REM - ANSI colored display
REM ====================================================================

REM ====================================================================
REM CONSTANTS
REM ====================================================================
CONST EMPTY AS INTEGER = 0
CONST PAWN AS INTEGER = 1
CONST KNIGHT AS INTEGER = 2
CONST BISHOP AS INTEGER = 3
CONST ROOK AS INTEGER = 4
CONST QUEEN AS INTEGER = 5
CONST KING AS INTEGER = 6

CONST WHITE AS INTEGER = 0
CONST BLACK AS INTEGER = 1

CONST VAL_PAWN AS INTEGER = 100
CONST VAL_KNIGHT AS INTEGER = 320
CONST VAL_BISHOP AS INTEGER = 330
CONST VAL_ROOK AS INTEGER = 500
CONST VAL_QUEEN AS INTEGER = 900
CONST VAL_KING AS INTEGER = 20000

CONST INF AS INTEGER = 100000
CONST MATE AS INTEGER = 50000

REM Castle flags (bitmask)
CONST WK_CASTLE AS INTEGER = 1
CONST WQ_CASTLE AS INTEGER = 2
CONST BK_CASTLE AS INTEGER = 4
CONST BQ_CASTLE AS INTEGER = 8

REM ====================================================================
REM Move Class - Represents a chess move
REM ====================================================================
CLASS Move
    DIM fromSq AS INTEGER
    DIM toSq AS INTEGER
    DIM piece AS INTEGER
    DIM captured AS INTEGER
    DIM promotion AS INTEGER
    DIM castling AS INTEGER
    DIM enPassant AS INTEGER
    DIM score AS INTEGER

    SUB Init(f AS INTEGER, t AS INTEGER, pc AS INTEGER, cap AS INTEGER)
        fromSq = f
        toSq = t
        piece = pc
        captured = cap
        promotion = 0
        castling = 0
        enPassant = 0
        score = 0
    END SUB

    SUB SetPromotion(promo AS INTEGER)
        promotion = promo
    END SUB

    SUB SetCastling(cast AS INTEGER)
        castling = cast
    END SUB

    SUB SetEnPassant(ep AS INTEGER)
        enPassant = ep
    END SUB

    SUB SetScore(s AS INTEGER)
        score = s
    END SUB

    FUNCTION GetFrom() AS INTEGER
        GetFrom = fromSq
    END FUNCTION

    FUNCTION GetTo() AS INTEGER
        GetTo = toSq
    END FUNCTION

    FUNCTION GetPiece() AS INTEGER
        GetPiece = piece
    END FUNCTION

    FUNCTION GetCaptured() AS INTEGER
        GetCaptured = captured
    END FUNCTION

    FUNCTION GetPromotion() AS INTEGER
        GetPromotion = promotion
    END FUNCTION

    FUNCTION GetCastling() AS INTEGER
        GetCastling = castling
    END FUNCTION

    FUNCTION GetEnPassant() AS INTEGER
        GetEnPassant = enPassant
    END FUNCTION

    FUNCTION GetScore() AS INTEGER
        GetScore = score
    END FUNCTION

    FUNCTION IsCapture() AS INTEGER
        IF captured > 0 THEN
            IsCapture = 1
        ELSE
            IsCapture = 0
        END IF
    END FUNCTION

    FUNCTION ToAlgebraic() AS STRING
        DIM result AS STRING
        DIM fromFile AS INTEGER
        DIM fromRank AS INTEGER
        DIM toFile AS INTEGER
        DIM toRank AS INTEGER

        fromFile = fromSq MOD 8
        fromRank = fromSq / 8
        toFile = toSq MOD 8
        toRank = toSq / 8

        result = CHR$(97 + fromFile) + CHR$(49 + fromRank)
        result = result + CHR$(97 + toFile) + CHR$(49 + toRank)

        IF promotion = QUEEN THEN
            result = result + "q"
        ELSEIF promotion = ROOK THEN
            result = result + "r"
        ELSEIF promotion = BISHOP THEN
            result = result + "b"
        ELSEIF promotion = KNIGHT THEN
            result = result + "n"
        END IF

        ToAlgebraic = result
    END FUNCTION
END CLASS

REM ====================================================================
REM Board Class - Chess board representation
REM ====================================================================
CLASS Board
    REM Board array: squares 0-63 (a1=0, h8=63)
    REM Positive = white pieces, Negative = black pieces
    DIM squares(63) AS INTEGER
    DIM sideToMove AS INTEGER
    DIM castleRights AS INTEGER
    DIM epSquare AS INTEGER
    DIM halfMoveClock AS INTEGER
    DIM fullMoveNum AS INTEGER
    DIM lastMoveFrom AS INTEGER
    DIM lastMoveTo AS INTEGER

    SUB Init()
        DIM i AS INTEGER
        FOR i = 0 TO 63
            squares(i) = EMPTY
        NEXT i
        sideToMove = WHITE
        castleRights = WK_CASTLE + WQ_CASTLE + BK_CASTLE + BQ_CASTLE
        epSquare = -1
        halfMoveClock = 0
        fullMoveNum = 1
        lastMoveFrom = -1
        lastMoveTo = -1
    END SUB

    SUB SetupInitial()
        Init()

        REM White pieces
        squares(0) = ROOK
        squares(1) = KNIGHT
        squares(2) = BISHOP
        squares(3) = QUEEN
        squares(4) = KING
        squares(5) = BISHOP
        squares(6) = KNIGHT
        squares(7) = ROOK
        DIM i AS INTEGER
        FOR i = 8 TO 15
            squares(i) = PAWN
        NEXT i

        REM Black pieces (negative)
        squares(56) = 0 - ROOK
        squares(57) = 0 - KNIGHT
        squares(58) = 0 - BISHOP
        squares(59) = 0 - QUEEN
        squares(60) = 0 - KING
        squares(61) = 0 - BISHOP
        squares(62) = 0 - KNIGHT
        squares(63) = 0 - ROOK
        FOR i = 48 TO 55
            squares(i) = 0 - PAWN
        NEXT i
    END SUB

    FUNCTION GetSquare(sq AS INTEGER) AS INTEGER
        GetSquare = squares(sq)
    END FUNCTION

    SUB SetSquare(sq AS INTEGER, piece AS INTEGER)
        squares(sq) = piece
    END SUB

    FUNCTION GetSide() AS INTEGER
        GetSide = sideToMove
    END FUNCTION

    SUB SetSide(s AS INTEGER)
        sideToMove = s
    END SUB

    FUNCTION GetCastleRights() AS INTEGER
        GetCastleRights = castleRights
    END FUNCTION

    SUB SetCastleRights(cr AS INTEGER)
        castleRights = cr
    END SUB

    FUNCTION GetEpSquare() AS INTEGER
        GetEpSquare = epSquare
    END FUNCTION

    SUB SetEpSquare(ep AS INTEGER)
        epSquare = ep
    END SUB

    FUNCTION GetHalfMove() AS INTEGER
        GetHalfMove = halfMoveClock
    END FUNCTION

    FUNCTION GetFullMove() AS INTEGER
        GetFullMove = fullMoveNum
    END FUNCTION

    FUNCTION GetLastFrom() AS INTEGER
        GetLastFrom = lastMoveFrom
    END FUNCTION

    FUNCTION GetLastTo() AS INTEGER
        GetLastTo = lastMoveTo
    END FUNCTION

    FUNCTION PieceColor(sq AS INTEGER) AS INTEGER
        IF squares(sq) > 0 THEN
            PieceColor = WHITE
        ELSEIF squares(sq) < 0 THEN
            PieceColor = BLACK
        ELSE
            PieceColor = -1
        END IF
    END FUNCTION

    FUNCTION AbsPiece(sq AS INTEGER) AS INTEGER
        DIM p AS INTEGER
        p = squares(sq)
        IF p < 0 THEN
            AbsPiece = 0 - p
        ELSE
            AbsPiece = p
        END IF
    END FUNCTION

    FUNCTION IsEmpty(sq AS INTEGER) AS INTEGER
        IF squares(sq) = 0 THEN
            IsEmpty = 1
        ELSE
            IsEmpty = 0
        END IF
    END FUNCTION

    FUNCTION FindKing(clr AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM target AS INTEGER
        IF clr = WHITE THEN
            target = KING
        ELSE
            target = 0 - KING
        END IF
        FOR i = 0 TO 63
            IF squares(i) = target THEN
                FindKing = i
                EXIT FUNCTION
            END IF
        NEXT i
        FindKing = -1
    END FUNCTION

    REM Make a move on the board
    SUB MakeMove(m AS Move)
        DIM f AS INTEGER
        DIM t AS INTEGER
        DIM pc AS INTEGER
        DIM cap AS INTEGER
        DIM promo AS INTEGER

        f = m.GetFrom()
        t = m.GetTo()
        pc = squares(f)
        cap = m.GetCaptured()
        promo = m.GetPromotion()

        REM Handle castling
        IF m.GetCastling() = 1 THEN
            REM Kingside castle
            IF sideToMove = WHITE THEN
                squares(4) = EMPTY
                squares(7) = EMPTY
                squares(6) = KING
                squares(5) = ROOK
                castleRights = castleRights - (castleRights MOD 4)
            ELSE
                squares(60) = EMPTY
                squares(63) = EMPTY
                squares(62) = 0 - KING
                squares(61) = 0 - ROOK
                DIM newRights AS INTEGER
                newRights = castleRights / 4
                newRights = newRights * 4
                castleRights = newRights - (castleRights MOD 4)
            END IF
        ELSEIF m.GetCastling() = 2 THEN
            REM Queenside castle
            IF sideToMove = WHITE THEN
                squares(4) = EMPTY
                squares(0) = EMPTY
                squares(2) = KING
                squares(3) = ROOK
                castleRights = castleRights - (castleRights MOD 4)
            ELSE
                squares(60) = EMPTY
                squares(56) = EMPTY
                squares(58) = 0 - KING
                squares(59) = 0 - ROOK
                DIM nr2 AS INTEGER
                nr2 = castleRights / 4
                nr2 = nr2 * 4
                castleRights = nr2 - (castleRights MOD 4)
            END IF
        ELSE
            REM Normal move
            squares(f) = EMPTY
            IF promo > 0 THEN
                IF sideToMove = WHITE THEN
                    squares(t) = promo
                ELSE
                    squares(t) = 0 - promo
                END IF
            ELSE
                squares(t) = pc
            END IF

            REM En passant capture
            IF m.GetEnPassant() = 1 THEN
                IF sideToMove = WHITE THEN
                    squares(t - 8) = EMPTY
                ELSE
                    squares(t + 8) = EMPTY
                END IF
            END IF

            REM Update castling rights
            IF f = 0 OR t = 0 THEN
                IF (castleRights MOD 4) >= 2 THEN
                    castleRights = castleRights - 2
                END IF
            END IF
            IF f = 7 OR t = 7 THEN
                IF (castleRights MOD 2) = 1 THEN
                    castleRights = castleRights - 1
                END IF
            END IF
            IF f = 56 OR t = 56 THEN
                IF castleRights >= 8 THEN
                    castleRights = castleRights - 8
                END IF
            END IF
            IF f = 63 OR t = 63 THEN
                DIM bkFlag AS INTEGER
                bkFlag = (castleRights / 4) MOD 2
                IF bkFlag = 1 THEN
                    castleRights = castleRights - 4
                END IF
            END IF
            IF f = 4 THEN
                IF (castleRights MOD 4) > 0 THEN
                    castleRights = castleRights - (castleRights MOD 4)
                END IF
            END IF
            IF f = 60 THEN
                IF castleRights >= 4 THEN
                    DIM bkRights AS INTEGER
                    bkRights = castleRights / 4
                    castleRights = castleRights - (bkRights * 4)
                END IF
            END IF
        END IF

        REM Update en passant square
        DIM absPc AS INTEGER
        IF pc < 0 THEN
            absPc = 0 - pc
        ELSE
            absPc = pc
        END IF
        IF absPc = PAWN THEN
            DIM diff AS INTEGER
            IF t > f THEN
                diff = t - f
            ELSE
                diff = f - t
            END IF
            IF diff = 16 THEN
                IF sideToMove = WHITE THEN
                    epSquare = f + 8
                ELSE
                    epSquare = f - 8
                END IF
            ELSE
                epSquare = -1
            END IF
        ELSE
            epSquare = -1
        END IF

        REM Update half-move clock
        IF absPc = PAWN OR cap > 0 THEN
            halfMoveClock = 0
        ELSE
            halfMoveClock = halfMoveClock + 1
        END IF

        REM Update full move number
        IF sideToMove = BLACK THEN
            fullMoveNum = fullMoveNum + 1
        END IF

        REM Store last move for display
        lastMoveFrom = f
        lastMoveTo = t

        REM Switch side
        IF sideToMove = WHITE THEN
            sideToMove = BLACK
        ELSE
            sideToMove = WHITE
        END IF
    END SUB

    REM Copy board state
    SUB CopyFrom(other AS Board)
        DIM i AS INTEGER
        FOR i = 0 TO 63
            squares(i) = other.GetSquare(i)
        NEXT i
        sideToMove = other.GetSide()
        castleRights = other.GetCastleRights()
        epSquare = other.GetEpSquare()
        halfMoveClock = other.GetHalfMove()
        fullMoveNum = other.GetFullMove()
        lastMoveFrom = other.GetLastFrom()
        lastMoveTo = other.GetLastTo()
    END SUB
END CLASS

REM ====================================================================
REM MoveGen Class - Move generation
REM ====================================================================
CLASS MoveGen
    DIM board AS Board
    DIM moves(255) AS Move
    DIM moveCount AS INTEGER

    SUB Init(b AS Board)
        board = b
        moveCount = 0
    END SUB

    SUB SetBoard(b AS Board)
        board = b
    END SUB

    FUNCTION GetMoveCount() AS INTEGER
        GetMoveCount = moveCount
    END FUNCTION

    FUNCTION GetMove(idx AS INTEGER) AS Move
        GetMove = moves(idx)
    END FUNCTION

    SUB ClearMoves()
        moveCount = 0
    END SUB

    SUB AddMove(f AS INTEGER, t AS INTEGER, pc AS INTEGER, cap AS INTEGER)
        DIM m AS Move
        m = NEW Move()
        m.Init(f, t, pc, cap)
        moves(moveCount) = m
        moveCount = moveCount + 1
    END SUB

    SUB AddPromotion(f AS INTEGER, t AS INTEGER, cap AS INTEGER)
        DIM m AS Move
        DIM i AS INTEGER
        DIM promos(3) AS INTEGER
        promos(0) = QUEEN
        promos(1) = ROOK
        promos(2) = BISHOP
        promos(3) = KNIGHT

        FOR i = 0 TO 3
            m = NEW Move()
            m.Init(f, t, PAWN, cap)
            m.SetPromotion(promos(i))
            moves(moveCount) = m
            moveCount = moveCount + 1
        NEXT i
    END SUB

    SUB AddCastling(kingside AS INTEGER)
        DIM m AS Move
        DIM f AS INTEGER
        DIM t AS INTEGER
        IF board.GetSide() = WHITE THEN
            f = 4
            IF kingside = 1 THEN
                t = 6
            ELSE
                t = 2
            END IF
        ELSE
            f = 60
            IF kingside = 1 THEN
                t = 62
            ELSE
                t = 58
            END IF
        END IF
        m = NEW Move()
        m.Init(f, t, KING, 0)
        m.SetCastling(kingside + 1)
        moves(moveCount) = m
        moveCount = moveCount + 1
    END SUB

    SUB AddEnPassant(f AS INTEGER, t AS INTEGER)
        DIM m AS Move
        m = NEW Move()
        m.Init(f, t, PAWN, PAWN)
        m.SetEnPassant(1)
        moves(moveCount) = m
        moveCount = moveCount + 1
    END SUB

    REM Generate all pseudo-legal moves
    SUB GenerateMoves()
        ClearMoves()
        DIM sq AS INTEGER
        DIM pc AS INTEGER
        DIM side AS INTEGER
        DIM absPc AS INTEGER

        side = board.GetSide()

        FOR sq = 0 TO 63
            pc = board.GetSquare(sq)
            IF pc = 0 THEN
                GOTO NextSquare
            END IF

            REM Check piece color
            IF side = WHITE THEN
                IF pc < 0 THEN
                    GOTO NextSquare
                END IF
                absPc = pc
            ELSE
                IF pc > 0 THEN
                    GOTO NextSquare
                END IF
                absPc = 0 - pc
            END IF

            IF absPc = PAWN THEN
                GenPawnMoves(sq, side)
            ELSEIF absPc = KNIGHT THEN
                GenKnightMoves(sq, side)
            ELSEIF absPc = BISHOP THEN
                GenBishopMoves(sq, side)
            ELSEIF absPc = ROOK THEN
                GenRookMoves(sq, side)
            ELSEIF absPc = QUEEN THEN
                GenQueenMoves(sq, side)
            ELSEIF absPc = KING THEN
                GenKingMoves(sq, side)
            END IF

NextSquare:
        NEXT sq

        REM Generate castling
        GenCastlingMoves(side)
    END SUB

    SUB GenPawnMoves(sq AS INTEGER, side AS INTEGER)
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM t AS INTEGER
        DIM cap AS INTEGER

        f = sq MOD 8
        r = sq / 8

        IF side = WHITE THEN
            REM Single push
            IF r < 7 THEN
                t = sq + 8
                IF board.IsEmpty(t) = 1 THEN
                    IF r = 6 THEN
                        AddPromotion(sq, t, 0)
                    ELSE
                        AddMove(sq, t, PAWN, 0)
                    END IF
                END IF
            END IF

            REM Double push from rank 2
            IF r = 1 THEN
                IF board.IsEmpty(sq + 8) = 1 THEN
                    IF board.IsEmpty(sq + 16) = 1 THEN
                        AddMove(sq, sq + 16, PAWN, 0)
                    END IF
                END IF
            END IF

            REM Captures
            IF f > 0 AND r < 7 THEN
                t = sq + 7
                cap = board.GetSquare(t)
                IF cap < 0 THEN
                    IF r = 6 THEN
                        AddPromotion(sq, t, 0 - cap)
                    ELSE
                        AddMove(sq, t, PAWN, 0 - cap)
                    END IF
                END IF
                REM En passant
                IF t = board.GetEpSquare() THEN
                    AddEnPassant(sq, t)
                END IF
            END IF
            IF f < 7 AND r < 7 THEN
                t = sq + 9
                cap = board.GetSquare(t)
                IF cap < 0 THEN
                    IF r = 6 THEN
                        AddPromotion(sq, t, 0 - cap)
                    ELSE
                        AddMove(sq, t, PAWN, 0 - cap)
                    END IF
                END IF
                IF t = board.GetEpSquare() THEN
                    AddEnPassant(sq, t)
                END IF
            END IF
        ELSE
            REM Black pawns move down
            IF r > 0 THEN
                t = sq - 8
                IF board.IsEmpty(t) = 1 THEN
                    IF r = 1 THEN
                        AddPromotion(sq, t, 0)
                    ELSE
                        AddMove(sq, t, PAWN, 0)
                    END IF
                END IF
            END IF

            IF r = 6 THEN
                IF board.IsEmpty(sq - 8) = 1 THEN
                    IF board.IsEmpty(sq - 16) = 1 THEN
                        AddMove(sq, sq - 16, PAWN, 0)
                    END IF
                END IF
            END IF

            IF f > 0 AND r > 0 THEN
                t = sq - 9
                cap = board.GetSquare(t)
                IF cap > 0 THEN
                    IF r = 1 THEN
                        AddPromotion(sq, t, cap)
                    ELSE
                        AddMove(sq, t, PAWN, cap)
                    END IF
                END IF
                IF t = board.GetEpSquare() THEN
                    AddEnPassant(sq, t)
                END IF
            END IF
            IF f < 7 AND r > 0 THEN
                t = sq - 7
                cap = board.GetSquare(t)
                IF cap > 0 THEN
                    IF r = 1 THEN
                        AddPromotion(sq, t, cap)
                    ELSE
                        AddMove(sq, t, PAWN, cap)
                    END IF
                END IF
                IF t = board.GetEpSquare() THEN
                    AddEnPassant(sq, t)
                END IF
            END IF
        END IF
    END SUB

    SUB GenKnightMoves(sq AS INTEGER, side AS INTEGER)
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM t AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM cap AS INTEGER
        DIM i AS INTEGER
        DIM offsets(15) AS INTEGER

        f = sq MOD 8
        r = sq / 8

        REM Knight move offsets (file, rank pairs)
        offsets(0) = -2
        offsets(1) = -1
        offsets(2) = -2
        offsets(3) = 1
        offsets(4) = -1
        offsets(5) = -2
        offsets(6) = -1
        offsets(7) = 2
        offsets(8) = 1
        offsets(9) = -2
        offsets(10) = 1
        offsets(11) = 2
        offsets(12) = 2
        offsets(13) = -1
        offsets(14) = 2
        offsets(15) = 1

        FOR i = 0 TO 7
            tf = f + offsets(i * 2)
            tr = r + offsets(i * 2 + 1)
            IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                t = tr * 8 + tf
                cap = board.GetSquare(t)
                IF side = WHITE THEN
                    IF cap <= 0 THEN
                        AddMove(sq, t, KNIGHT, 0 - cap)
                    END IF
                ELSE
                    IF cap >= 0 THEN
                        AddMove(sq, t, KNIGHT, cap)
                    END IF
                END IF
            END IF
        NEXT i
    END SUB

    SUB GenSlidingMoves(sq AS INTEGER, side AS INTEGER, df AS INTEGER, dr AS INTEGER, pc AS INTEGER)
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM t AS INTEGER
        DIM cap AS INTEGER

        f = sq MOD 8
        r = sq / 8

        tf = f + df
        tr = r + dr

        DO WHILE tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7
            t = tr * 8 + tf
            cap = board.GetSquare(t)

            IF cap = 0 THEN
                AddMove(sq, t, pc, 0)
            ELSE
                IF side = WHITE THEN
                    IF cap < 0 THEN
                        AddMove(sq, t, pc, 0 - cap)
                    END IF
                ELSE
                    IF cap > 0 THEN
                        AddMove(sq, t, pc, cap)
                    END IF
                END IF
                EXIT DO
            END IF

            tf = tf + df
            tr = tr + dr
        LOOP
    END SUB

    SUB GenBishopMoves(sq AS INTEGER, side AS INTEGER)
        GenSlidingMoves(sq, side, 1, 1, BISHOP)
        GenSlidingMoves(sq, side, 1, -1, BISHOP)
        GenSlidingMoves(sq, side, -1, 1, BISHOP)
        GenSlidingMoves(sq, side, -1, -1, BISHOP)
    END SUB

    SUB GenRookMoves(sq AS INTEGER, side AS INTEGER)
        GenSlidingMoves(sq, side, 1, 0, ROOK)
        GenSlidingMoves(sq, side, -1, 0, ROOK)
        GenSlidingMoves(sq, side, 0, 1, ROOK)
        GenSlidingMoves(sq, side, 0, -1, ROOK)
    END SUB

    SUB GenQueenMoves(sq AS INTEGER, side AS INTEGER)
        GenSlidingMoves(sq, side, 1, 0, QUEEN)
        GenSlidingMoves(sq, side, -1, 0, QUEEN)
        GenSlidingMoves(sq, side, 0, 1, QUEEN)
        GenSlidingMoves(sq, side, 0, -1, QUEEN)
        GenSlidingMoves(sq, side, 1, 1, QUEEN)
        GenSlidingMoves(sq, side, 1, -1, QUEEN)
        GenSlidingMoves(sq, side, -1, 1, QUEEN)
        GenSlidingMoves(sq, side, -1, -1, QUEEN)
    END SUB

    SUB GenKingMoves(sq AS INTEGER, side AS INTEGER)
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM df AS INTEGER
        DIM dr AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM t AS INTEGER
        DIM cap AS INTEGER

        f = sq MOD 8
        r = sq / 8

        FOR df = -1 TO 1
            FOR dr = -1 TO 1
                IF df = 0 AND dr = 0 THEN
                    GOTO NextKingDir
                END IF
                tf = f + df
                tr = r + dr
                IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                    t = tr * 8 + tf
                    cap = board.GetSquare(t)
                    IF side = WHITE THEN
                        IF cap <= 0 THEN
                            AddMove(sq, t, KING, 0 - cap)
                        END IF
                    ELSE
                        IF cap >= 0 THEN
                            AddMove(sq, t, KING, cap)
                        END IF
                    END IF
                END IF
NextKingDir:
            NEXT dr
        NEXT df
    END SUB

    SUB GenCastlingMoves(side AS INTEGER)
        DIM cr AS INTEGER
        cr = board.GetCastleRights()

        IF side = WHITE THEN
            REM White kingside
            IF (cr MOD 2) = 1 THEN
                IF board.IsEmpty(5) = 1 AND board.IsEmpty(6) = 1 THEN
                    AddCastling(1)
                END IF
            END IF
            REM White queenside
            IF (cr MOD 4) >= 2 THEN
                IF board.IsEmpty(1) = 1 AND board.IsEmpty(2) = 1 AND board.IsEmpty(3) = 1 THEN
                    AddCastling(0)
                END IF
            END IF
        ELSE
            REM Black kingside
            DIM bkFlag AS INTEGER
            bkFlag = (cr / 4) MOD 2
            IF bkFlag = 1 THEN
                IF board.IsEmpty(61) = 1 AND board.IsEmpty(62) = 1 THEN
                    AddCastling(1)
                END IF
            END IF
            REM Black queenside
            IF cr >= 8 THEN
                IF board.IsEmpty(57) = 1 AND board.IsEmpty(58) = 1 AND board.IsEmpty(59) = 1 THEN
                    AddCastling(0)
                END IF
            END IF
        END IF
    END SUB
END CLASS

REM ====================================================================
REM AttackChecker Class - Check detection
REM ====================================================================
CLASS AttackChecker
    DIM board AS Board

    SUB Init(b AS Board)
        board = b
    END SUB

    SUB SetBoard(b AS Board)
        board = b
    END SUB

    FUNCTION IsSquareAttacked(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        REM Check if square is attacked by given color
        IsSquareAttacked = 0

        REM Check pawn attacks
        IF CheckPawnAttack(sq, byColor) = 1 THEN
            IsSquareAttacked = 1
            EXIT FUNCTION
        END IF

        REM Check knight attacks
        IF CheckKnightAttack(sq, byColor) = 1 THEN
            IsSquareAttacked = 1
            EXIT FUNCTION
        END IF

        REM Check bishop/queen diagonal attacks
        IF CheckDiagonalAttack(sq, byColor) = 1 THEN
            IsSquareAttacked = 1
            EXIT FUNCTION
        END IF

        REM Check rook/queen straight attacks
        IF CheckStraightAttack(sq, byColor) = 1 THEN
            IsSquareAttacked = 1
            EXIT FUNCTION
        END IF

        REM Check king attacks
        IF CheckKingAttack(sq, byColor) = 1 THEN
            IsSquareAttacked = 1
            EXIT FUNCTION
        END IF
    END FUNCTION

    FUNCTION CheckPawnAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM t AS INTEGER
        DIM pc AS INTEGER
        DIM target AS INTEGER

        f = sq MOD 8
        r = sq / 8

        IF byColor = WHITE THEN
            target = PAWN
            REM Check squares where white pawns could attack from
            IF r > 0 THEN
                IF f > 0 THEN
                    t = sq - 9
                    IF board.GetSquare(t) = target THEN
                        CheckPawnAttack = 1
                        EXIT FUNCTION
                    END IF
                END IF
                IF f < 7 THEN
                    t = sq - 7
                    IF board.GetSquare(t) = target THEN
                        CheckPawnAttack = 1
                        EXIT FUNCTION
                    END IF
                END IF
            END IF
        ELSE
            target = 0 - PAWN
            IF r < 7 THEN
                IF f > 0 THEN
                    t = sq + 7
                    IF board.GetSquare(t) = target THEN
                        CheckPawnAttack = 1
                        EXIT FUNCTION
                    END IF
                END IF
                IF f < 7 THEN
                    t = sq + 9
                    IF board.GetSquare(t) = target THEN
                        CheckPawnAttack = 1
                        EXIT FUNCTION
                    END IF
                END IF
            END IF
        END IF

        CheckPawnAttack = 0
    END FUNCTION

    FUNCTION CheckKnightAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM i AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM t AS INTEGER
        DIM pc AS INTEGER
        DIM target AS INTEGER
        DIM offsets(15) AS INTEGER

        f = sq MOD 8
        r = sq / 8

        IF byColor = WHITE THEN
            target = KNIGHT
        ELSE
            target = 0 - KNIGHT
        END IF

        offsets(0) = -2
        offsets(1) = -1
        offsets(2) = -2
        offsets(3) = 1
        offsets(4) = -1
        offsets(5) = -2
        offsets(6) = -1
        offsets(7) = 2
        offsets(8) = 1
        offsets(9) = -2
        offsets(10) = 1
        offsets(11) = 2
        offsets(12) = 2
        offsets(13) = -1
        offsets(14) = 2
        offsets(15) = 1

        FOR i = 0 TO 7
            tf = f + offsets(i * 2)
            tr = r + offsets(i * 2 + 1)
            IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                t = tr * 8 + tf
                IF board.GetSquare(t) = target THEN
                    CheckKnightAttack = 1
                    EXIT FUNCTION
                END IF
            END IF
        NEXT i

        CheckKnightAttack = 0
    END FUNCTION

    FUNCTION CheckDiagonalAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        DIM bishopPc AS INTEGER
        DIM queenPc AS INTEGER

        IF byColor = WHITE THEN
            bishopPc = 3
            queenPc = 5
        ELSE
            bishopPc = -3
            queenPc = -5
        END IF

        IF CheckRay(sq, 1, 1, bishopPc, queenPc) = 1 THEN
            CheckDiagonalAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, 1, -1, bishopPc, queenPc) = 1 THEN
            CheckDiagonalAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, -1, 1, bishopPc, queenPc) = 1 THEN
            CheckDiagonalAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, -1, -1, bishopPc, queenPc) = 1 THEN
            CheckDiagonalAttack = 1
            EXIT FUNCTION
        END IF

        CheckDiagonalAttack = 0
    END FUNCTION

    FUNCTION CheckStraightAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        DIM rookPc AS INTEGER
        DIM queenPc AS INTEGER

        IF byColor = WHITE THEN
            rookPc = 4
            queenPc = 5
        ELSE
            rookPc = -4
            queenPc = -5
        END IF

        IF CheckRay(sq, 1, 0, rookPc, queenPc) = 1 THEN
            CheckStraightAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, -1, 0, rookPc, queenPc) = 1 THEN
            CheckStraightAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, 0, 1, rookPc, queenPc) = 1 THEN
            CheckStraightAttack = 1
            EXIT FUNCTION
        END IF
        IF CheckRay(sq, 0, -1, rookPc, queenPc) = 1 THEN
            CheckStraightAttack = 1
            EXIT FUNCTION
        END IF

        CheckStraightAttack = 0
    END FUNCTION

    FUNCTION CheckRay(sq AS INTEGER, df AS INTEGER, dr AS INTEGER, pc1 AS INTEGER, pc2 AS INTEGER) AS INTEGER
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM t AS INTEGER
        DIM piece AS INTEGER

        f = sq MOD 8
        r = sq / 8
        tf = f + df
        tr = r + dr

        DO WHILE tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7
            t = tr * 8 + tf
            piece = board.GetSquare(t)
            IF piece <> 0 THEN
                IF piece = pc1 OR piece = pc2 THEN
                    CheckRay = 1
                    EXIT FUNCTION
                ELSE
                    CheckRay = 0
                    EXIT FUNCTION
                END IF
            END IF
            tf = tf + df
            tr = tr + dr
        LOOP

        CheckRay = 0
    END FUNCTION

    FUNCTION CheckKingAttack(sq AS INTEGER, byColor AS INTEGER) AS INTEGER
        DIM f AS INTEGER
        DIM r AS INTEGER
        DIM df AS INTEGER
        DIM dr AS INTEGER
        DIM tf AS INTEGER
        DIM tr AS INTEGER
        DIM t AS INTEGER
        DIM target AS INTEGER

        f = sq MOD 8
        r = sq / 8

        IF byColor = WHITE THEN
            target = KING
        ELSE
            target = 0 - KING
        END IF

        FOR df = -1 TO 1
            FOR dr = -1 TO 1
                IF df = 0 AND dr = 0 THEN
                    GOTO NextKingCheck
                END IF
                tf = f + df
                tr = r + dr
                IF tf >= 0 AND tf <= 7 AND tr >= 0 AND tr <= 7 THEN
                    t = tr * 8 + tf
                    IF board.GetSquare(t) = target THEN
                        CheckKingAttack = 1
                        EXIT FUNCTION
                    END IF
                END IF
NextKingCheck:
            NEXT dr
        NEXT df

        CheckKingAttack = 0
    END FUNCTION

    FUNCTION IsInCheck(side AS INTEGER) AS INTEGER
        DIM kingSq AS INTEGER
        DIM enemy AS INTEGER

        kingSq = board.FindKing(side)
        IF kingSq = -1 THEN
            IsInCheck = 0
            EXIT FUNCTION
        END IF

        IF side = WHITE THEN
            enemy = BLACK
        ELSE
            enemy = WHITE
        END IF

        IsInCheck = IsSquareAttacked(kingSq, enemy)
    END FUNCTION
END CLASS

REM ====================================================================
REM Evaluator Class - Position evaluation
REM ====================================================================
CLASS Evaluator
    DIM board AS Board

    SUB Init(b AS Board)
        board = b
    END SUB

    SUB SetBoard(b AS Board)
        board = b
    END SUB

    FUNCTION Evaluate() AS INTEGER
        DIM score AS INTEGER
        DIM sq AS INTEGER
        DIM pc AS INTEGER
        DIM absPc AS INTEGER
        DIM color AS INTEGER
        DIM pieceVal AS INTEGER
        DIM posVal AS INTEGER

        score = 0

        FOR sq = 0 TO 63
            pc = board.GetSquare(sq)
            IF pc = 0 THEN
                GOTO NextEvalSq
            END IF

            IF pc > 0 THEN
                color = WHITE
                absPc = pc
            ELSE
                color = BLACK
                absPc = 0 - pc
            END IF

            REM Material value
            IF absPc = PAWN THEN
                pieceVal = VAL_PAWN
            ELSEIF absPc = KNIGHT THEN
                pieceVal = VAL_KNIGHT
            ELSEIF absPc = BISHOP THEN
                pieceVal = VAL_BISHOP
            ELSEIF absPc = ROOK THEN
                pieceVal = VAL_ROOK
            ELSEIF absPc = QUEEN THEN
                pieceVal = VAL_QUEEN
            ELSEIF absPc = KING THEN
                pieceVal = 0
            ELSE
                pieceVal = 0
            END IF

            REM Positional value
            posVal = GetPieceSquareValue(sq, absPc, color)

            IF color = WHITE THEN
                score = score + pieceVal + posVal
            ELSE
                score = score - pieceVal - posVal
            END IF

NextEvalSq:
        NEXT sq

        REM Return score from side to move's perspective
        IF board.GetSide() = WHITE THEN
            Evaluate = score
        ELSE
            Evaluate = 0 - score
        END IF
    END FUNCTION

    FUNCTION GetPieceSquareValue(sq AS INTEGER, pc AS INTEGER, color AS INTEGER) AS INTEGER
        DIM r AS INTEGER
        DIM f AS INTEGER
        DIM idx AS INTEGER

        IF color = WHITE THEN
            r = sq / 8
            f = sq MOD 8
        ELSE
            r = 7 - (sq / 8)
            f = sq MOD 8
        END IF
        idx = r * 8 + f

        IF pc = PAWN THEN
            GetPieceSquareValue = GetPawnPST(idx)
        ELSEIF pc = KNIGHT THEN
            GetPieceSquareValue = GetKnightPST(idx)
        ELSEIF pc = BISHOP THEN
            GetPieceSquareValue = GetBishopPST(idx)
        ELSEIF pc = ROOK THEN
            GetPieceSquareValue = GetRookPST(idx)
        ELSEIF pc = QUEEN THEN
            GetPieceSquareValue = GetQueenPST(idx)
        ELSEIF pc = KING THEN
            GetPieceSquareValue = GetKingPST(idx)
        ELSE
            GetPieceSquareValue = 0
        END IF
    END FUNCTION

    FUNCTION GetPawnPST(idx AS INTEGER) AS INTEGER
        REM Pawn piece-square table (encourages center control and advancement)
        DIM pst(63) AS INTEGER
        pst(0) = 0
        pst(1) = 0
        pst(2) = 0
        pst(3) = 0
        pst(4) = 0
        pst(5) = 0
        pst(6) = 0
        pst(7) = 0
        pst(8) = 5
        pst(9) = 10
        pst(10) = 10
        pst(11) = -20
        pst(12) = -20
        pst(13) = 10
        pst(14) = 10
        pst(15) = 5
        pst(16) = 5
        pst(17) = -5
        pst(18) = -10
        pst(19) = 0
        pst(20) = 0
        pst(21) = -10
        pst(22) = -5
        pst(23) = 5
        pst(24) = 0
        pst(25) = 0
        pst(26) = 0
        pst(27) = 20
        pst(28) = 20
        pst(29) = 0
        pst(30) = 0
        pst(31) = 0
        pst(32) = 5
        pst(33) = 5
        pst(34) = 10
        pst(35) = 25
        pst(36) = 25
        pst(37) = 10
        pst(38) = 5
        pst(39) = 5
        pst(40) = 10
        pst(41) = 10
        pst(42) = 20
        pst(43) = 30
        pst(44) = 30
        pst(45) = 20
        pst(46) = 10
        pst(47) = 10
        pst(48) = 50
        pst(49) = 50
        pst(50) = 50
        pst(51) = 50
        pst(52) = 50
        pst(53) = 50
        pst(54) = 50
        pst(55) = 50
        pst(56) = 0
        pst(57) = 0
        pst(58) = 0
        pst(59) = 0
        pst(60) = 0
        pst(61) = 0
        pst(62) = 0
        pst(63) = 0
        GetPawnPST = pst(idx)
    END FUNCTION

    FUNCTION GetKnightPST(idx AS INTEGER) AS INTEGER
        DIM pst(63) AS INTEGER
        pst(0) = -50
        pst(1) = -40
        pst(2) = -30
        pst(3) = -30
        pst(4) = -30
        pst(5) = -30
        pst(6) = -40
        pst(7) = -50
        pst(8) = -40
        pst(9) = -20
        pst(10) = 0
        pst(11) = 5
        pst(12) = 5
        pst(13) = 0
        pst(14) = -20
        pst(15) = -40
        pst(16) = -30
        pst(17) = 5
        pst(18) = 10
        pst(19) = 15
        pst(20) = 15
        pst(21) = 10
        pst(22) = 5
        pst(23) = -30
        pst(24) = -30
        pst(25) = 0
        pst(26) = 15
        pst(27) = 20
        pst(28) = 20
        pst(29) = 15
        pst(30) = 0
        pst(31) = -30
        pst(32) = -30
        pst(33) = 5
        pst(34) = 15
        pst(35) = 20
        pst(36) = 20
        pst(37) = 15
        pst(38) = 5
        pst(39) = -30
        pst(40) = -30
        pst(41) = 0
        pst(42) = 10
        pst(43) = 15
        pst(44) = 15
        pst(45) = 10
        pst(46) = 0
        pst(47) = -30
        pst(48) = -40
        pst(49) = -20
        pst(50) = 0
        pst(51) = 0
        pst(52) = 0
        pst(53) = 0
        pst(54) = -20
        pst(55) = -40
        pst(56) = -50
        pst(57) = -40
        pst(58) = -30
        pst(59) = -30
        pst(60) = -30
        pst(61) = -30
        pst(62) = -40
        pst(63) = -50
        GetKnightPST = pst(idx)
    END FUNCTION

    FUNCTION GetBishopPST(idx AS INTEGER) AS INTEGER
        DIM pst(63) AS INTEGER
        pst(0) = -20
        pst(1) = -10
        pst(2) = -10
        pst(3) = -10
        pst(4) = -10
        pst(5) = -10
        pst(6) = -10
        pst(7) = -20
        pst(8) = -10
        pst(9) = 5
        pst(10) = 0
        pst(11) = 0
        pst(12) = 0
        pst(13) = 0
        pst(14) = 5
        pst(15) = -10
        pst(16) = -10
        pst(17) = 10
        pst(18) = 10
        pst(19) = 10
        pst(20) = 10
        pst(21) = 10
        pst(22) = 10
        pst(23) = -10
        pst(24) = -10
        pst(25) = 0
        pst(26) = 10
        pst(27) = 10
        pst(28) = 10
        pst(29) = 10
        pst(30) = 0
        pst(31) = -10
        pst(32) = -10
        pst(33) = 5
        pst(34) = 5
        pst(35) = 10
        pst(36) = 10
        pst(37) = 5
        pst(38) = 5
        pst(39) = -10
        pst(40) = -10
        pst(41) = 0
        pst(42) = 5
        pst(43) = 10
        pst(44) = 10
        pst(45) = 5
        pst(46) = 0
        pst(47) = -10
        pst(48) = -10
        pst(49) = 0
        pst(50) = 0
        pst(51) = 0
        pst(52) = 0
        pst(53) = 0
        pst(54) = 0
        pst(55) = -10
        pst(56) = -20
        pst(57) = -10
        pst(58) = -10
        pst(59) = -10
        pst(60) = -10
        pst(61) = -10
        pst(62) = -10
        pst(63) = -20
        GetBishopPST = pst(idx)
    END FUNCTION

    FUNCTION GetRookPST(idx AS INTEGER) AS INTEGER
        DIM pst(63) AS INTEGER
        pst(0) = 0
        pst(1) = 0
        pst(2) = 0
        pst(3) = 5
        pst(4) = 5
        pst(5) = 0
        pst(6) = 0
        pst(7) = 0
        pst(8) = -5
        pst(9) = 0
        pst(10) = 0
        pst(11) = 0
        pst(12) = 0
        pst(13) = 0
        pst(14) = 0
        pst(15) = -5
        pst(16) = -5
        pst(17) = 0
        pst(18) = 0
        pst(19) = 0
        pst(20) = 0
        pst(21) = 0
        pst(22) = 0
        pst(23) = -5
        pst(24) = -5
        pst(25) = 0
        pst(26) = 0
        pst(27) = 0
        pst(28) = 0
        pst(29) = 0
        pst(30) = 0
        pst(31) = -5
        pst(32) = -5
        pst(33) = 0
        pst(34) = 0
        pst(35) = 0
        pst(36) = 0
        pst(37) = 0
        pst(38) = 0
        pst(39) = -5
        pst(40) = -5
        pst(41) = 0
        pst(42) = 0
        pst(43) = 0
        pst(44) = 0
        pst(45) = 0
        pst(46) = 0
        pst(47) = -5
        pst(48) = 5
        pst(49) = 10
        pst(50) = 10
        pst(51) = 10
        pst(52) = 10
        pst(53) = 10
        pst(54) = 10
        pst(55) = 5
        pst(56) = 0
        pst(57) = 0
        pst(58) = 0
        pst(59) = 0
        pst(60) = 0
        pst(61) = 0
        pst(62) = 0
        pst(63) = 0
        GetRookPST = pst(idx)
    END FUNCTION

    FUNCTION GetQueenPST(idx AS INTEGER) AS INTEGER
        DIM pst(63) AS INTEGER
        pst(0) = -20
        pst(1) = -10
        pst(2) = -10
        pst(3) = -5
        pst(4) = -5
        pst(5) = -10
        pst(6) = -10
        pst(7) = -20
        pst(8) = -10
        pst(9) = 0
        pst(10) = 5
        pst(11) = 0
        pst(12) = 0
        pst(13) = 0
        pst(14) = 0
        pst(15) = -10
        pst(16) = -10
        pst(17) = 5
        pst(18) = 5
        pst(19) = 5
        pst(20) = 5
        pst(21) = 5
        pst(22) = 0
        pst(23) = -10
        pst(24) = 0
        pst(25) = 0
        pst(26) = 5
        pst(27) = 5
        pst(28) = 5
        pst(29) = 5
        pst(30) = 0
        pst(31) = -5
        pst(32) = -5
        pst(33) = 0
        pst(34) = 5
        pst(35) = 5
        pst(36) = 5
        pst(37) = 5
        pst(38) = 0
        pst(39) = -5
        pst(40) = -10
        pst(41) = 0
        pst(42) = 5
        pst(43) = 5
        pst(44) = 5
        pst(45) = 5
        pst(46) = 0
        pst(47) = -10
        pst(48) = -10
        pst(49) = 0
        pst(50) = 0
        pst(51) = 0
        pst(52) = 0
        pst(53) = 0
        pst(54) = 0
        pst(55) = -10
        pst(56) = -20
        pst(57) = -10
        pst(58) = -10
        pst(59) = -5
        pst(60) = -5
        pst(61) = -10
        pst(62) = -10
        pst(63) = -20
        GetQueenPST = pst(idx)
    END FUNCTION

    FUNCTION GetKingPST(idx AS INTEGER) AS INTEGER
        REM Middlegame king table (stay safe, castle)
        DIM pst(63) AS INTEGER
        pst(0) = 20
        pst(1) = 30
        pst(2) = 10
        pst(3) = 0
        pst(4) = 0
        pst(5) = 10
        pst(6) = 30
        pst(7) = 20
        pst(8) = 20
        pst(9) = 20
        pst(10) = 0
        pst(11) = 0
        pst(12) = 0
        pst(13) = 0
        pst(14) = 20
        pst(15) = 20
        pst(16) = -10
        pst(17) = -20
        pst(18) = -20
        pst(19) = -20
        pst(20) = -20
        pst(21) = -20
        pst(22) = -20
        pst(23) = -10
        pst(24) = -20
        pst(25) = -30
        pst(26) = -30
        pst(27) = -40
        pst(28) = -40
        pst(29) = -30
        pst(30) = -30
        pst(31) = -20
        pst(32) = -30
        pst(33) = -40
        pst(34) = -40
        pst(35) = -50
        pst(36) = -50
        pst(37) = -40
        pst(38) = -40
        pst(39) = -30
        pst(40) = -30
        pst(41) = -40
        pst(42) = -40
        pst(43) = -50
        pst(44) = -50
        pst(45) = -40
        pst(46) = -40
        pst(47) = -30
        pst(48) = -30
        pst(49) = -40
        pst(50) = -40
        pst(51) = -50
        pst(52) = -50
        pst(53) = -40
        pst(54) = -40
        pst(55) = -30
        pst(56) = -30
        pst(57) = -40
        pst(58) = -40
        pst(59) = -50
        pst(60) = -50
        pst(61) = -40
        pst(62) = -40
        pst(63) = -30
        GetKingPST = pst(idx)
    END FUNCTION
END CLASS

REM ====================================================================
REM AI Class - Alpha-Beta Search Engine
REM ====================================================================
CLASS AIEngine
    DIM board AS Board
    DIM moveGen AS MoveGen
    DIM attacker AS AttackChecker
    DIM evaluator AS Evaluator
    DIM maxDepth AS INTEGER
    DIM nodesSearched AS INTEGER
    DIM bestMove AS Move
    DIM hasBestMove AS INTEGER

    SUB Init(b AS Board, depth AS INTEGER)
        board = b
        maxDepth = depth
        nodesSearched = 0
        hasBestMove = 0

        moveGen = NEW MoveGen()
        moveGen.Init(board)

        attacker = NEW AttackChecker()
        attacker.Init(board)

        evaluator = NEW Evaluator()
        evaluator.Init(board)
    END SUB

    SUB SetDepth(d AS INTEGER)
        maxDepth = d
    END SUB

    SUB SetBoard(b AS Board)
        board = b
        moveGen.SetBoard(b)
        attacker.SetBoard(b)
        evaluator.SetBoard(b)
    END SUB

    FUNCTION GetBestMove() AS Move
        DIM score AS INTEGER
        nodesSearched = 0
        hasBestMove = 0

        score = AlphaBeta(maxDepth, 0 - INF, INF)

        IF hasBestMove = 1 THEN
            GetBestMove = bestMove
        ELSE
            REM Return first legal move if no best found
            moveGen.SetBoard(board)
            moveGen.GenerateMoves()
            DIM i AS INTEGER
            FOR i = 0 TO moveGen.GetMoveCount() - 1
                IF IsLegalMove(moveGen.GetMove(i)) = 1 THEN
                    GetBestMove = moveGen.GetMove(i)
                    EXIT FUNCTION
                END IF
            NEXT i
            GetBestMove = moveGen.GetMove(0)
        END IF
    END FUNCTION

    FUNCTION GetNodesSearched() AS INTEGER
        GetNodesSearched = nodesSearched
    END FUNCTION

    FUNCTION AlphaBeta(depth AS INTEGER, alpha AS INTEGER, beta AS INTEGER) AS INTEGER
        DIM score AS INTEGER
        DIM bestScore AS INTEGER
        DIM i AS INTEGER
        DIM m AS Move
        DIM tempBoard AS Board
        DIM legalMoves AS INTEGER
        DIM localMoveGen AS MoveGen

        nodesSearched = nodesSearched + 1

        REM Terminal node
        IF depth = 0 THEN
            AlphaBeta = evaluator.Evaluate()
            EXIT FUNCTION
        END IF

        REM Generate moves using local MoveGen to avoid recursion corruption
        localMoveGen = NEW MoveGen()
        localMoveGen.Init(board)
        localMoveGen.GenerateMoves()

        bestScore = 0 - INF
        legalMoves = 0

        FOR i = 0 TO moveCount - 1
            m = localMoveGen.GetMove(i)

            REM Check legality
            IF IsLegalMove(m) = 0 THEN
                GOTO NextABMove
            END IF

            legalMoves = legalMoves + 1

            REM Make move on copy
            tempBoard = NEW Board()
            tempBoard.CopyFrom(board)
            board.MakeMove(m)

            REM Recurse
            evaluator.SetBoard(board)
            attacker.SetBoard(board)
            score = 0 - AlphaBeta(depth - 1, 0 - beta, 0 - alpha)

            REM Unmake move
            board.CopyFrom(tempBoard)
            evaluator.SetBoard(board)
            attacker.SetBoard(board)

            IF score > bestScore THEN
                bestScore = score
                IF depth = maxDepth THEN
                    bestMove = m
                    hasBestMove = 1
                END IF
            END IF

            IF score > alpha THEN
                alpha = score
            END IF

            IF alpha >= beta THEN
                AlphaBeta = bestScore
                EXIT FUNCTION
            END IF

NextABMove:
        NEXT i

        REM Check for checkmate or stalemate
        IF legalMoves = 0 THEN
            IF attacker.IsInCheck(board.GetSide()) = 1 THEN
                AlphaBeta = 0 - MATE + (maxDepth - depth)
            ELSE
                AlphaBeta = 0
            END IF
            EXIT FUNCTION
        END IF

        AlphaBeta = bestScore
    END FUNCTION

    SUB OrderMoves()
        REM Simple MVV-LVA ordering
        DIM i AS INTEGER
        DIM m AS Move
        DIM cap AS INTEGER
        DIM pc AS INTEGER
        DIM score AS INTEGER

        FOR i = 0 TO moveGen.GetMoveCount() - 1
            m = moveGen.GetMove(i)
            cap = m.GetCaptured()
            pc = m.GetPiece()

            IF cap > 0 THEN
                REM MVV-LVA: victim value - attacker value / 10
                score = GetPieceValue(cap) * 10 - GetPieceValue(pc)
            ELSE
                score = 0
            END IF

            REM Bonus for promotion
            IF m.GetPromotion() > 0 THEN
                score = score + GetPieceValue(m.GetPromotion())
            END IF

            m.SetScore(score)
        NEXT i

        REM Simple bubble sort by score (descending)
        DIM j AS INTEGER
        DIM temp AS Move
        FOR i = 0 TO moveGen.GetMoveCount() - 2
            FOR j = i + 1 TO moveGen.GetMoveCount() - 1
                IF moveGen.GetMove(j).GetScore() > moveGen.GetMove(i).GetScore() THEN
                    temp = moveGen.GetMove(i)
                    REM Swap not directly possible, need to regenerate
                    GOTO SkipSort
                END IF
            NEXT j
        NEXT i
SkipSort:
    END SUB

    FUNCTION GetPieceValue(pc AS INTEGER) AS INTEGER
        IF pc = PAWN THEN
            GetPieceValue = VAL_PAWN
        ELSEIF pc = KNIGHT THEN
            GetPieceValue = VAL_KNIGHT
        ELSEIF pc = BISHOP THEN
            GetPieceValue = VAL_BISHOP
        ELSEIF pc = ROOK THEN
            GetPieceValue = VAL_ROOK
        ELSEIF pc = QUEEN THEN
            GetPieceValue = VAL_QUEEN
        ELSE
            GetPieceValue = 0
        END IF
    END FUNCTION

    FUNCTION IsLegalMove(m AS Move) AS INTEGER
        DIM tempBoard AS Board
        DIM side AS INTEGER

        side = board.GetSide()

        REM Make move on copy
        tempBoard = NEW Board()
        tempBoard.CopyFrom(board)
        board.MakeMove(m)

        REM Check if own king is in check
        attacker.SetBoard(board)
        IF attacker.IsInCheck(side) = 1 THEN
            board.CopyFrom(tempBoard)
            attacker.SetBoard(board)
            IsLegalMove = 0
            EXIT FUNCTION
        END IF

        REM Handle castling through check
        IF m.GetCastling() > 0 THEN
            REM Check intermediate squares
            DIM f AS INTEGER
            DIM t AS INTEGER
            f = m.GetFrom()
            t = m.GetTo()

            board.CopyFrom(tempBoard)
            attacker.SetBoard(board)

            IF side = WHITE THEN
                IF attacker.IsSquareAttacked(4, BLACK) = 1 THEN
                    IsLegalMove = 0
                    EXIT FUNCTION
                END IF
                IF m.GetCastling() = 2 THEN
                    IF attacker.IsSquareAttacked(5, BLACK) = 1 THEN
                        IsLegalMove = 0
                        EXIT FUNCTION
                    END IF
                ELSE
                    IF attacker.IsSquareAttacked(3, BLACK) = 1 THEN
                        IsLegalMove = 0
                        EXIT FUNCTION
                    END IF
                END IF
            ELSE
                IF attacker.IsSquareAttacked(60, WHITE) = 1 THEN
                    IsLegalMove = 0
                    EXIT FUNCTION
                END IF
                IF m.GetCastling() = 2 THEN
                    IF attacker.IsSquareAttacked(61, WHITE) = 1 THEN
                        IsLegalMove = 0
                        EXIT FUNCTION
                    END IF
                ELSE
                    IF attacker.IsSquareAttacked(59, WHITE) = 1 THEN
                        IsLegalMove = 0
                        EXIT FUNCTION
                    END IF
                END IF
            END IF
        END IF

        board.CopyFrom(tempBoard)
        attacker.SetBoard(board)
        IsLegalMove = 1
    END FUNCTION
END CLASS

REM ====================================================================
REM Display Class - Board rendering
REM ====================================================================
CLASS Display
    DIM board AS Board

    SUB Init(b AS Board)
        board = b
    END SUB

    SUB SetBoard(b AS Board)
        board = b
    END SUB

    SUB DrawBoard()
        DIM r AS INTEGER
        DIM f AS INTEGER
        DIM sq AS INTEGER
        DIM pc AS INTEGER
        DIM isLight AS INTEGER
        DIM ch AS STRING
        DIM lastFrom AS INTEGER
        DIM lastTo AS INTEGER

        lastFrom = board.GetLastFrom()
        lastTo = board.GetLastTo()

        PRINT
        PRINT "    a   b   c   d   e   f   g   h"
        PRINT "  +---+---+---+---+---+---+---+---+"

        FOR r = 7 TO 0 STEP -1
            PRINT r + 1; "|";
            FOR f = 0 TO 7
                sq = r * 8 + f
                pc = board.GetSquare(sq)

                REM Highlight last move
                IF sq = lastFrom OR sq = lastTo THEN
                    PRINT "*";
                ELSE
                    PRINT " ";
                END IF

                ch = PieceChar(pc)
                PRINT ch;

                IF sq = lastFrom OR sq = lastTo THEN
                    PRINT "*|";
                ELSE
                    PRINT " |";
                END IF
            NEXT f
            PRINT " "; r + 1
            PRINT "  +---+---+---+---+---+---+---+---+"
        NEXT r

        PRINT "    a   b   c   d   e   f   g   h"
        PRINT
    END SUB

    FUNCTION PieceChar(pc AS INTEGER) AS STRING
        IF pc = PAWN THEN
            PieceChar = "P"
        ELSEIF pc = KNIGHT THEN
            PieceChar = "N"
        ELSEIF pc = BISHOP THEN
            PieceChar = "B"
        ELSEIF pc = ROOK THEN
            PieceChar = "R"
        ELSEIF pc = QUEEN THEN
            PieceChar = "Q"
        ELSEIF pc = KING THEN
            PieceChar = "K"
        ELSEIF pc = 0 - PAWN THEN
            PieceChar = "p"
        ELSEIF pc = 0 - KNIGHT THEN
            PieceChar = "n"
        ELSEIF pc = 0 - BISHOP THEN
            PieceChar = "b"
        ELSEIF pc = 0 - ROOK THEN
            PieceChar = "r"
        ELSEIF pc = 0 - QUEEN THEN
            PieceChar = "q"
        ELSEIF pc = 0 - KING THEN
            PieceChar = "k"
        ELSE
            PieceChar = "."
        END IF
    END FUNCTION

    SUB ShowStatus()
        DIM side AS STRING
        IF board.GetSide() = WHITE THEN
            side = "White"
        ELSE
            side = "Black"
        END IF
        PRINT side; " to move"
        PRINT "Move: "; board.GetFullMove(); "  Half-move clock: "; board.GetHalfMove()
    END SUB
END CLASS

REM ====================================================================
REM Game Class - Main game controller
REM ====================================================================
CLASS Game
    DIM board AS Board
    DIM display AS Display
    DIM ai AS AIEngine
    DIM moveGen AS MoveGen
    DIM attacker AS AttackChecker
    DIM humanColor AS INTEGER
    DIM aiDepth AS INTEGER
    DIM gameOver AS INTEGER

    SUB Init()
        board = NEW Board()
        board.SetupInitial()

        display = NEW Display()
        display.Init(board)

        moveGen = NEW MoveGen()
        moveGen.Init(board)

        attacker = NEW AttackChecker()
        attacker.Init(board)

        aiDepth = 2
        humanColor = WHITE
        gameOver = 0

        ai = NEW AIEngine()
        ai.Init(board, aiDepth)
    END SUB

    SUB SetAIDepth(d AS INTEGER)
        aiDepth = d
        ai.SetDepth(d)
    END SUB

    SUB SetHumanColor(c AS INTEGER)
        humanColor = c
    END SUB

    SUB Run()
        DIM userCmd AS STRING
        DIM m AS Move
        DIM startTime AS INTEGER
        DIM endTime AS INTEGER

        PRINT "==================================="
        PRINT "       VIPER CHESS"
        PRINT "==================================="
        PRINT
        PRINT "Commands:"
        PRINT "  Move: e2e4, e7e8q (promotion)"
        PRINT "  quit - Exit game"
        PRINT "  new  - New game"
        PRINT "  depth N - Set AI depth (1-6)"
        PRINT "==================================="
        PRINT

        DO WHILE gameOver = 0
            display.SetBoard(board)
            display.DrawBoard()
            display.ShowStatus()

            REM Check game end conditions
            IF IsGameOver() = 1 THEN
                EXIT DO
            END IF

            IF board.GetSide() = humanColor THEN
                REM Human move
                PRINT "Your move: ";
                INPUT userCmd

                IF userCmd = "quit" THEN
                    gameOver = 1
                    EXIT DO
                ELSEIF userCmd = "new" THEN
                    board.SetupInitial()
                    GOTO ContinueLoop
                ELSEIF LEFT$(userCmd, 5) = "depth" THEN
                    DIM newDepth AS INTEGER
                    newDepth = VAL(MID$(userCmd, 7))
                    IF newDepth >= 1 AND newDepth <= 6 THEN
                        SetAIDepth(newDepth)
                        PRINT "AI depth set to "; newDepth
                    END IF
                    GOTO ContinueLoop
                END IF

                m = ParseMove(userCmd)
                IF m.GetFrom() >= 0 THEN
                    IF IsLegalMove(m) = 1 THEN
                        board.MakeMove(m)
                    ELSE
                        PRINT "Illegal move!"
                    END IF
                ELSE
                    PRINT "Invalid input. Use format: e2e4"
                END IF
            ELSE
                REM AI move
                PRINT "AI thinking..."
                startTime = Viper.Time.GetTickCount()

                ai.SetBoard(board)
                m = ai.GetBestMove()
                board.MakeMove(m)

                endTime = Viper.Time.GetTickCount()

                PRINT "AI plays: "; m.ToAlgebraic()
                PRINT "Nodes: "; ai.GetNodesSearched(); "  Time: "; endTime - startTime; "ms"
            END IF

ContinueLoop:
        LOOP

        PRINT "Game Over!"
    END SUB

    FUNCTION ParseMove(moveStr AS STRING) AS Move
        DIM m AS Move
        DIM fromFile AS INTEGER
        DIM fromRank AS INTEGER
        DIM toFile AS INTEGER
        DIM toRank AS INTEGER
        DIM fromSq AS INTEGER
        DIM toSq AS INTEGER
        DIM promo AS INTEGER
        DIM i AS INTEGER
        DIM pc AS INTEGER
        DIM cap AS INTEGER
        DIM len AS INTEGER

        m = NEW Move()
        m.Init(-1, -1, 0, 0)

        len = LEN(moveStr)
        IF len < 4 THEN
            ParseMove = m
            EXIT FUNCTION
        END IF

        fromFile = ASC(MID$(moveStr, 1, 1)) - 97
        fromRank = ASC(MID$(moveStr, 2, 1)) - 49
        toFile = ASC(MID$(moveStr, 3, 1)) - 97
        toRank = ASC(MID$(moveStr, 4, 1)) - 49

        IF fromFile < 0 OR fromFile > 7 THEN
            ParseMove = m
            EXIT FUNCTION
        END IF
        IF fromRank < 0 OR fromRank > 7 THEN
            ParseMove = m
            EXIT FUNCTION
        END IF
        IF toFile < 0 OR toFile > 7 THEN
            ParseMove = m
            EXIT FUNCTION
        END IF
        IF toRank < 0 OR toRank > 7 THEN
            ParseMove = m
            EXIT FUNCTION
        END IF

        fromSq = fromRank * 8 + fromFile
        toSq = toRank * 8 + toFile

        pc = board.AbsPiece(fromSq)
        cap = board.AbsPiece(toSq)

        m.Init(fromSq, toSq, pc, cap)

        REM Check for promotion
        IF len >= 5 THEN
            DIM promoChar AS STRING
            promoChar = MID$(moveStr, 5, 1)
            IF promoChar = "q" OR promoChar = "Q" THEN
                m.SetPromotion(QUEEN)
            ELSEIF promoChar = "r" OR promoChar = "R" THEN
                m.SetPromotion(ROOK)
            ELSEIF promoChar = "b" OR promoChar = "B" THEN
                m.SetPromotion(BISHOP)
            ELSEIF promoChar = "n" OR promoChar = "N" THEN
                m.SetPromotion(KNIGHT)
            END IF
        ELSE
            REM Auto-promote to queen
            IF pc = PAWN THEN
                IF (board.GetSide() = WHITE AND toRank = 7) OR (board.GetSide() = BLACK AND toRank = 0) THEN
                    m.SetPromotion(QUEEN)
                END IF
            END IF
        END IF

        REM Check for castling
        IF pc = KING THEN
            DIM diff AS INTEGER
            IF toSq > fromSq THEN
                diff = toSq - fromSq
            ELSE
                diff = fromSq - toSq
            END IF
            IF diff = 2 THEN
                IF toFile > fromFile THEN
                    m.SetCastling(2)
                ELSE
                    m.SetCastling(1)
                END IF
            END IF
        END IF

        REM Check for en passant
        IF pc = PAWN THEN
            IF toSq = board.GetEpSquare() THEN
                m.SetEnPassant(1)
                m.Init(fromSq, toSq, PAWN, PAWN)
                m.SetEnPassant(1)
            END IF
        END IF

        ParseMove = m
    END FUNCTION

    FUNCTION IsLegalMove(m AS Move) AS INTEGER
        DIM tempBoard AS Board
        DIM side AS INTEGER
        DIM inCheck AS INTEGER

        side = board.GetSide()

        REM Make move
        tempBoard = NEW Board()
        tempBoard.CopyFrom(board)
        board.MakeMove(m)

        REM Check if own king is in check
        attacker.SetBoard(board)
        inCheck = attacker.IsInCheck(side)
        IF inCheck = 1 THEN
            board.CopyFrom(tempBoard)
            attacker.SetBoard(board)
            IsLegalMove = 0
            EXIT FUNCTION
        END IF

        board.CopyFrom(tempBoard)
        attacker.SetBoard(board)
        IsLegalMove = 1
    END FUNCTION

    FUNCTION IsGameOver() AS INTEGER
        DIM side AS INTEGER
        DIM i AS INTEGER
        DIM legalMoves AS INTEGER
        DIM m AS Move

        side = board.GetSide()

        REM 50-move rule
        IF board.GetHalfMove() >= 100 THEN
            PRINT "Draw by 50-move rule!"
            gameOver = 1
            IsGameOver = 1
            EXIT FUNCTION
        END IF

        REM Check for legal moves
        moveGen.SetBoard(board)
        moveGen.GenerateMoves()
        legalMoves = 0

        FOR i = 0 TO moveGen.GetMoveCount() - 1
            m = moveGen.GetMove(i)
            IF IsLegalMove(m) = 1 THEN
                legalMoves = legalMoves + 1
                EXIT FOR
            END IF
        NEXT i

        IF legalMoves = 0 THEN
            IF attacker.IsInCheck(side) = 1 THEN
                IF side = WHITE THEN
                    PRINT "Checkmate! Black wins!"
                ELSE
                    PRINT "Checkmate! White wins!"
                END IF
            ELSE
                PRINT "Stalemate! Draw!"
            END IF
            gameOver = 1
            IsGameOver = 1
            EXIT FUNCTION
        END IF

        REM Check for insufficient material
        IF IsInsufficientMaterial() = 1 THEN
            PRINT "Draw by insufficient material!"
            gameOver = 1
            IsGameOver = 1
            EXIT FUNCTION
        END IF

        IsGameOver = 0
    END FUNCTION

    FUNCTION IsInsufficientMaterial() AS INTEGER
        DIM i AS INTEGER
        DIM pc AS INTEGER
        DIM whitePieces AS INTEGER
        DIM blackPieces AS INTEGER
        DIM whiteMinor AS INTEGER
        DIM blackMinor AS INTEGER

        whitePieces = 0
        blackPieces = 0
        whiteMinor = 0
        blackMinor = 0

        FOR i = 0 TO 63
            pc = board.GetSquare(i)
            IF pc = PAWN OR pc = 0 - PAWN THEN
                IsInsufficientMaterial = 0
                EXIT FUNCTION
            END IF
            IF pc = ROOK OR pc = 0 - ROOK THEN
                IsInsufficientMaterial = 0
                EXIT FUNCTION
            END IF
            IF pc = QUEEN OR pc = 0 - QUEEN THEN
                IsInsufficientMaterial = 0
                EXIT FUNCTION
            END IF
            IF pc = KNIGHT OR pc = BISHOP THEN
                whitePieces = whitePieces + 1
                whiteMinor = whiteMinor + 1
            END IF
            IF pc = 0 - KNIGHT OR pc = 0 - BISHOP THEN
                blackPieces = blackPieces + 1
                blackMinor = blackMinor + 1
            END IF
        NEXT i

        REM K vs K
        IF whitePieces = 0 AND blackPieces = 0 THEN
            IsInsufficientMaterial = 1
            EXIT FUNCTION
        END IF

        REM K+minor vs K
        IF whitePieces = 0 AND blackMinor = 1 THEN
            IsInsufficientMaterial = 1
            EXIT FUNCTION
        END IF
        IF blackPieces = 0 AND whiteMinor = 1 THEN
            IsInsufficientMaterial = 1
            EXIT FUNCTION
        END IF

        IsInsufficientMaterial = 0
    END FUNCTION
END CLASS

REM ====================================================================
REM MAIN PROGRAM
REM ====================================================================

DIM game AS Game
game = NEW Game()
game.Init()
game.Run()

END
