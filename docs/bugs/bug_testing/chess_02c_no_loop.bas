REM Test without FOR loop

CLASS ChessPiece
    pieceType AS INTEGER

    SUB Init(pType AS INTEGER)
        ME.pieceType = pType
        PRINT "Init called with type "; pType
    END SUB
END CLASS

DIM pieces(3) AS ChessPiece

PRINT "Creating pieces..."
pieces(1) = NEW ChessPiece()
pieces(2) = NEW ChessPiece()
pieces(3) = NEW ChessPiece()

PRINT "Calling Init..."
pieces(1).Init(1)
pieces(2).Init(2)
pieces(3).Init(3)

PRINT "Test passed!"
