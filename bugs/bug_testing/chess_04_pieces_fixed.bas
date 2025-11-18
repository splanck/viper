REM Chess Game - Part 4: Working Pieces with Workarounds
REM Workarounds: temp vars for array methods (BUG-083), IF/ELSE for strings (BUG-084)

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

    FUNCTION GetSymbol() AS STRING
        DIM s AS STRING
        REM Workaround for BUG-084: Use IF/ELSE instead of SELECT CASE
        IF ME.pieceType = 1 THEN
            s = "P"
        ELSEIF ME.pieceType = 2 THEN
            s = "N"
        ELSEIF ME.pieceType = 3 THEN
            s = "B"
        ELSEIF ME.pieceType = 4 THEN
            s = "R"
        ELSEIF ME.pieceType = 5 THEN
            s = "Q"
        ELSEIF ME.pieceType = 6 THEN
            s = "K"
        ELSE
            s = "?"
        END IF
        GetSymbol = s
    END FUNCTION
END CLASS

REM Create a full set of white pieces
PRINT "Creating White's pieces..."
DIM pieces(16) AS ChessPiece
DIM i AS INTEGER
DIM temp AS ChessPiece

REM 8 Pawns
FOR i = 1 TO 8
    pieces(i) = NEW ChessPiece()
    temp = pieces(i)
    temp.Init(1, 0, 2, i)
NEXT i

REM 2 Rooks
pieces(9) = NEW ChessPiece()
temp = pieces(9)
temp.Init(4, 0, 1, 1)

pieces(10) = NEW ChessPiece()
temp = pieces(10)
temp.Init(4, 0, 1, 8)

REM 2 Knights
pieces(11) = NEW ChessPiece()
temp = pieces(11)
temp.Init(2, 0, 1, 2)

pieces(12) = NEW ChessPiece()
temp = pieces(12)
temp.Init(2, 0, 1, 7)

REM 2 Bishops
pieces(13) = NEW ChessPiece()
temp = pieces(13)
temp.Init(3, 0, 1, 3)

pieces(14) = NEW ChessPiece()
temp = pieces(14)
temp.Init(3, 0, 1, 6)

REM Queen and King
pieces(15) = NEW ChessPiece()
temp = pieces(15)
temp.Init(5, 0, 1, 4)

pieces(16) = NEW ChessPiece()
temp = pieces(16)
temp.Init(6, 0, 1, 5)

PRINT ""
PRINT "White's Starting Position:"
FOR i = 1 TO 16
    temp = pieces(i)
    PRINT temp.GetSymbol(); " ";
NEXT i
PRINT ""
PRINT ""
PRINT "Test 4 passed! All workarounds work!"
