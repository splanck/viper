REM === Chess Engine v2 - Piece Classes ===
REM Stress testing: OOP, inheritance, arrays, string handling

REM Piece types as constants
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

REM Base Piece class
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

    FUNCTION GetRow() AS INTEGER
        GetRow = ME.pieceRow
    END FUNCTION

    FUNCTION GetCol() AS INTEGER
        GetCol = ME.pieceCol
    END FUNCTION

    SUB MoveTo(r AS INTEGER, c AS INTEGER)
        ME.pieceRow = r
        ME.pieceCol = c
        ME.hasMoved = 1
    END SUB

    FUNCTION HasMoved() AS INTEGER
        HasMoved = ME.hasMoved
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        REM Chess piece symbols
        DIM symbol AS STRING
        SELECT CASE ME.pieceType
            CASE 1  REM PAWN
                IF ME.pieceColor = 0 THEN
                    symbol = "P"
                ELSE
                    symbol = "p"
                END IF
            CASE 2  REM KNIGHT
                IF ME.pieceColor = 0 THEN
                    symbol = "N"
                ELSE
                    symbol = "n"
                END IF
            CASE 3  REM BISHOP
                IF ME.pieceColor = 0 THEN
                    symbol = "B"
                ELSE
                    symbol = "b"
                END IF
            CASE 4  REM ROOK
                IF ME.pieceColor = 0 THEN
                    symbol = "R"
                ELSE
                    symbol = "r"
                END IF
            CASE 5  REM QUEEN
                IF ME.pieceColor = 0 THEN
                    symbol = "Q"
                ELSE
                    symbol = "q"
                END IF
            CASE 6  REM KING
                IF ME.pieceColor = 0 THEN
                    symbol = "K"
                ELSE
                    symbol = "k"
                END IF
            CASE ELSE
                symbol = " "
        END SELECT
        GetSymbol = symbol
    END FUNCTION
END CLASS

REM Test the piece class
PRINT "=== Piece Class Test ==="
PRINT ""

DIM p AS Piece
p = NEW Piece()
PRINT "Piece created"
p.Init(QUEEN, WHITE, 0, 3)
PRINT "Piece initialized"
PRINT "Type: "; p.GetType()
PRINT "Color: "; p.GetColor()
PRINT "Position: ("; p.GetRow(); ","; p.GetCol(); ")"
PRINT "Symbol: "; p.GetSymbol()

PRINT ""
PRINT "Test Complete!"
