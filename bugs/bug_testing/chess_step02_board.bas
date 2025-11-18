REM Chess Game - Step 2: Board with 2D Array
REM Testing: Arrays of objects, nested loops, array initialization

CLASS ChessPiece
    pieceType AS INTEGER
    pieceColor AS INTEGER
    row AS INTEGER
    col AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.row = r
        ME.col = c
    END SUB

    FUNCTION GetSymbol() AS STRING
        DIM symbol AS STRING
        SELECT CASE ME.pieceType
            CASE 0
                symbol = "."  REM Empty
            CASE 1
                symbol = "P"
            CASE 2
                symbol = "R"
            CASE 3
                symbol = "N"
            CASE 4
                symbol = "B"
            CASE 5
                symbol = "Q"
            CASE 6
                symbol = "K"
            CASE ELSE
                symbol = "?"
        END SELECT
        GetSymbol = symbol
    END FUNCTION
END CLASS

REM Initialize 8x8 board - NOTE: Using 1-based indexing (1-8)
DIM board(64) AS ChessPiece  REM Flattened 2D array: board[row * 8 + col]
DIM r AS INTEGER
DIM c AS INTEGER
DIM idx AS INTEGER

PRINT "=== Initializing Chess Board ==="
PRINT ""

REM Initialize all squares as empty
FOR r = 1 TO 8
    FOR c = 1 TO 8
        idx = (r - 1) * 8 + c
        board(idx) = NEW ChessPiece()
        board(idx).Init(0, 0, r, c)  REM Type 0 = empty
    NEXT c
NEXT r

PRINT "Board initialized (all empty)"
PRINT ""

REM Place some pieces
PRINT "Placing white rooks..."
idx = (1 - 1) * 8 + 1  REM Row 1, Col 1
board(idx).Init(2, 0, 1, 1)

idx = (1 - 1) * 8 + 8  REM Row 1, Col 8
board(idx).Init(2, 0, 1, 8)

PRINT "Placing white king..."
idx = (1 - 1) * 8 + 5  REM Row 1, Col 5
board(idx).Init(6, 0, 1, 5)

PRINT "Placing black pawns..."
FOR c = 1 TO 8
    idx = (7 - 1) * 8 + c  REM Row 7
    board(idx).Init(1, 1, 7, c)
NEXT c

PRINT ""
PRINT "=== Simple Board Display ==="
FOR r = 8 TO 1 STEP -1
    PRINT r; " ";
    FOR c = 1 TO 8
        idx = (r - 1) * 8 + c
        PRINT board(idx).GetSymbol(); " ";
    NEXT c
    PRINT ""
NEXT r
PRINT "  a b c d e f g h"

PRINT ""
PRINT "Step 2 Complete: Board with pieces works!"
