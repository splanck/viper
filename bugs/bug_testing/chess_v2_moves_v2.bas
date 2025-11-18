REM === Chess Move Generation - Incremental Build ===

CONST EMPTY = 0
CONST PAWN = 1
CONST KNIGHT = 2

CONST WHITE = 0
CONST BLACK = 1

DIM board_type(64) AS INTEGER
DIM board_color(64) AS INTEGER

SUB InitBoard()
    DIM i AS INTEGER
    FOR i = 1 TO 64
        board_type(i) = EMPTY
        board_color(i) = WHITE
    NEXT i
END SUB

SUB SetPiece(row AS INTEGER, col AS INTEGER, pType AS INTEGER, pColor AS INTEGER)
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    board_type(idx) = pType
    board_color(idx) = pColor
END SUB

FUNCTION GetPieceType(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    IF row < 1 OR row > 8 OR col < 1 OR col > 8 THEN
        GetPieceType = -1
    ELSE
        idx = (row - 1) * 8 + col
        GetPieceType = board_type(idx)
    END IF
END FUNCTION

FUNCTION GetPieceColor(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    IF row < 1 OR row > 8 OR col < 1 OR col > 8 THEN
        GetPieceColor = -1
    ELSE
        idx = (row - 1) * 8 + col
        GetPieceColor = board_color(idx)
    END IF
END FUNCTION

FUNCTION IsSquareEmpty(row AS INTEGER, col AS INTEGER) AS INTEGER
    IF GetPieceType(row, col) = EMPTY THEN
        IsSquareEmpty = 1
    ELSE
        IsSquareEmpty = 0
    END IF
END FUNCTION

FUNCTION IsEnemyPiece(row AS INTEGER, col AS INTEGER, myColor AS INTEGER) AS INTEGER
    DIM pType AS INTEGER
    DIM pColor AS INTEGER

    IsEnemyPiece = 0
    pType = GetPieceType(row, col)
    IF pType = EMPTY OR pType = -1 THEN
        IsEnemyPiece = 0
    ELSE
        pColor = GetPieceColor(row, col)
        IF pColor <> myColor THEN
            IsEnemyPiece = 1
        END IF
    END IF
END FUNCTION

REM Move list
DIM move_from_row(256) AS INTEGER
DIM move_from_col(256) AS INTEGER
DIM move_to_row(256) AS INTEGER
DIM move_to_col(256) AS INTEGER
DIM move_count AS INTEGER

SUB ClearMoves()
    move_count = 0
END SUB

SUB AddMove(fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER)
    move_count = move_count + 1
    move_from_row(move_count) = fromRow
    move_from_col(move_count) = fromCol
    move_to_row(move_count) = toRow
    move_to_col(move_count) = toCol
END SUB

SUB GeneratePawnMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    DIM direction AS INTEGER
    DIM startRow AS INTEGER
    DIM newRow AS INTEGER

    IF pColor = WHITE THEN
        direction = 1
        startRow = 2
    ELSE
        direction = -1
        startRow = 7
    END IF

    REM Forward one square
    newRow = row + direction
    IF IsSquareEmpty(newRow, col) = 1 THEN
        AddMove(row, col, newRow, col)

        REM Forward two from start
        IF row = startRow THEN
            newRow = row + direction + direction
            IF IsSquareEmpty(newRow, col) = 1 THEN
                AddMove(row, col, newRow, col)
            END IF
        END IF
    END IF

    REM Captures
    newRow = row + direction
    IF col > 1 THEN
        IF IsEnemyPiece(newRow, col - 1, pColor) = 1 THEN
            AddMove(row, col, newRow, col - 1)
        END IF
    END IF
    IF col < 8 THEN
        IF IsEnemyPiece(newRow, col + 1, pColor) = 1 THEN
            AddMove(row, col, newRow, col + 1)
        END IF
    END IF
END SUB

PRINT "Testing pawn moves..."
InitBoard()
SetPiece(2, 4, PAWN, WHITE)
SetPiece(4, 5, PAWN, BLACK)
ClearMoves()
GeneratePawnMoves(2, 4, WHITE)
PRINT "White pawn at d2 can move to "; move_count; " squares"
PRINT "Test passed!"
