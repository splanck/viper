REM Chess Game - Part 1: Basic Piece Class
REM Testing: OOP basics, class creation, simple methods

CLASS ChessPiece
    pieceType AS INTEGER   REM 1=Pawn, 2=Knight, 3=Bishop, 4=Rook, 5=Queen, 6=King
    pieceColor AS INTEGER  REM 0=White, 1=Black
    rowPos AS INTEGER
    colPos AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.rowPos = r
        ME.colPos = c
        PRINT "Created piece type "; pType; " at ("; r; ","; c; ")"
    END SUB

    SUB Display()
        PRINT "Piece: type="; ME.pieceType; " color="; ME.pieceColor; " pos=("; ME.rowPos; ","; ME.colPos; ")"
    END SUB
END CLASS

REM Test 1: Create a simple piece
DIM piece AS ChessPiece
piece = NEW ChessPiece()
piece.Init(1, 0, 2, 1)
piece.Display()

PRINT "Test 1 passed!"
