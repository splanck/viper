REM === Chess Engine v2 - Procedural Design (BUG-089/090 workaround) ===
REM Using integer arrays instead of object arrays to avoid OOP limitations

REM Piece types
CONST EMPTY = 0
CONST PAWN = 1
CONST KNIGHT = 2
CONST BISHOP = 3
CONST ROOK = 4
CONST QUEEN = 5
CONST KING = 6

REM Colors
CONST WHITE = 0
CONST BLACK = 1

REM Board represented as two flat integer arrays
REM board_type(64) - piece type at each square (0-6)
REM board_color(64) - piece color at each square (0-1)
DIM board_type(64) AS INTEGER
DIM board_color(64) AS INTEGER
DIM board_moved(64) AS INTEGER  REM Has piece moved (for castling/pawn)

SUB InitBoard()
    DIM i AS INTEGER
    FOR i = 1 TO 64
        board_type(i) = EMPTY
        board_color(i) = WHITE
        board_moved(i) = 0
    NEXT i
END SUB

SUB SetPiece(row AS INTEGER, col AS INTEGER, pType AS INTEGER, pColor AS INTEGER)
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    board_type(idx) = pType
    board_color(idx) = pColor
    board_moved(idx) = 0
END SUB

FUNCTION GetPieceType(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    GetPieceType = board_type(idx)
END FUNCTION

FUNCTION GetPieceColor(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    GetPieceColor = board_color(idx)
END FUNCTION

FUNCTION GetPieceSymbol(row AS INTEGER, col AS INTEGER) AS STRING
    DIM pType AS INTEGER
    DIM pColor AS INTEGER
    DIM symbol AS STRING

    pType = GetPieceType(row, col)
    pColor = GetPieceColor(row, col)

    SELECT CASE pType
        CASE 1  REM PAWN
            IF pColor = 0 THEN symbol = "P" ELSE symbol = "p"
        CASE 2  REM KNIGHT
            IF pColor = 0 THEN symbol = "N" ELSE symbol = "n"
        CASE 3  REM BISHOP
            IF pColor = 0 THEN symbol = "B" ELSE symbol = "b"
        CASE 4  REM ROOK
            IF pColor = 0 THEN symbol = "R" ELSE symbol = "r"
        CASE 5  REM QUEEN
            IF pColor = 0 THEN symbol = "Q" ELSE symbol = "q"
        CASE 6  REM KING
            IF pColor = 0 THEN symbol = "K" ELSE symbol = "k"
        CASE ELSE
            symbol = " "
    END SELECT

    GetPieceSymbol = symbol
END FUNCTION

SUB SetupInitialPosition()
    REM White pieces (row 1)
    SetPiece(1, 1, 4, 0)  REM Rook
    SetPiece(1, 2, 2, 0)  REM Knight
    SetPiece(1, 3, 3, 0)  REM Bishop
    SetPiece(1, 4, 5, 0)  REM Queen
    SetPiece(1, 5, 6, 0)  REM King
    SetPiece(1, 6, 3, 0)  REM Bishop
    SetPiece(1, 7, 2, 0)  REM Knight
    SetPiece(1, 8, 4, 0)  REM Rook

    REM White pawns (row 2)
    DIM col AS INTEGER
    FOR col = 1 TO 8
        SetPiece(2, col, 1, 0)
    NEXT col

    REM Black pawns (row 7)
    FOR col = 1 TO 8
        SetPiece(7, col, 1, 1)
    NEXT col

    REM Black pieces (row 8)
    SetPiece(8, 1, 4, 1)  REM Rook
    SetPiece(8, 2, 2, 1)  REM Knight
    SetPiece(8, 3, 3, 1)  REM Bishop
    SetPiece(8, 4, 5, 1)  REM Queen
    SetPiece(8, 5, 6, 1)  REM King
    SetPiece(8, 6, 3, 1)  REM Bishop
    SetPiece(8, 7, 2, 1)  REM Knight
    SetPiece(8, 8, 4, 1)  REM Rook
END SUB

SUB DisplayBoard()
    DIM row AS INTEGER
    DIM col AS INTEGER

    PRINT ""
    PRINT "  a b c d e f g h"
    PRINT ""

    FOR row = 8 TO 1 STEP -1
        PRINT row; " ";
        FOR col = 1 TO 8
            PRINT GetPieceSymbol(row, col); " ";
        NEXT col
        PRINT row
    NEXT row

    PRINT ""
    PRINT "  a b c d e f g h"
    PRINT ""
END SUB

REM Test the board
PRINT "=== Chess Engine v2 - Procedural Design ==="
PRINT ""

InitBoard()
PRINT "Empty board:"
DisplayBoard()

PRINT "Setting up initial position..."
SetupInitialPosition()
DisplayBoard()

PRINT "Chess board working! Workaround for BUG-089/090 successful."
