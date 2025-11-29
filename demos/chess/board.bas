'==============================================================================
' CHESS BOARD
' Board representation using mailbox array (8x8 = 64 squares)
' Each square holds piece type and color encoded as: color * 8 + pieceType
' Empty squares = 0
'==============================================================================

' Global board state
DIM gBoard(63) AS INTEGER       ' Piece on each square (color*8 + type, 0=empty)
DIM gSideToMove AS INTEGER      ' WHITE or BLACK
DIM gCastling AS INTEGER        ' Castling rights (bitfield)
DIM gEpSquare AS INTEGER        ' En passant target square (-1 if none)
DIM gHalfMoveClock AS INTEGER   ' Half-move clock for 50-move rule
DIM gFullMoveNumber AS INTEGER  ' Full move number
DIM gKingSquare(1) AS INTEGER   ' King positions [WHITE], [BLACK]

' Position history for repetition detection
DIM gPosHistory(511) AS INTEGER ' Simple hash history
DIM gPosHistoryCount AS INTEGER

' Encode piece for board storage
FUNCTION EncodePiece(pieceType AS INTEGER, pieceColor AS INTEGER) AS INTEGER
    IF pieceType = EMPTY THEN
        EncodePiece = 0
    ELSE
        EncodePiece = pieceColor * 8 + pieceType
    END IF
END FUNCTION

' Decode piece type from board
FUNCTION DecodePieceType(encoded AS INTEGER) AS INTEGER
    IF encoded = 0 THEN
        DecodePieceType = EMPTY
    ELSE
        DecodePieceType = (encoded - 1) MOD 8 + 1
    END IF
END FUNCTION

' Decode piece color from board
FUNCTION DecodePieceColor(encoded AS INTEGER) AS INTEGER
    IF encoded = 0 THEN
        DecodePieceColor = WHITE
    ELSE
        DecodePieceColor = (encoded - 1) \ 8
    END IF
END FUNCTION

' Get piece at square
FUNCTION GetPiece(sq AS INTEGER) AS INTEGER
    GetPiece = DecodePieceType(gBoard(sq))
END FUNCTION

' Get color at square
FUNCTION GetColor(sq AS INTEGER) AS INTEGER
    GetColor = DecodePieceColor(gBoard(sq))
END FUNCTION

' Set piece at square
SUB SetPiece(sq AS INTEGER, pieceType AS INTEGER, pieceColor AS INTEGER)
    gBoard(sq) = EncodePiece(pieceType, pieceColor)
    IF pieceType = KING THEN
        gKingSquare(pieceColor) = sq
    END IF
END SUB

' Clear square
SUB ClearSquare(sq AS INTEGER)
    gBoard(sq) = 0
END SUB

' Check if square is empty
FUNCTION IsEmpty(sq AS INTEGER) AS INTEGER
    IF gBoard(sq) = 0 THEN
        IsEmpty = 1
    ELSE
        IsEmpty = 0
    END IF
END FUNCTION

' Check if square has enemy piece
FUNCTION IsEnemy(sq AS INTEGER, myColor AS INTEGER) AS INTEGER
    IF gBoard(sq) = 0 THEN
        IsEnemy = 0
    ELSEIF DecodePieceColor(gBoard(sq)) <> myColor THEN
        IsEnemy = 1
    ELSE
        IsEnemy = 0
    END IF
END FUNCTION

' Check if square has friendly piece
FUNCTION IsFriendly(sq AS INTEGER, myColor AS INTEGER) AS INTEGER
    IF gBoard(sq) = 0 THEN
        IsFriendly = 0
    ELSEIF DecodePieceColor(gBoard(sq)) = myColor THEN
        IsFriendly = 1
    ELSE
        IsFriendly = 0
    END IF
END FUNCTION

' Initialize board to starting position
SUB InitBoard()
    DIM i AS INTEGER

    ' Clear board
    FOR i = 0 TO 63
        gBoard(i) = 0
    NEXT i

    ' White pieces
    SetPiece(SQ(0, 0), ROOK, WHITE)
    SetPiece(SQ(1, 0), KNIGHT, WHITE)
    SetPiece(SQ(2, 0), BISHOP, WHITE)
    SetPiece(SQ(3, 0), QUEEN, WHITE)
    SetPiece(SQ(4, 0), KING, WHITE)
    SetPiece(SQ(5, 0), BISHOP, WHITE)
    SetPiece(SQ(6, 0), KNIGHT, WHITE)
    SetPiece(SQ(7, 0), ROOK, WHITE)

    ' White pawns
    FOR i = 0 TO 7
        SetPiece(SQ(i, 1), PAWN, WHITE)
    NEXT i

    ' Black pieces
    SetPiece(SQ(0, 7), ROOK, BLACK)
    SetPiece(SQ(1, 7), KNIGHT, BLACK)
    SetPiece(SQ(2, 7), BISHOP, BLACK)
    SetPiece(SQ(3, 7), QUEEN, BLACK)
    SetPiece(SQ(4, 7), KING, BLACK)
    SetPiece(SQ(5, 7), BISHOP, BLACK)
    SetPiece(SQ(6, 7), KNIGHT, BLACK)
    SetPiece(SQ(7, 7), ROOK, BLACK)

    ' Black pawns
    FOR i = 0 TO 7
        SetPiece(SQ(i, 6), PAWN, BLACK)
    NEXT i

    ' Initialize game state
    gSideToMove = WHITE
    ' Use addition since castling flags are distinct bits (1+2+4+8 = 15)
    gCastling = CASTLE_WK + CASTLE_WQ + CASTLE_BK + CASTLE_BQ
    gEpSquare = -1
    gHalfMoveClock = 0
    gFullMoveNumber = 1
    gPosHistoryCount = 0
END SUB

' Simple position hash for repetition detection
' Using simple additive hash to prevent overflow
FUNCTION ComputeHash() AS INTEGER
    DIM h AS INTEGER
    DIM i AS INTEGER
    DIM piece AS INTEGER

    h = 0
    ' Sum of piece * (square + 1) gives unique signature for position
    FOR i = 0 TO 63
        piece = gBoard(i)
        IF piece <> 0 THEN
            h = h + piece * (i + 1)
        END IF
    NEXT i

    ' Add state info with small multipliers
    h = h + gSideToMove * 100
    h = h + gCastling * 1000
    h = h + (gEpSquare + 1) * 10000

    ComputeHash = h
END FUNCTION

' Store position in history
SUB StorePosition()
    IF gPosHistoryCount < 512 THEN
        gPosHistory(gPosHistoryCount) = ComputeHash()
        gPosHistoryCount = gPosHistoryCount + 1
    END IF
END SUB

' Check for threefold repetition
FUNCTION IsRepetition() AS INTEGER
    DIM currentHash AS INTEGER
    DIM count AS INTEGER
    DIM i AS INTEGER

    currentHash = ComputeHash()
    count = 0

    FOR i = 0 TO gPosHistoryCount - 1
        IF gPosHistory(i) = currentHash THEN
            count = count + 1
            IF count >= 2 THEN
                IsRepetition = 1
                EXIT FUNCTION
            END IF
        END IF
    NEXT i

    IsRepetition = 0
END FUNCTION

' Display board with ANSI colors
SUB DisplayBoard()
    DIM rank AS INTEGER
    DIM file AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM c AS STRING
    DIM bgLight AS STRING
    DIM bgDark AS STRING
    DIM fgWhite AS STRING
    DIM fgBlack AS STRING
    DIM reset AS STRING
    DIM sb AS Viper.Text.StringBuilder

    sb = NEW Viper.Text.StringBuilder()

    ' ANSI escape codes
    bgLight = ESC + "[48;5;180m"   ' Light square (tan)
    bgDark = ESC + "[48;5;94m"     ' Dark square (brown)
    fgWhite = ESC + "[97m"         ' White pieces
    fgBlack = ESC + "[30m"         ' Black pieces
    reset = ESC + "[0m"

    sb.Append(reset)
    sb.Append("  +---+---+---+---+---+---+---+---+")
    sb.Append(CHR$(10))

    FOR rank = 7 TO 0 STEP -1
        sb.Append(CHR$(49 + rank))
        sb.Append(" |")

        FOR file = 0 TO 7
            sq = SQ(file, rank)
            piece = GetPiece(sq)
            clr = GetColor(sq)

            ' Background color based on square
            IF ((rank + file) MOD 2) = 1 THEN
                sb.Append(bgLight)
            ELSE
                sb.Append(bgDark)
            END IF

            ' Piece character
            IF piece = EMPTY THEN
                sb.Append("   ")
            ELSE
                sb.Append(" ")
                IF clr = WHITE THEN
                    sb.Append(fgWhite)
                ELSE
                    sb.Append(fgBlack)
                END IF
                c = PieceChar(piece, WHITE)  ' Always uppercase, color from foreground
                sb.Append(c)
                sb.Append(" ")
            END IF

            sb.Append(reset)
            sb.Append("|")
        NEXT file

        sb.Append(CHR$(10))
        sb.Append("  +---+---+---+---+---+---+---+---+")
        sb.Append(CHR$(10))
    NEXT rank

    sb.Append("    a   b   c   d   e   f   g   h")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Display board in simple ASCII (no colors)
SUB DisplayBoardSimple()
    DIM rank AS INTEGER
    DIM file AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM c AS STRING
    DIM sb AS Viper.Text.StringBuilder

    sb = NEW Viper.Text.StringBuilder()

    sb.Append("  +---+---+---+---+---+---+---+---+")
    sb.Append(CHR$(10))

    FOR rank = 7 TO 0 STEP -1
        sb.Append(CHR$(49 + rank))
        sb.Append(" |")

        FOR file = 0 TO 7
            sq = SQ(file, rank)
            piece = GetPiece(sq)
            clr = GetColor(sq)

            sb.Append(" ")
            c = PieceChar(piece, clr)
            sb.Append(c)
            sb.Append(" |")
        NEXT file

        sb.Append(CHR$(10))
        sb.Append("  +---+---+---+---+---+---+---+---+")
        sb.Append(CHR$(10))
    NEXT rank

    sb.Append("    a   b   c   d   e   f   g   h")
    sb.Append(CHR$(10))

    PRINT sb.ToString()
END SUB

' Get FEN string for current position
FUNCTION GetFEN() AS STRING
    DIM rank AS INTEGER
    DIM file AS INTEGER
    DIM sq AS INTEGER
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM emptyCount AS INTEGER
    DIM sb AS Viper.Text.StringBuilder

    sb = NEW Viper.Text.StringBuilder()

    ' Piece placement
    FOR rank = 7 TO 0 STEP -1
        emptyCount = 0
        FOR file = 0 TO 7
            sq = SQ(file, rank)
            piece = GetPiece(sq)
            IF piece = EMPTY THEN
                emptyCount = emptyCount + 1
            ELSE
                IF emptyCount > 0 THEN
                    sb.Append(STR$(emptyCount))
                    emptyCount = 0
                END IF
                clr = GetColor(sq)
                sb.Append(PieceChar(piece, clr))
            END IF
        NEXT file
        IF emptyCount > 0 THEN
            sb.Append(STR$(emptyCount))
        END IF
        IF rank > 0 THEN
            sb.Append("/")
        END IF
    NEXT rank

    ' Side to move
    sb.Append(" ")
    IF gSideToMove = WHITE THEN
        sb.Append("w")
    ELSE
        sb.Append("b")
    END IF

    ' Castling rights
    sb.Append(" ")
    IF gCastling = 0 THEN
        sb.Append("-")
    ELSE
        IF HasFlag(gCastling, CASTLE_WK) = 1 THEN sb.Append("K")
        IF HasFlag(gCastling, CASTLE_WQ) = 1 THEN sb.Append("Q")
        IF HasFlag(gCastling, CASTLE_BK) = 1 THEN sb.Append("k")
        IF HasFlag(gCastling, CASTLE_BQ) = 1 THEN sb.Append("q")
    END IF

    ' En passant
    sb.Append(" ")
    IF gEpSquare < 0 THEN
        sb.Append("-")
    ELSE
        sb.Append(SquareToAlg(gEpSquare))
    END IF

    ' Half-move clock and full move number
    sb.Append(" ")
    sb.Append(STR$(gHalfMoveClock))
    sb.Append(" ")
    sb.Append(STR$(gFullMoveNumber))

    GetFEN = sb.ToString()
END FUNCTION

' Set position from FEN string
SUB SetFEN(fen AS STRING)
    DIM i AS INTEGER
    DIM rank AS INTEGER
    DIM file AS INTEGER
    DIM sq AS INTEGER
    DIM c AS STRING
    DIM piece AS INTEGER
    DIM clr AS INTEGER
    DIM part AS INTEGER
    DIM pos AS INTEGER

    ' Clear board
    FOR i = 0 TO 63
        gBoard(i) = 0
    NEXT i

    rank = 7
    file = 0
    pos = 1
    part = 0

    ' Parse FEN
    FOR i = 1 TO LEN(fen)
        c = MID$(fen, i, 1)

        IF part = 0 THEN
            ' Piece placement
            IF c = "/" THEN
                rank = rank - 1
                file = 0
            ELSEIF c >= "1" AND c <= "8" THEN
                file = file + (ASC(c) - 48)
            ELSEIF c = " " THEN
                part = 1
            ELSE
                sq = SQ(file, rank)
                piece = CharToPiece(c)
                clr = CharToColor(c)
                SetPiece(sq, piece, clr)
                file = file + 1
            END IF
        ELSEIF part = 1 THEN
            ' Side to move
            IF c = "w" THEN
                gSideToMove = WHITE
            ELSEIF c = "b" THEN
                gSideToMove = BLACK
            ELSEIF c = " " THEN
                part = 2
                gCastling = 0
            END IF
        ELSEIF part = 2 THEN
            ' Castling rights
            IF c = "K" THEN
                gCastling = SetFlag(gCastling, CASTLE_WK)
            ELSEIF c = "Q" THEN
                gCastling = SetFlag(gCastling, CASTLE_WQ)
            ELSEIF c = "k" THEN
                gCastling = SetFlag(gCastling, CASTLE_BK)
            ELSEIF c = "q" THEN
                gCastling = SetFlag(gCastling, CASTLE_BQ)
            ELSEIF c = " " THEN
                part = 3
                gEpSquare = -1
            END IF
        ELSEIF part = 3 THEN
            ' En passant
            IF c = "-" THEN
                gEpSquare = -1
            ELSEIF c >= "a" AND c <= "h" THEN
                pos = i
            ELSEIF c >= "1" AND c <= "8" AND pos > 0 THEN
                gEpSquare = AlgToSquare(MID$(fen, pos, 2))
                pos = 0
            ELSEIF c = " " THEN
                part = 4
                gHalfMoveClock = 0
            END IF
        ELSEIF part = 4 THEN
            ' Half-move clock (simplified - just skip)
            IF c = " " THEN
                part = 5
                gFullMoveNumber = 1
            ELSEIF c >= "0" AND c <= "9" THEN
                gHalfMoveClock = gHalfMoveClock * 10 + (ASC(c) - 48)
            END IF
        ELSEIF part = 5 THEN
            ' Full move number
            IF c >= "0" AND c <= "9" THEN
                gFullMoveNumber = gFullMoveNumber * 10 + (ASC(c) - 48)
            END IF
        END IF
    NEXT i

    gPosHistoryCount = 0
END SUB
