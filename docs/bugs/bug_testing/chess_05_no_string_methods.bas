REM Chess Game - Part 5: Avoiding String FUNCTION Methods (BUG-084 workaround)
REM Workaround: Use module-level functions for string operations

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
END CLASS

REM Module-level function for getting piece symbol (BUG-084 workaround)
FUNCTION GetPieceSymbol(pType AS INTEGER) AS STRING
    DIM s AS STRING
    IF pType = 1 THEN
        s = "P"
    ELSEIF pType = 2 THEN
        s = "N"
    ELSEIF pType = 3 THEN
        s = "B"
    ELSEIF pType = 4 THEN
        s = "R"
    ELSEIF pType = 5 THEN
        s = "Q"
    ELSEIF pType = 6 THEN
        s = "K"
    ELSE
        s = "?"
    END IF
    GetPieceSymbol = s
END FUNCTION

REM Create white pieces
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

REM 2 Rooks, 2 Knights, 2 Bishops
pieces(9) = NEW ChessPiece()
temp = pieces(9)
temp.Init(4, 0, 1, 1)

pieces(10) = NEW ChessPiece()
temp = pieces(10)
temp.Init(4, 0, 1, 8)

pieces(11) = NEW ChessPiece()
temp = pieces(11)
temp.Init(2, 0, 1, 2)

pieces(12) = NEW ChessPiece()
temp = pieces(12)
temp.Init(2, 0, 1, 7)

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
    PRINT GetPieceSymbol(temp.pieceType); " ";
NEXT i
PRINT ""
PRINT ""
PRINT "Test 5 passed! Module-level string functions work!"
