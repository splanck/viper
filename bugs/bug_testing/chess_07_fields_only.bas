REM Test with pure field access - no methods at all

CLASS ChessPiece
    pieceType AS INTEGER
    rowPos AS INTEGER
    colPos AS INTEGER
END CLASS

DIM pieces(8) AS ChessPiece
DIM i AS INTEGER

PRINT "Creating 8 pieces with field-only access..."
FOR i = 1 TO 8
    pieces(i) = NEW ChessPiece()
    pieces(i).pieceType = 1
    pieces(i).rowPos = 2
    pieces(i).colPos = i
NEXT i

PRINT "Displaying pieces:"
FOR i = 1 TO 8
    PRINT "Piece "; i; ": type="; pieces(i).pieceType; " pos=("; pieces(i).rowPos; ","; pieces(i).colPos; ")"
NEXT i

PRINT ""
PRINT "Test passed! Field-only access works!"
