'==============================================================================
' CHESS CONSTANTS
' Core constants for piece types, colors, squares, and game state
'==============================================================================

' Piece types (0 = empty)
CONST EMPTY AS INTEGER = 0
CONST PAWN AS INTEGER = 1
CONST KNIGHT AS INTEGER = 2
CONST BISHOP AS INTEGER = 3
CONST ROOK AS INTEGER = 4
CONST QUEEN AS INTEGER = 5
CONST KING AS INTEGER = 6

' Colors
CONST WHITE AS INTEGER = 0
CONST BLACK AS INTEGER = 1

' Piece values for evaluation (centipawns)
CONST PAWN_VALUE AS INTEGER = 100
CONST KNIGHT_VALUE AS INTEGER = 320
CONST BISHOP_VALUE AS INTEGER = 330
CONST ROOK_VALUE AS INTEGER = 500
CONST QUEEN_VALUE AS INTEGER = 900
CONST KING_VALUE AS INTEGER = 20000

' Castling flags (bitwise)
CONST CASTLE_WK AS INTEGER = 1   ' White kingside
CONST CASTLE_WQ AS INTEGER = 2   ' White queenside
CONST CASTLE_BK AS INTEGER = 4   ' Black kingside
CONST CASTLE_BQ AS INTEGER = 8   ' Black queenside

' Move flags
CONST MOVE_NORMAL AS INTEGER = 0
CONST MOVE_CAPTURE AS INTEGER = 1
CONST MOVE_CASTLE_KS AS INTEGER = 2
CONST MOVE_CASTLE_QS AS INTEGER = 3
CONST MOVE_EP AS INTEGER = 4
CONST MOVE_PROMOTION AS INTEGER = 5
CONST MOVE_DOUBLE_PAWN AS INTEGER = 6

' Game state
CONST STATE_PLAYING AS INTEGER = 0
CONST STATE_CHECKMATE AS INTEGER = 1
CONST STATE_STALEMATE AS INTEGER = 2
CONST STATE_DRAW_50 AS INTEGER = 3
CONST STATE_DRAW_REPETITION AS INTEGER = 4
CONST STATE_DRAW_MATERIAL AS INTEGER = 5
CONST STATE_RESIGNED AS INTEGER = 6

' Search constants
CONST INFINITY AS INTEGER = 30000
CONST MATE_SCORE AS INTEGER = 29000
CONST MAX_DEPTH AS INTEGER = 64
CONST MAX_MOVES AS INTEGER = 256

' Transposition table flags
CONST TT_EXACT AS INTEGER = 0
CONST TT_ALPHA AS INTEGER = 1
CONST TT_BETA AS INTEGER = 2

' ANSI color codes for display
CONST ESC AS STRING = CHR$(27)

' Square indices (a1=0, h8=63)
' Files: a=0, b=1, c=2, d=3, e=4, f=5, g=6, h=7
' Ranks: 1=0, 2=1, 3=2, 4=3, 5=4, 6=5, 7=6, 8=7
' Square = rank * 8 + file

' Helper function to get square from file and rank
FUNCTION SQ(file AS INTEGER, rank AS INTEGER) AS INTEGER
    SQ = rank * 8 + file
END FUNCTION

' Helper function to get file from square
FUNCTION FILE_OF(sq AS INTEGER) AS INTEGER
    FILE_OF = sq MOD 8
END FUNCTION

' Helper function to get rank from square
FUNCTION RANK_OF(sq AS INTEGER) AS INTEGER
    RANK_OF = sq \ 8
END FUNCTION

' Check if square is valid
FUNCTION VALID_SQ(sq AS INTEGER) AS INTEGER
    IF sq >= 0 AND sq < 64 THEN
        VALID_SQ = 1
    ELSE
        VALID_SQ = 0
    END IF
END FUNCTION

' Get piece value
FUNCTION GetPieceValue(pieceType AS INTEGER) AS INTEGER
    SELECT CASE pieceType
        CASE PAWN
            GetPieceValue = PAWN_VALUE
        CASE KNIGHT
            GetPieceValue = KNIGHT_VALUE
        CASE BISHOP
            GetPieceValue = BISHOP_VALUE
        CASE ROOK
            GetPieceValue = ROOK_VALUE
        CASE QUEEN
            GetPieceValue = QUEEN_VALUE
        CASE KING
            GetPieceValue = KING_VALUE
        CASE ELSE
            GetPieceValue = 0
    END SELECT
END FUNCTION

' Convert square to algebraic notation (e.g., 0 -> "a1", 63 -> "h8")
FUNCTION SquareToAlg(sq AS INTEGER) AS STRING
    DIM f AS INTEGER
    DIM r AS INTEGER
    f = FILE_OF(sq)
    r = RANK_OF(sq)
    SquareToAlg = CHR$(97 + f) + CHR$(49 + r)
END FUNCTION

' Convert algebraic notation to square (e.g., "a1" -> 0, "h8" -> 63)
FUNCTION AlgToSquare(alg AS STRING) AS INTEGER
    DIM f AS INTEGER
    DIM r AS INTEGER
    IF LEN(alg) < 2 THEN
        AlgToSquare = -1
        EXIT FUNCTION
    END IF
    f = ASC(LEFT$(alg, 1)) - 97
    r = ASC(MID$(alg, 2, 1)) - 49
    IF f < 0 OR f > 7 OR r < 0 OR r > 7 THEN
        AlgToSquare = -1
    ELSE
        AlgToSquare = SQ(f, r)
    END IF
END FUNCTION

' Get piece character for display
FUNCTION PieceChar(pieceType AS INTEGER, pieceColor AS INTEGER) AS STRING
    DIM c AS STRING
    SELECT CASE pieceType
        CASE PAWN
            c = "P"
        CASE KNIGHT
            c = "N"
        CASE BISHOP
            c = "B"
        CASE ROOK
            c = "R"
        CASE QUEEN
            c = "Q"
        CASE KING
            c = "K"
        CASE ELSE
            c = "."
    END SELECT
    IF pieceColor = BLACK AND pieceType <> EMPTY THEN
        c = LCASE$(c)
    END IF
    PieceChar = c
END FUNCTION

' Parse piece character to type (P/p=pawn, etc.)
FUNCTION CharToPiece(c AS STRING) AS INTEGER
    DIM u AS STRING
    u = UCASE$(c)
    SELECT CASE u
        CASE "P"
            CharToPiece = PAWN
        CASE "N"
            CharToPiece = KNIGHT
        CASE "B"
            CharToPiece = BISHOP
        CASE "R"
            CharToPiece = ROOK
        CASE "Q"
            CharToPiece = QUEEN
        CASE "K"
            CharToPiece = KING
        CASE ELSE
            CharToPiece = EMPTY
    END SELECT
END FUNCTION

' Get color from piece character (lowercase = black)
FUNCTION CharToColor(c AS STRING) AS INTEGER
    IF c >= "a" THEN
        IF c <= "z" THEN
            CharToColor = BLACK
            EXIT FUNCTION
        END IF
    END IF
    CharToColor = WHITE
END FUNCTION

'==============================================================================
' BITWISE OPERATION HELPERS
' Since Viper BASIC doesn't support bitwise AND/OR/XOR for integers,
' we implement them using arithmetic operations.
'==============================================================================

' Bitwise AND for values 0-255 (8-bit)
FUNCTION BitAnd(a AS INTEGER, b AS INTEGER) AS INTEGER
    DIM result AS INTEGER
    DIM bitA AS INTEGER
    DIM bitB AS INTEGER
    DIM power AS INTEGER
    DIM i AS INTEGER

    result = 0
    power = 1
    FOR i = 0 TO 7
        bitA = (a \ power) MOD 2
        bitB = (b \ power) MOD 2
        IF bitA = 1 THEN
            IF bitB = 1 THEN
                result = result + power
            END IF
        END IF
        power = power * 2
    NEXT i
    BitAnd = result
END FUNCTION

' Bitwise OR for values 0-255 (8-bit)
FUNCTION BitOr(a AS INTEGER, b AS INTEGER) AS INTEGER
    DIM result AS INTEGER
    DIM bitA AS INTEGER
    DIM bitB AS INTEGER
    DIM power AS INTEGER
    DIM i AS INTEGER

    result = 0
    power = 1
    FOR i = 0 TO 7
        bitA = (a \ power) MOD 2
        bitB = (b \ power) MOD 2
        IF bitA = 1 THEN
            result = result + power
        ELSEIF bitB = 1 THEN
            result = result + power
        END IF
        power = power * 2
    NEXT i
    BitOr = result
END FUNCTION

' Test if a specific bit is set (power of 2)
FUNCTION HasFlag(flags AS INTEGER, flag AS INTEGER) AS INTEGER
    ' For single-bit flags (1, 2, 4, 8, ...), use MOD trick
    DIM masked AS INTEGER
    masked = (flags \ flag) MOD 2
    HasFlag = masked
END FUNCTION

' Clear a specific bit flag
FUNCTION ClearFlag(flags AS INTEGER, flag AS INTEGER) AS INTEGER
    IF HasFlag(flags, flag) = 1 THEN
        ClearFlag = flags - flag
    ELSE
        ClearFlag = flags
    END IF
END FUNCTION

' Set a specific bit flag
FUNCTION SetFlag(flags AS INTEGER, flag AS INTEGER) AS INTEGER
    IF HasFlag(flags, flag) = 0 THEN
        SetFlag = flags + flag
    ELSE
        SetFlag = flags
    END IF
END FUNCTION
