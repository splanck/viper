REM Test without FOR loop in PRINT

CLASS ChessPiece
    pieceType AS INTEGER
END CLASS

DIM pieces(3) AS ChessPiece

pieces(1) = NEW ChessPiece()
pieces(2) = NEW ChessPiece()
pieces(3) = NEW ChessPiece()

pieces(1).pieceType = 10
pieces(2).pieceType = 20
pieces(3).pieceType = 30

PRINT "Piece 1: "; pieces(1).pieceType
PRINT "Piece 2: "; pieces(2).pieceType
PRINT "Piece 3: "; pieces(3).pieceType

PRINT "Test passed!"
