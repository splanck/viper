REM Chess Game - Part 2: Test Arrays of Pieces
REM Testing: Arrays of objects, object field access in arrays

CLASS ChessPiece
    pieceType AS INTEGER
    pieceColor AS INTEGER
    rowPos AS INTEGER
    colPos AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.rowPos = r
        ME.colPos = c
    END SUB

    SUB Display()
        PRINT "  [Type:"; ME.pieceType; " Color:"; ME.pieceColor; " Pos:("; ME.rowPos; ","; ME.colPos; ")]"
    END SUB
END CLASS

REM Test 2: Create array of pieces (like a chess player's pieces)
PRINT "Creating array of 8 pieces..."
DIM pieces(8) AS ChessPiece
DIM i AS INTEGER

REM Initialize pawns
FOR i = 1 TO 8
    pieces(i) = NEW ChessPiece()
    pieces(i).Init(1, 0, 2, i)
    PRINT "Piece "; i; ":";
    pieces(i).Display()
NEXT i

PRINT ""
PRINT "Test 2 passed! Arrays of objects work!"
