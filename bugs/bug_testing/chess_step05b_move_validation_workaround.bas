REM Chess Game - Step 5b: Move Validation (BUG-087 Workaround)
REM Testing: Complex methods using IF/ELSEIF instead of SELECT CASE

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
        SELECT CASE ME.pieceType
            CASE 0
                symbol = " "
            CASE 1
                symbol = "P"
            CASE 2
                symbol = "R"
            CASE 3
                symbol = "N"
            CASE 4
                symbol = "B"
            CASE 5
                symbol = "Q"
            CASE 6
                symbol = "K"
            CASE ELSE
                symbol = "?"
        END SELECT
        GetSymbol = symbol
    END FUNCTION

    FUNCTION IsValidMove(toRow AS INTEGER, toCol AS INTEGER) AS INTEGER
        DIM rowDiff AS INTEGER
        DIM colDiff AS INTEGER
        DIM absRowDiff AS INTEGER
        DIM absColDiff AS INTEGER

        REM Default to invalid
        IsValidMove = 0

        REM Calculate differences
        rowDiff = toRow - ME.row
        colDiff = toCol - ME.col

        REM Get absolute values
        IF rowDiff < 0 THEN
            absRowDiff = -rowDiff
        ELSE
            absRowDiff = rowDiff
        END IF

        IF colDiff < 0 THEN
            absColDiff = -colDiff
        ELSE
            absColDiff = colDiff
        END IF

        REM Validate based on piece type - using IF/ELSEIF to avoid BUG-087
        IF ME.pieceType = 0 THEN
            REM Empty square
            IsValidMove = 0

        ELSEIF ME.pieceType = 1 THEN
            REM Pawn
            IF ME.pieceColor = 0 THEN
                REM White pawn moves forward
                IF toRow = ME.row + 1 AND toCol = ME.col THEN
                    IsValidMove = 1
                END IF
            ELSE
                REM Black pawn moves backward
                IF toRow = ME.row - 1 AND toCol = ME.col THEN
                    IsValidMove = 1
                END IF
            END IF

        ELSEIF ME.pieceType = 2 THEN
            REM Rook - straight lines only
            IF rowDiff = 0 OR colDiff = 0 THEN
                IsValidMove = 1
            END IF

        ELSEIF ME.pieceType = 3 THEN
            REM Knight - L-shape
            IF (absRowDiff = 2 AND absColDiff = 1) OR (absRowDiff = 1 AND absColDiff = 2) THEN
                IsValidMove = 1
            END IF

        ELSEIF ME.pieceType = 4 THEN
            REM Bishop - diagonals only
            IF absRowDiff = absColDiff AND absRowDiff > 0 THEN
                IsValidMove = 1
            END IF

        ELSEIF ME.pieceType = 5 THEN
            REM Queen - combines rook and bishop
            IF rowDiff = 0 OR colDiff = 0 OR absRowDiff = absColDiff THEN
                IsValidMove = 1
            END IF

        ELSEIF ME.pieceType = 6 THEN
            REM King - one square in any direction
            IF absRowDiff <= 1 AND absColDiff <= 1 AND (absRowDiff > 0 OR absColDiff > 0) THEN
                IsValidMove = 1
            END IF
        END IF
    END FUNCTION
END CLASS

REM Test move validation
PRINT "=== Chess Move Validation Test (BUG-087 Workaround) ==="
PRINT ""

REM Test 1: Pawn moves
DIM whitePawn AS ChessPiece
whitePawn = NEW ChessPiece()
whitePawn.Init(1, 0, 2, 4)

PRINT "White Pawn at (2,4):"
PRINT "  Move to (3,4): ";
IF whitePawn.IsValidMove(3, 4) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (2,5): ";
IF whitePawn.IsValidMove(2, 5) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT ""

REM Test 2: Rook moves
DIM rook AS ChessPiece
rook = NEW ChessPiece()
rook.Init(2, 0, 1, 1)

PRINT "White Rook at (1,1):"
PRINT "  Move to (1,8): ";
IF rook.IsValidMove(1, 8) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (5,1): ";
IF rook.IsValidMove(5, 1) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (3,3): ";
IF rook.IsValidMove(3, 3) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT ""

REM Test 3: Knight moves
DIM knight AS ChessPiece
knight = NEW ChessPiece()
knight.Init(3, 0, 4, 4)

PRINT "White Knight at (4,4):"
PRINT "  Move to (6,5): ";
IF knight.IsValidMove(6, 5) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (5,6): ";
IF knight.IsValidMove(5, 6) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (5,5): ";
IF knight.IsValidMove(5, 5) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT ""

REM Test 4: Bishop diagonal moves
DIM bishop AS ChessPiece
bishop = NEW ChessPiece()
bishop.Init(4, 0, 1, 3)

PRINT "White Bishop at (1,3):"
PRINT "  Move to (4,6): ";
IF bishop.IsValidMove(4, 6) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (3,5): ";
IF bishop.IsValidMove(3, 5) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT "  Move to (3,4): ";
IF bishop.IsValidMove(3, 4) THEN
    PRINT "Valid"
ELSE
    PRINT "Invalid"
END IF

PRINT ""
PRINT "Step 5b Complete: Move validation works (using IF/ELSEIF)!"
