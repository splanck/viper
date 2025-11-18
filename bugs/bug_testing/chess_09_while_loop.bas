REM Test WHILE loop workaround for BUG-085

CLASS ChessPiece
    pieceType AS INTEGER
END CLASS

DIM pieces(5) AS ChessPiece
DIM i AS INTEGER

PRINT "Creating pieces with WHILE loop..."
i = 1
DO WHILE i <= 5
    pieces(i) = NEW ChessPiece()
    pieces(i).pieceType = i * 10
    i = i + 1
LOOP

PRINT "Displaying pieces with WHILE loop..."
i = 1
DO WHILE i <= 5
    PRINT "Piece "; i; ": type = "; pieces(i).pieceType
    i = i + 1
LOOP

PRINT ""
PRINT "WHILE loop workaround works!"
