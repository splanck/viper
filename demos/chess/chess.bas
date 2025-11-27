REM ============================================================================
REM  VIPER CHESS - Full-Featured Chess Game with AI
REM ============================================================================
REM  Features:
REM  - Complete chess rules (castling, en passant, pawn promotion)
REM  - Minimax AI with alpha-beta pruning
REM  - ANSI board display using StringBuilder
REM  - Save/Load game functionality
REM  - Move history and undo using List
REM  - Extensive use of Viper.* OOP runtime
REM
REM  NOTE: All code in single file due to multi-file class visibility bug
REM ============================================================================

REM ============================================================================
REM  PIECE CLASS
REM ============================================================================
CLASS Piece
    DIM pieceType AS INTEGER
    DIM pieceColor AS INTEGER
    DIM pieceMoved AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER)
        pieceType = pType
        pieceColor = pColor
        pieceMoved = 0
    END SUB

    SUB SetMoved()
        pieceMoved = 1
    END SUB

    FUNCTION GetType() AS INTEGER
        GetType = pieceType
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        GetColor = pieceColor
    END FUNCTION

    FUNCTION GetMoved() AS INTEGER
        GetMoved = pieceMoved
    END FUNCTION

    FUNCTION IsEmpty() AS INTEGER
        IF pieceType = 0 THEN
            IsEmpty = 1
        ELSE
            IsEmpty = 0
        END IF
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        DIM sym AS STRING
        sym = "."

        IF pieceType = 1 THEN
            IF pieceColor = 1 THEN
                sym = "P"
            ELSE
                sym = "p"
            END IF
        ELSE IF pieceType = 2 THEN
            IF pieceColor = 1 THEN
                sym = "N"
            ELSE
                sym = "n"
            END IF
        ELSE IF pieceType = 3 THEN
            IF pieceColor = 1 THEN
                sym = "B"
            ELSE
                sym = "b"
            END IF
        ELSE IF pieceType = 4 THEN
            IF pieceColor = 1 THEN
                sym = "R"
            ELSE
                sym = "r"
            END IF
        ELSE IF pieceType = 5 THEN
            IF pieceColor = 1 THEN
                sym = "Q"
            ELSE
                sym = "q"
            END IF
        ELSE IF pieceType = 6 THEN
            IF pieceColor = 1 THEN
                sym = "K"
            ELSE
                sym = "k"
            END IF
        END IF

        GetSymbol = sym
    END FUNCTION

    FUNCTION GetValue() AS INTEGER
        DIM val AS INTEGER
        val = 0

        IF pieceType = 1 THEN
            val = 100
        ELSE IF pieceType = 2 THEN
            val = 320
        ELSE IF pieceType = 3 THEN
            val = 330
        ELSE IF pieceType = 4 THEN
            val = 500
        ELSE IF pieceType = 5 THEN
            val = 900
        ELSE IF pieceType = 6 THEN
            val = 20000
        END IF

        GetValue = val
    END FUNCTION
END CLASS

REM ============================================================================
REM  MOVE CLASS
REM ============================================================================
CLASS Move
    DIM fromRow AS INTEGER
    DIM fromCol AS INTEGER
    DIM toRow AS INTEGER
    DIM toCol AS INTEGER
    DIM capturedType AS INTEGER
    DIM capturedColor AS INTEGER
    DIM moveFlag AS INTEGER
    DIM promotionType AS INTEGER
    DIM score AS INTEGER

    SUB Init(fr AS INTEGER, fc AS INTEGER, tr AS INTEGER, tc AS INTEGER)
        fromRow = fr
        fromCol = fc
        toRow = tr
        toCol = tc
        capturedType = 0
        capturedColor = 0
        moveFlag = 0
        promotionType = 0
        score = 0
    END SUB

    SUB SetCapture(cType AS INTEGER, cColor AS INTEGER)
        capturedType = cType
        capturedColor = cColor
    END SUB

    SUB SetFlag(flag AS INTEGER)
        moveFlag = flag
    END SUB

    SUB SetPromotion(pType AS INTEGER)
        promotionType = pType
        moveFlag = 4
    END SUB

    SUB SetScore(s AS INTEGER)
        score = s
    END SUB

    FUNCTION GetFromRow() AS INTEGER
        GetFromRow = fromRow
    END FUNCTION

    FUNCTION GetFromCol() AS INTEGER
        GetFromCol = fromCol
    END FUNCTION

    FUNCTION GetToRow() AS INTEGER
        GetToRow = toRow
    END FUNCTION

    FUNCTION GetToCol() AS INTEGER
        GetToCol = toCol
    END FUNCTION

    FUNCTION GetCapturedType() AS INTEGER
        GetCapturedType = capturedType
    END FUNCTION

    FUNCTION GetCapturedColor() AS INTEGER
        GetCapturedColor = capturedColor
    END FUNCTION

    FUNCTION GetFlag() AS INTEGER
        GetFlag = moveFlag
    END FUNCTION

    FUNCTION GetPromotionType() AS INTEGER
        GetPromotionType = promotionType
    END FUNCTION

    FUNCTION GetScore() AS INTEGER
        GetScore = score
    END FUNCTION

    FUNCTION ToAlgebraic() AS STRING
        DIM result AS STRING
        DIM fromFile AS STRING
        DIM toFile AS STRING

        fromFile = CHR$(97 + fromCol)
        toFile = CHR$(97 + toCol)

        result = fromFile + LTRIM$(STR$(fromRow + 1)) + toFile + LTRIM$(STR$(toRow + 1))

        IF moveFlag = 4 AND promotionType > 0 THEN
            IF promotionType = 5 THEN
                result = result + "q"
            ELSE IF promotionType = 4 THEN
                result = result + "r"
            ELSE IF promotionType = 3 THEN
                result = result + "b"
            ELSE IF promotionType = 2 THEN
                result = result + "n"
            END IF
        END IF

        ToAlgebraic = result
    END FUNCTION
END CLASS

REM ============================================================================
REM  GLOBAL STATE
REM ============================================================================
DIM board(8, 8) AS Piece
DIM currentTurn AS INTEGER
DIM whiteCanCastleKing AS INTEGER
DIM whiteCanCastleQueen AS INTEGER
DIM blackCanCastleKing AS INTEGER
DIM blackCanCastleQueen AS INTEGER
DIM enPassantCol AS INTEGER
DIM halfMoveClock AS INTEGER
DIM fullMoveNumber AS INTEGER
DIM gameOver AS INTEGER
DIM winner AS INTEGER
DIM moveHistory AS Viper.Collections.List
DIM aiDepth AS INTEGER
DIM playerColor AS INTEGER
DIM nodesSearched AS INTEGER

REM Global return values for FindKing (workaround for no BYREF)
DIM foundKingRow AS INTEGER
DIM foundKingCol AS INTEGER

REM ANSI escape
DIM ESC AS STRING
ESC = CHR$(27)

REM ============================================================================
REM  BOARD HELPERS
REM ============================================================================
FUNCTION IsValidSquare(row AS INTEGER, col AS INTEGER) AS INTEGER
    IF row >= 0 AND row <= 7 AND col >= 0 AND col <= 7 THEN
        IsValidSquare = 1
    ELSE
        IsValidSquare = 0
    END IF
END FUNCTION

FUNCTION IsEmptySquare(row AS INTEGER, col AS INTEGER) AS INTEGER
    IF board(row, col).GetType() = 0 THEN
        IsEmptySquare = 1
    ELSE
        IsEmptySquare = 0
    END IF
END FUNCTION

FUNCTION IsEnemyPiece(row AS INTEGER, col AS INTEGER, clr AS INTEGER) AS INTEGER
    DIM piece AS Piece
    piece = board(row, col)
    IF piece.GetType() > 0 AND piece.GetColor() <> clr THEN
        IsEnemyPiece = 1
    ELSE
        IsEnemyPiece = 0
    END IF
END FUNCTION

FUNCTION IsFriendlyPiece(row AS INTEGER, col AS INTEGER, clr AS INTEGER) AS INTEGER
    DIM piece AS Piece
    piece = board(row, col)
    IF piece.GetType() > 0 AND piece.GetColor() = clr THEN
        IsFriendlyPiece = 1
    ELSE
        IsFriendlyPiece = 0
    END IF
END FUNCTION

REM ============================================================================
REM  FIND KING - Sets foundKingRow/foundKingCol globals
REM ============================================================================
SUB FindKing(clr AS INTEGER)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM found AS INTEGER

    foundKingRow = -1
    foundKingCol = -1
    found = 0

    row = 0
    WHILE row <= 7 AND found = 0
        col = 0
        WHILE col <= 7 AND found = 0
            piece = board(row, col)
            IF piece.GetType() = 6 AND piece.GetColor() = clr THEN
                foundKingRow = row
                foundKingCol = col
                found = 1
            END IF
            col = col + 1
        WEND
        row = row + 1
    WEND
END SUB

REM ============================================================================
REM  INITIALIZE BOARD
REM ============================================================================
SUB InitBoard()
    DIM row AS INTEGER
    DIM col AS INTEGER

    REM Clear board
    FOR row = 0 TO 7
        FOR col = 0 TO 7
            board(row, col) = NEW Piece()
            board(row, col).Init(0, 0)
        NEXT col
    NEXT row

    REM Place white pieces (row 0 and 1)
    board(0, 0) = NEW Piece()
    board(0, 0).Init(4, 1)
    board(0, 1) = NEW Piece()
    board(0, 1).Init(2, 1)
    board(0, 2) = NEW Piece()
    board(0, 2).Init(3, 1)
    board(0, 3) = NEW Piece()
    board(0, 3).Init(5, 1)
    board(0, 4) = NEW Piece()
    board(0, 4).Init(6, 1)
    board(0, 5) = NEW Piece()
    board(0, 5).Init(3, 1)
    board(0, 6) = NEW Piece()
    board(0, 6).Init(2, 1)
    board(0, 7) = NEW Piece()
    board(0, 7).Init(4, 1)

    FOR col = 0 TO 7
        board(1, col) = NEW Piece()
        board(1, col).Init(1, 1)
    NEXT col

    REM Place black pieces (row 6 and 7)
    FOR col = 0 TO 7
        board(6, col) = NEW Piece()
        board(6, col).Init(1, -1)
    NEXT col

    board(7, 0) = NEW Piece()
    board(7, 0).Init(4, -1)
    board(7, 1) = NEW Piece()
    board(7, 1).Init(2, -1)
    board(7, 2) = NEW Piece()
    board(7, 2).Init(3, -1)
    board(7, 3) = NEW Piece()
    board(7, 3).Init(5, -1)
    board(7, 4) = NEW Piece()
    board(7, 4).Init(6, -1)
    board(7, 5) = NEW Piece()
    board(7, 5).Init(3, -1)
    board(7, 6) = NEW Piece()
    board(7, 6).Init(2, -1)
    board(7, 7) = NEW Piece()
    board(7, 7).Init(4, -1)

    REM Initialize game state
    currentTurn = 1
    whiteCanCastleKing = 1
    whiteCanCastleQueen = 1
    blackCanCastleKing = 1
    blackCanCastleQueen = 1
    enPassantCol = -1
    halfMoveClock = 0
    fullMoveNumber = 1
    gameOver = 0
    winner = 0

    REM Initialize move history using List
    moveHistory = NEW Viper.Collections.List()
END SUB

REM ============================================================================
REM  BOARD DISPLAY
REM ============================================================================
SUB DrawBoard()
    DIM sb AS Viper.Text.StringBuilder
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM sym AS STRING

    sb = NEW Viper.Text.StringBuilder()

    REM Clear screen
    PRINT ESC; "[2J"; ESC; "[H";

    REM Draw title
    PRINT ""
    PRINT ESC; "[1;36m"; "    VIPER CHESS"; ESC; "[0m"
    PRINT ""

    REM Draw column headers
    PRINT "      a   b   c   d   e   f   g   h"
    PRINT "    +---+---+---+---+---+---+---+---+"

    REM Draw board from top (row 7) to bottom (row 0)
    FOR row = 7 TO 0 STEP -1
        sb.Clear()
        sb.Append("  ")
        sb.Append(LTRIM$(STR$(row + 1)))
        sb.Append(" |")

        FOR col = 0 TO 7
            piece = board(row, col)
            sym = piece.GetSymbol()

            sb.Append(" ")
            IF piece.GetType() > 0 THEN
                sb.Append(sym)
            ELSE
                sb.Append(".")
            END IF
            sb.Append(" |")
        NEXT col

        sb.Append(" ")
        sb.Append(LTRIM$(STR$(row + 1)))

        PRINT sb.ToString()
        PRINT "    +---+---+---+---+---+---+---+---+"
    NEXT row

    PRINT "      a   b   c   d   e   f   g   h"
    PRINT ""

    REM Display game info
    IF currentTurn = 1 THEN
        PRINT "  Turn: WHITE"
    ELSE
        PRINT "  Turn: BLACK"
    END IF

    PRINT "  Move: "; fullMoveNumber

    IF moveHistory.Count > 0 THEN
        DIM lastMove AS Move
        lastMove = moveHistory.get_Item(moveHistory.Count - 1)
        PRINT "  Last: "; lastMove.ToAlgebraic()
    END IF

    PRINT ""
END SUB

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
        DIM qRook2 AS Piece
        qRook2 = board(toRow, 3)
        board(toRow, 0) = qRook2
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

REM ============================================================================
REM  GENERATE MOVES FOR A PIECE
REM ============================================================================
SUB GeneratePieceMoves(row AS INTEGER, col AS INTEGER, moves AS Viper.Collections.List)
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
REM  AI - PIECE SQUARE VALUE
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
REM  AI - EVALUATE POSITION
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
REM  AI - MINIMAX WITH ALPHA-BETA
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
REM  AI - GET BEST MOVE
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

REM ============================================================================
REM  PARSE MOVE INPUT
REM ============================================================================
FUNCTION ParseMove(inp AS STRING) AS Move
    DIM move AS Move
    DIM fromFile AS STRING
    DIM toFile AS STRING
    DIM fromRank AS STRING
    DIM toRank AS STRING
    DIM fromRow AS INTEGER
    DIM fromCol AS INTEGER
    DIM toRow AS INTEGER
    DIM toCol AS INTEGER
    DIM cleanInput AS STRING

    move = NEW Move()

    cleanInput = TRIM$(LCASE$(inp))

    IF LEN(cleanInput) < 4 THEN
        move.Init(-1, -1, -1, -1)
        ParseMove = move
        EXIT FUNCTION
    END IF

    fromFile = LEFT$(cleanInput, 1)
    fromRank = MID$(cleanInput, 2, 1)
    toFile = MID$(cleanInput, 3, 1)
    toRank = MID$(cleanInput, 4, 1)

    fromCol = ASC(fromFile) - 97
    toCol = ASC(toFile) - 97
    fromRow = VAL(fromRank) - 1
    toRow = VAL(toRank) - 1

    IF fromCol < 0 OR fromCol > 7 OR toCol < 0 OR toCol > 7 THEN
        move.Init(-1, -1, -1, -1)
        ParseMove = move
        EXIT FUNCTION
    END IF

    IF fromRow < 0 OR fromRow > 7 OR toRow < 0 OR toRow > 7 THEN
        move.Init(-1, -1, -1, -1)
        ParseMove = move
        EXIT FUNCTION
    END IF

    move.Init(fromRow, fromCol, toRow, toCol)

    IF LEN(cleanInput) >= 5 THEN
        DIM promChar AS STRING
        promChar = MID$(cleanInput, 5, 1)

        IF promChar = "q" THEN
            move.SetPromotion(5)
        ELSE IF promChar = "r" THEN
            move.SetPromotion(4)
        ELSE IF promChar = "b" THEN
            move.SetPromotion(3)
        ELSE IF promChar = "n" THEN
            move.SetPromotion(2)
        END IF
    END IF

    ParseMove = move
END FUNCTION

REM ============================================================================
REM  VALIDATE AND EXECUTE PLAYER MOVE
REM ============================================================================
FUNCTION TryPlayerMove(inp AS STRING) AS INTEGER
    DIM parsedMove AS Move
    DIM legalMoves AS Viper.Collections.List
    DIM move AS Move
    DIM i AS INTEGER
    DIM found AS INTEGER

    parsedMove = ParseMove(inp)

    IF parsedMove.GetFromRow() < 0 THEN
        PRINT "Invalid move format. Use algebraic notation (e.g., e2e4)"
        TryPlayerMove = 0
        EXIT FUNCTION
    END IF

    DIM piece AS Piece
    piece = board(parsedMove.GetFromRow(), parsedMove.GetFromCol())

    IF piece.GetType() = 0 THEN
        PRINT "No piece at that square!"
        TryPlayerMove = 0
        EXIT FUNCTION
    END IF

    IF piece.GetColor() <> currentTurn THEN
        PRINT "That's not your piece!"
        TryPlayerMove = 0
        EXIT FUNCTION
    END IF

    legalMoves = GenerateLegalMoves(currentTurn)
    found = 0

    FOR i = 0 TO legalMoves.Count - 1
        move = legalMoves.get_Item(i)

        IF move.GetFromRow() = parsedMove.GetFromRow() THEN
            IF move.GetFromCol() = parsedMove.GetFromCol() THEN
                IF move.GetToRow() = parsedMove.GetToRow() THEN
                    IF move.GetToCol() = parsedMove.GetToCol() THEN
                        IF move.GetFlag() = 4 THEN
                            IF parsedMove.GetPromotionType() > 0 THEN
                                move.SetPromotion(parsedMove.GetPromotionType())
                            ELSE
                                move.SetPromotion(5)
                            END IF
                        END IF

                        found = 1
                        MakeMove(move)
                        moveHistory.Add(move)

                        IF currentTurn = 1 THEN
                            currentTurn = -1
                        ELSE
                            currentTurn = 1
                            fullMoveNumber = fullMoveNumber + 1
                        END IF

                        EXIT FOR
                    END IF
                END IF
            END IF
        END IF
    NEXT i

    IF found = 0 THEN
        PRINT "Illegal move!"
        TryPlayerMove = 0
        EXIT FUNCTION
    END IF

    TryPlayerMove = 1
END FUNCTION

REM ============================================================================
REM  CHECK GAME END
REM ============================================================================
SUB CheckGameEnd()
    DIM moves AS Viper.Collections.List

    moves = GenerateLegalMoves(currentTurn)

    IF moves.Count = 0 THEN
        IF IsInCheck(currentTurn) = 1 THEN
            gameOver = 1
            IF currentTurn = 1 THEN
                winner = -1
                PRINT ""
                PRINT "CHECKMATE! Black wins!"
            ELSE
                winner = 1
                PRINT ""
                PRINT "CHECKMATE! White wins!"
            END IF
        ELSE
            gameOver = 1
            winner = 0
            PRINT ""
            PRINT "STALEMATE! Game is a draw."
        END IF
    ELSE IF IsInCheck(currentTurn) = 1 THEN
        PRINT "CHECK!"
    END IF
END SUB

REM ============================================================================
REM  SHOW VALID MOVES
REM ============================================================================
SUB ShowValidMoves(inp AS STRING)
    DIM cleanInput AS STRING
    DIM fileChar AS STRING
    DIM rankChar AS STRING
    DIM col AS INTEGER
    DIM row AS INTEGER
    DIM moves AS Viper.Collections.List
    DIM move AS Move
    DIM sb AS Viper.Text.StringBuilder
    DIM i AS INTEGER
    DIM count AS INTEGER

    cleanInput = TRIM$(LCASE$(inp))

    IF LEN(cleanInput) < 2 THEN
        PRINT "Usage: show <square> (e.g., show e2)"
        EXIT SUB
    END IF

    fileChar = LEFT$(cleanInput, 1)
    rankChar = MID$(cleanInput, 2, 1)

    col = ASC(fileChar) - 97
    row = VAL(rankChar) - 1

    IF col < 0 OR col > 7 OR row < 0 OR row > 7 THEN
        PRINT "Invalid square!"
        EXIT SUB
    END IF

    moves = GenerateLegalMoves(currentTurn)
    sb = NEW Viper.Text.StringBuilder()
    sb.Append("Valid moves from ")
    sb.Append(fileChar)
    sb.Append(rankChar)
    sb.Append(": ")

    count = 0
    FOR i = 0 TO moves.Count - 1
        move = moves.get_Item(i)
        IF move.GetFromRow() = row AND move.GetFromCol() = col THEN
            IF count > 0 THEN
                sb.Append(", ")
            END IF
            sb.Append(move.ToAlgebraic())
            count = count + 1
        END IF
    NEXT i

    IF count = 0 THEN
        sb.Append("(none)")
    END IF

    PRINT sb.ToString()
END SUB

REM ============================================================================
REM  SAVE GAME
REM ============================================================================
SUB SaveGame(filename AS STRING)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece

    OPEN filename FOR OUTPUT AS #1

    FOR row = 0 TO 7
        FOR col = 0 TO 7
            piece = board(row, col)
            REM Write piece data as separate values
            PRINT #1, piece.GetType()
            PRINT #1, piece.GetColor()
            PRINT #1, piece.GetMoved()
        NEXT col
    NEXT row

    PRINT #1, currentTurn
    PRINT #1, whiteCanCastleKing
    PRINT #1, whiteCanCastleQueen
    PRINT #1, blackCanCastleKing
    PRINT #1, blackCanCastleQueen
    PRINT #1, enPassantCol
    PRINT #1, fullMoveNumber
    PRINT #1, aiDepth
    PRINT #1, playerColor

    CLOSE #1

    PRINT "Game saved to "; filename
END SUB

REM ============================================================================
REM  LOAD GAME
REM ============================================================================
SUB LoadGame(filename AS STRING)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM pType AS INTEGER
    DIM pColor AS INTEGER
    DIM pMoved AS INTEGER
    DIM piece AS Piece

    OPEN filename FOR INPUT AS #1

    FOR row = 0 TO 7
        FOR col = 0 TO 7
            REM Read piece data as separate values
            INPUT #1, pType
            INPUT #1, pColor
            INPUT #1, pMoved
            piece = NEW Piece()
            piece.Init(pType, pColor)
            IF pMoved = 1 THEN
                piece.SetMoved()
            END IF
            board(row, col) = piece
        NEXT col
    NEXT row

    INPUT #1, currentTurn
    INPUT #1, whiteCanCastleKing
    INPUT #1, whiteCanCastleQueen
    INPUT #1, blackCanCastleKing
    INPUT #1, blackCanCastleQueen
    INPUT #1, enPassantCol
    INPUT #1, fullMoveNumber
    INPUT #1, aiDepth
    INPUT #1, playerColor

    CLOSE #1

    PRINT "Game loaded from "; filename
END SUB

REM ============================================================================
REM  UNDO MOVE
REM ============================================================================
SUB UndoLastMove()
    IF moveHistory.Count < 2 THEN
        PRINT "Cannot undo - not enough moves!"
        EXIT SUB
    END IF

    DIM move AS Move
    DIM i AS INTEGER

    FOR i = 1 TO 2
        IF moveHistory.Count > 0 THEN
            move = moveHistory.get_Item(moveHistory.Count - 1)
            UnmakeMove(move)
            moveHistory.RemoveAt(moveHistory.Count - 1)

            IF currentTurn = 1 THEN
                currentTurn = -1
                fullMoveNumber = fullMoveNumber - 1
            ELSE
                currentTurn = 1
            END IF
        END IF
    NEXT i

    PRINT "Moves undone!"
END SUB

REM ============================================================================
REM  MAIN MENU
REM ============================================================================
SUB ShowMainMenu()
    DIM choice AS STRING

    WHILE 1 = 1
        PRINT ESC; "[2J"; ESC; "[H";
        PRINT ""
        PRINT ESC; "[1;36m"; "    +===============================+"; ESC; "[0m"
        PRINT ESC; "[1;36m"; "    |      VIPER CHESS v1.0         |"; ESC; "[0m"
        PRINT ESC; "[1;36m"; "    +===============================+"; ESC; "[0m"
        PRINT ""
        PRINT "    [1] New Game"
        PRINT "    [2] Load Game"
        PRINT "    [3] Options"
        PRINT "    [4] Instructions"
        PRINT "    [Q] Quit"
        PRINT ""
        PRINT "    Select: ";

        INPUT choice
        choice = TRIM$(UCASE$(choice))

        IF choice = "1" THEN
            ShowNewGameMenu()
        ELSE IF choice = "2" THEN
            LoadGame("chess_save.dat")
            PlayGame()
        ELSE IF choice = "3" THEN
            ShowOptions()
        ELSE IF choice = "4" THEN
            ShowInstructions()
        ELSE IF choice = "Q" THEN
            EXIT WHILE
        END IF
    WEND
END SUB

REM ============================================================================
REM  NEW GAME MENU
REM ============================================================================
SUB ShowNewGameMenu()
    DIM choice AS STRING

    PRINT ESC; "[2J"; ESC; "[H";
    PRINT ""
    PRINT "    === NEW GAME ==="
    PRINT ""
    PRINT "    Play as:"
    PRINT "    [1] White (you move first)"
    PRINT "    [2] Black (AI moves first)"
    PRINT ""
    PRINT "    Color [1/2]: ";
    INPUT choice

    IF choice = "2" THEN
        playerColor = -1
    ELSE
        playerColor = 1
    END IF

    PRINT ""
    PRINT "    Difficulty:"
    PRINT "    [E] Easy (depth 2)"
    PRINT "    [M] Medium (depth 3)"
    PRINT "    [H] Hard (depth 4)"
    PRINT ""
    PRINT "    Difficulty [E/M/H]: ";
    INPUT choice
    choice = UCASE$(TRIM$(choice))

    IF choice = "E" THEN
        aiDepth = 2
    ELSE IF choice = "H" THEN
        aiDepth = 4
    ELSE
        aiDepth = 3
    END IF

    InitBoard()
    PlayGame()
END SUB

REM ============================================================================
REM  OPTIONS
REM ============================================================================
SUB ShowOptions()
    DIM choice AS STRING
    DIM depth AS INTEGER

    PRINT ESC; "[2J"; ESC; "[H";
    PRINT ""
    PRINT "    === OPTIONS ==="
    PRINT ""
    PRINT "    Current AI Depth: "; aiDepth
    PRINT ""
    PRINT "    [1] Set AI Depth (1-5)"
    PRINT "    [B] Back"
    PRINT ""
    PRINT "    Select: ";

    INPUT choice
    choice = UCASE$(TRIM$(choice))

    IF choice = "1" THEN
        PRINT "    Enter depth (1-5): ";
        INPUT depth
        IF depth >= 1 AND depth <= 5 THEN
            aiDepth = depth
            PRINT "    Depth set to "; aiDepth
        ELSE
            PRINT "    Invalid depth!"
        END IF
        SLEEP 1000
    END IF
END SUB

REM ============================================================================
REM  INSTRUCTIONS
REM ============================================================================
SUB ShowInstructions()
    PRINT ESC; "[2J"; ESC; "[H";
    PRINT ""
    PRINT "    === INSTRUCTIONS ==="
    PRINT ""
    PRINT "    MOVES:"
    PRINT "      Enter moves in algebraic notation: e2e4"
    PRINT "      (from-square to to-square)"
    PRINT ""
    PRINT "      For pawn promotion, add piece letter: e7e8q"
    PRINT "      (q=queen, r=rook, b=bishop, n=knight)"
    PRINT ""
    PRINT "    COMMANDS:"
    PRINT "      show <square>  - Show valid moves (e.g., show e2)"
    PRINT "      undo           - Undo last move pair"
    PRINT "      save           - Save game"
    PRINT "      quit           - Return to menu"
    PRINT ""
    PRINT "    PIECES:"
    PRINT "      K/k = King    Q/q = Queen"
    PRINT "      R/r = Rook    B/b = Bishop"
    PRINT "      N/n = Knight  P/p = Pawn"
    PRINT "      (Uppercase = White, Lowercase = Black)"
    PRINT ""
    PRINT "    Press ENTER to continue..."

    DIM dummy AS STRING
    INPUT dummy
END SUB

REM ============================================================================
REM  MAIN GAME LOOP
REM ============================================================================
SUB PlayGame()
    DIM inp AS STRING
    DIM aiMove AS Move

    gameOver = 0

    WHILE gameOver = 0
        DrawBoard()
        CheckGameEnd()

        IF gameOver = 1 THEN
            EXIT WHILE
        END IF

        IF currentTurn = playerColor THEN
            PRINT "Your move (or 'help'): ";
            INPUT inp
            inp = TRIM$(inp)

            IF LCASE$(inp) = "quit" THEN
                EXIT WHILE
            ELSE IF LCASE$(inp) = "save" THEN
                SaveGame("chess_save.dat")
            ELSE IF LCASE$(inp) = "undo" THEN
                UndoLastMove()
            ELSE IF LCASE$(inp) = "help" THEN
                PRINT "Commands: <move>, show <sq>, undo, save, quit"
            ELSE IF LEFT$(LCASE$(inp), 4) = "show" THEN
                ShowValidMoves(MID$(inp, 6))
            ELSE
                IF TryPlayerMove(inp) = 0 THEN
                    PRINT "Try again..."
                END IF
            END IF

            SLEEP 500
        ELSE
            aiMove = GetBestMove(currentTurn)

            IF aiMove.GetFromRow() >= 0 THEN
                PRINT "AI plays: "; aiMove.ToAlgebraic()
                MakeMove(aiMove)
                moveHistory.Add(aiMove)

                IF currentTurn = 1 THEN
                    currentTurn = -1
                ELSE
                    currentTurn = 1
                    fullMoveNumber = fullMoveNumber + 1
                END IF
            END IF

            SLEEP 500
        END IF
    WEND

    PRINT ""
    PRINT "Press ENTER to continue..."
    DIM dummy AS STRING
    INPUT dummy
END SUB

REM ============================================================================
REM  MAIN PROGRAM
REM ============================================================================
RANDOMIZE

aiDepth = 3
playerColor = 1
InitBoard()

ShowMainMenu()

PRINT ""
PRINT "Thanks for playing VIPER CHESS!"
PRINT ""
