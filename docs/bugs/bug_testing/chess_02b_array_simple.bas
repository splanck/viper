REM Simpler test - just create and access array elements

CLASS ChessPiece
    pieceType AS INTEGER

    SUB Init(pType AS INTEGER)
        ME.pieceType = pType
        PRINT "Init called with type "; pType
    END SUB
END CLASS

DIM pieces(3) AS ChessPiece
DIM i AS INTEGER

PRINT "Test: Creating pieces in array..."
FOR i = 1 TO 3
    pieces(i) = NEW ChessPiece()
NEXT i

PRINT "Test: Calling Init on array elements..."
FOR i = 1 TO 3
    pieces(i).Init(i)
NEXT i

PRINT "Test passed!"
