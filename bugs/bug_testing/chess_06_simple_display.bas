REM Super simple test - just create and display pieces

CLASS ChessPiece
    pieceType AS INTEGER

    SUB Init(pType AS INTEGER)
        ME.pieceType = pType
    END SUB
END CLASS

DIM pieces(3) AS ChessPiece
DIM temp AS ChessPiece

PRINT "Creating pieces..."
pieces(1) = NEW ChessPiece()
pieces(2) = NEW ChessPiece()
pieces(3) = NEW ChessPiece()

PRINT "Initializing..."
temp = pieces(1)
temp.Init(1)

temp = pieces(2)
temp.Init(2)

temp = pieces(3)
temp.Init(3)

PRINT "Reading types:"
temp = pieces(1)
PRINT "Piece 1: "; temp.pieceType

temp = pieces(2)
PRINT "Piece 2: "; temp.pieceType

temp = pieces(3)
PRINT "Piece 3: "; temp.pieceType

PRINT "Done!"
