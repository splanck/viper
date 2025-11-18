REM === Chess Engine v2 - Board Class ===
REM Stress testing: 2D arrays, ANSI colors, complex loops

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

REM ANSI color codes
CONST ANSI_RESET = 0
CONST ANSI_WHITE_BG = 47
CONST ANSI_BLACK_BG = 40
CONST ANSI_WHITE_FG = 97
CONST ANSI_BLACK_FG = 30

CLASS Piece
    pieceType AS INTEGER
    pieceColor AS INTEGER
    pieceRow AS INTEGER
    pieceCol AS INTEGER
    hasMoved AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.pieceRow = r
        ME.pieceCol = c
        ME.hasMoved = 0
    END SUB

    FUNCTION GetType() AS INTEGER
        GetType = ME.pieceType
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        GetColor = ME.pieceColor
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        REM Chess piece symbols
        DIM symbol AS STRING
        SELECT CASE ME.pieceType
            CASE 1  REM PAWN
                IF ME.pieceColor = 0 THEN symbol = "P" ELSE symbol = "p"
            CASE 2  REM KNIGHT
                IF ME.pieceColor = 0 THEN symbol = "N" ELSE symbol = "n"
            CASE 3  REM BISHOP
                IF ME.pieceColor = 0 THEN symbol = "B" ELSE symbol = "b"
            CASE 4  REM ROOK
                IF ME.pieceColor = 0 THEN symbol = "R" ELSE symbol = "r"
            CASE 5  REM QUEEN
                IF ME.pieceColor = 0 THEN symbol = "Q" ELSE symbol = "q"
            CASE 6  REM KING
                IF ME.pieceColor = 0 THEN symbol = "K" ELSE symbol = "k"
            CASE ELSE
                symbol = " "
        END SELECT
        GetSymbol = symbol
    END FUNCTION
END CLASS

REM Module-level functions to work around BUG-089
REM (Cannot call methods on object array fields from class methods)

SUB InitPiecesArray(pieces() AS Piece)
    DIM i AS INTEGER
    FOR i = 1 TO 64
        pieces(i) = NEW Piece()
        pieces(i).Init(0, 0, 0, 0)
    NEXT i
END SUB

SUB SetPieceInArray(pieces() AS Piece, row AS INTEGER, col AS INTEGER, pType AS INTEGER, pColor AS INTEGER)
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    pieces(idx).Init(pType, pColor, row, col)
END SUB

FUNCTION GetPieceFromArray(pieces() AS Piece, row AS INTEGER, col AS INTEGER) AS Piece
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    GetPieceFromArray = pieces(idx)
END FUNCTION

CLASS Board
    REM 8x8 board - using flat array indexed as (row-1)*8 + (col-1)
    DIM pieces(64) AS Piece

    SUB InitBoard()
        InitPiecesArray(pieces)
    END SUB

    SUB SetupInitialPosition()
        REM White pieces (row 1)
        SetPieceInArray(pieces, 1, 1, 4, 0)  REM Rook
        SetPieceInArray(pieces, 1, 2, 2, 0)  REM Knight
        SetPieceInArray(pieces, 1, 3, 3, 0)  REM Bishop
        SetPieceInArray(pieces, 1, 4, 5, 0)  REM Queen
        SetPieceInArray(pieces, 1, 5, 6, 0)  REM King
        SetPieceInArray(pieces, 1, 6, 3, 0)  REM Bishop
        SetPieceInArray(pieces, 1, 7, 2, 0)  REM Knight
        SetPieceInArray(pieces, 1, 8, 4, 0)  REM Rook

        REM White pawns (row 2)
        DIM col AS INTEGER
        FOR col = 1 TO 8
            SetPieceInArray(pieces, 2, col, 1, 0)
        NEXT col

        REM Black pawns (row 7)
        FOR col = 1 TO 8
            SetPieceInArray(pieces, 7, col, 1, 1)
        NEXT col

        REM Black pieces (row 8)
        SetPieceInArray(pieces, 8, 1, 4, 1)  REM Rook
        SetPieceInArray(pieces, 8, 2, 2, 1)  REM Knight
        SetPieceInArray(pieces, 8, 3, 3, 1)  REM Bishop
        SetPieceInArray(pieces, 8, 4, 5, 1)  REM Queen
        SetPieceInArray(pieces, 8, 5, 6, 1)  REM King
        SetPieceInArray(pieces, 8, 6, 3, 1)  REM Bishop
        SetPieceInArray(pieces, 8, 7, 2, 1)  REM Knight
        SetPieceInArray(pieces, 8, 8, 4, 1)  REM Rook
    END SUB

    SUB Display()
        DIM row AS INTEGER
        DIM col AS INTEGER

        PRINT ""
        PRINT "  a b c d e f g h"
        PRINT ""

        FOR row = 8 TO 1 STEP -1
            PRINT row; " ";
            FOR col = 1 TO 8
                DIM p AS Piece
                p = GetPieceFromArray(pieces, row, col)
                PRINT p.GetSymbol(); " ";
            NEXT col
            PRINT row
        NEXT row

        PRINT ""
        PRINT "  a b c d e f g h"
        PRINT ""
    END SUB
END CLASS

REM Test the board
PRINT "=== Chess Board Test ==="
PRINT ""

DIM board AS Board
board = NEW Board()
board.InitBoard()
PRINT "Empty board initialized"
board.Display()

PRINT ""
PRINT "Setting up initial position..."
board.SetupInitialPosition()
board.Display()

PRINT "Board test complete!"
