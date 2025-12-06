REM Chess Game - Part 3: Array with Workaround for BUG-083
REM Using temporary variables to call methods on array elements

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
        PRINT "  Type:"; ME.pieceType; " Color:"; ME.pieceColor; " @("; ME.rowPos; ","; ME.colPos; ")"
    END SUB

    FUNCTION GetSymbol() AS STRING
        DIM s AS STRING
        SELECT CASE ME.pieceType
            CASE 1
                s = "P"  REM Pawn
            CASE 2
                s = "N"  REM Knight
            CASE 3
                s = "B"  REM Bishop
            CASE 4
                s = "R"  REM Rook
            CASE 5
                s = "Q"  REM Queen
            CASE 6
                s = "K"  REM King
            CASE ELSE
                s = "?"
        END SELECT
        GetSymbol = s
    END FUNCTION
END CLASS

REM Test: Create white's starting pieces
PRINT "Creating White's pieces..."
DIM pieces(16) AS ChessPiece
DIM i AS INTEGER
DIM tempPiece AS ChessPiece

REM Create 8 pawns
FOR i = 1 TO 8
    pieces(i) = NEW ChessPiece()
    tempPiece = pieces(i)  REM Workaround for BUG-083
    tempPiece.Init(1, 0, 2, i)
NEXT i

REM Create special pieces
pieces(9) = NEW ChessPiece()
tempPiece = pieces(9)
tempPiece.Init(4, 0, 1, 1)  REM Rook

pieces(10) = NEW ChessPiece()
tempPiece = pieces(10)
tempPiece.Init(2, 0, 1, 2)  REM Knight

pieces(11) = NEW ChessPiece()
tempPiece = pieces(11)
tempPiece.Init(3, 0, 1, 3)  REM Bishop

pieces(12) = NEW ChessPiece()
tempPiece = pieces(12)
tempPiece.Init(5, 0, 1, 4)  REM Queen

PRINT ""
PRINT "Displaying pieces:"
FOR i = 1 TO 12
    PRINT "Piece"; i; ":";
    tempPiece = pieces(i)
    tempPiece.Display()
NEXT i

PRINT ""
PRINT "Testing GetSymbol function:"
FOR i = 1 TO 12
    tempPiece = pieces(i)
    PRINT tempPiece.GetSymbol(); " ";
NEXT i
PRINT ""

PRINT ""
PRINT "Test 3 passed! Workaround for BUG-083 works!"
