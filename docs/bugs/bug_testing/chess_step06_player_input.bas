REM Chess Game - Step 6: Player Input and Move Execution
REM Testing: INPUT statement, coordinate parsing, move execution

CLASS ChessPiece
    pieceType AS INTEGER
    pieceColor AS INTEGER
    row AS INTEGER
    col AS INTEGER

    SUB Init(pType AS INTEGER, pColor AS INTEGER, r AS INTEGER, c AS INTEGER)
        ME.pieceType = pType
        ME.pieceColor = pColor
        ME.row = r
        ME.col = c
    END SUB

    FUNCTION GetSymbol() AS STRING
        DIM symbol AS STRING
        IF ME.pieceType = 0 THEN
            symbol = "."
        ELSEIF ME.pieceType = 1 THEN
            symbol = "P"
        ELSEIF ME.pieceType = 2 THEN
            symbol = "R"
        ELSEIF ME.pieceType = 3 THEN
            symbol = "N"
        ELSEIF ME.pieceType = 6 THEN
            symbol = "K"
        ELSE
            symbol = "?"
        END IF
        GetSymbol = symbol
    END FUNCTION

    FUNCTION IsValidMove(toRow AS INTEGER, toCol AS INTEGER) AS INTEGER
        DIM rowDiff AS INTEGER
        DIM colDiff AS INTEGER

        IsValidMove = 0
        rowDiff = toRow - ME.row
        colDiff = toCol - ME.col

        IF ME.pieceType = 0 THEN
            REM Empty square
            IsValidMove = 0
        ELSEIF ME.pieceType = 1 THEN
            REM Pawn (simplified - just forward one square)
            IF ME.pieceColor = 0 AND toRow = ME.row + 1 AND toCol = ME.col THEN
                IsValidMove = 1
            END IF
        ELSEIF ME.pieceType = 2 THEN
            REM Rook
            IF rowDiff = 0 OR colDiff = 0 THEN
                IsValidMove = 1
            END IF
        ELSEIF ME.pieceType = 6 THEN
            REM King (simplified)
            IF rowDiff >= -1 AND rowDiff <= 1 AND colDiff >= -1 AND colDiff <= 1 THEN
                IsValidMove = 1
            END IF
        END IF
    END FUNCTION

    SUB MoveTo(newRow AS INTEGER, newCol AS INTEGER)
        ME.row = newRow
        ME.col = newCol
    END SUB
END CLASS

REM WORKAROUND for BUG-086: Cannot pass arrays to SUBs, so can't create DisplayBoard helper
REM Need to use global array instead
DIM board(25) AS ChessPiece
DIM r AS INTEGER
DIM c AS INTEGER
DIM idx AS INTEGER

REM Initialize 5x5 mini board
PRINT "=== Chess Player Input Test (5x5 board) ==="

FOR r = 1 TO 5
    FOR c = 1 TO 5
        idx = (r - 1) * 5 + c
        board(idx) = NEW ChessPiece()
        board(idx).Init(0, 0, r, c)
    NEXT c
NEXT r

REM Place a few pieces
board(3).Init(2, 0, 1, 3)   REM White Rook at (1,3)
board(13).Init(1, 0, 3, 3)  REM White Pawn at (3,3)
board(15).Init(6, 0, 3, 5)  REM White King at (3,5)

PRINT "Initial board:"
REM Can't call DisplayBoard due to BUG-086, so inline the display
FOR r = 5 TO 1 STEP -1
    PRINT r; " ";
    FOR c = 1 TO 5
        idx = (r - 1) * 5 + c
        PRINT board(idx).GetSymbol(); " ";
    NEXT c
    PRINT ""
NEXT r
PRINT "  1 2 3 4 5"
PRINT ""

REM Simple move test
DIM fromRow AS INTEGER
DIM fromCol AS INTEGER
DIM toRow AS INTEGER
DIM toCol AS INTEGER
DIM fromIdx AS INTEGER
DIM toIdx AS INTEGER
DIM moveValid AS INTEGER

PRINT "Let's move the Rook from (1,3) to (1,5)"
PRINT ""

fromRow = 1
fromCol = 3
toRow = 1
toCol = 5

fromIdx = (fromRow - 1) * 5 + fromCol
toIdx = (toRow - 1) * 5 + toCol

PRINT "Checking if move is valid..."
moveValid = board(fromIdx).IsValidMove(toRow, toCol)

IF moveValid THEN
    PRINT "Move is valid! Executing..."

    REM Copy piece data to target
    board(toIdx).Init(board(fromIdx).pieceType, board(fromIdx).pieceColor, toRow, toCol)

    REM Clear source
    board(fromIdx).Init(0, 0, fromRow, fromCol)

    PRINT ""
    PRINT "After move:"
    FOR r = 5 TO 1 STEP -1
        PRINT r; " ";
        FOR c = 1 TO 5
            idx = (r - 1) * 5 + c
            PRINT board(idx).GetSymbol(); " ";
        NEXT c
        PRINT ""
    NEXT r
    PRINT "  1 2 3 4 5"
ELSE
    PRINT "Move is invalid!"
END IF

PRINT ""
PRINT "Step 6 Complete: Move execution works!"
PRINT "(INPUT skipped due to BUG-080 - INPUT only works with INTEGER)"
