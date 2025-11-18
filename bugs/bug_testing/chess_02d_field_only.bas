REM Test field access only (no method calls)

CLASS ChessPiece
    pieceType AS INTEGER
END CLASS

DIM pieces(3) AS ChessPiece

PRINT "Creating pieces..."
pieces(1) = NEW ChessPiece()
pieces(2) = NEW ChessPiece()
pieces(3) = NEW ChessPiece()

PRINT "Setting fields..."
pieces(1).pieceType = 1
pieces(2).pieceType = 2
pieces(3).pieceType = 3

PRINT "Reading fields..."
PRINT "Piece 1 type: "; pieces(1).pieceType
PRINT "Piece 2 type: "; pieces(2).pieceType
PRINT "Piece 3 type: "; pieces(3).pieceType

PRINT "Test passed!"
