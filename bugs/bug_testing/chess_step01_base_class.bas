REM Chess Game - Step 1: Base Piece Class
REM Testing: Basic OOP, field access, methods

CLASS ChessPiece
    pieceType AS INTEGER    REM 1=Pawn, 2=Rook, 3=Knight, 4=Bishop, 5=Queen, 6=King
    pieceColor AS INTEGER   REM 0=White, 1=Black
    row AS INTEGER
    col AS INTEGER
    hasMoved AS INTEGER     REM 0=false, 1=true (for castling/pawn moves)

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.row = r
        ME.col = c
        ME.hasMoved = 0
        PRINT "Piece initialized: type="; pType; " color="; pColor; " pos=("; row; ","; col; ")"
    END SUB

    FUNCTION GetSymbol() AS STRING
        REM Return chess symbols based on piece type
        DIM symbol AS STRING
        SELECT CASE ME.pieceType
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

    SUB Display()
        PRINT ME.GetSymbol(); " at ("; ME.row; ","; ME.col; ")"
    END SUB
END CLASS

REM Test the base class
PRINT "=== Chess Piece Base Class Test ==="
PRINT ""

DIM whitePawn AS ChessPiece
DIM blackRook AS ChessPiece
DIM whiteKing AS ChessPiece

whitePawn = NEW ChessPiece()
whitePawn.Init(1, 0, 2, 1)
PRINT "White Pawn symbol: "; whitePawn.GetSymbol()
whitePawn.Display()

PRINT ""

blackRook = NEW ChessPiece()
blackRook.Init(2, 1, 8, 1)
PRINT "Black Rook symbol: "; blackRook.GetSymbol()
blackRook.Display()

PRINT ""

whiteKing = NEW ChessPiece()
whiteKing.Init(6, 0, 1, 5)
PRINT "White King symbol: "; whiteKing.GetSymbol()
whiteKing.Display()

PRINT ""
PRINT "Step 1 Complete: Base class works!"
