REM === Chess Engine v2 - Move Generation ===
REM Testing: Complex logic, nested loops, array manipulation

REM Piece types
CONST EMPTY = 0
CONST PAWN = 1
CONST KNIGHT = 2
CONST BISHOP = 3
CONST ROOK = 4
CONST QUEEN = 5
CONST KING = 6

REM Colors
CONST WHITE = 0
CONST BLACK = 1

REM Board state
DIM board_type(64) AS INTEGER
DIM board_color(64) AS INTEGER
DIM board_moved(64) AS INTEGER

REM Move list (max 256 possible moves in a position)
DIM move_from_row(256) AS INTEGER
DIM move_from_col(256) AS INTEGER
DIM move_to_row(256) AS INTEGER
DIM move_to_col(256) AS INTEGER
DIM move_count AS INTEGER

SUB InitBoard()
    DIM i AS INTEGER
    FOR i = 1 TO 64
        board_type(i) = EMPTY
        board_color(i) = WHITE
        board_moved(i) = 0
    NEXT i
END SUB

SUB SetPiece(row AS INTEGER, col AS INTEGER, pType AS INTEGER, pColor AS INTEGER)
    DIM idx AS INTEGER
    idx = (row - 1) * 8 + col
    board_type(idx) = pType
    board_color(idx) = pColor
    board_moved(idx) = 0
END SUB

FUNCTION GetPieceType(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    IF row < 1 OR row > 8 OR col < 1 OR col > 8 THEN
        GetPieceType = -1
        RETURN
    END IF
    idx = (row - 1) * 8 + col
    GetPieceType = board_type(idx)
END FUNCTION

FUNCTION GetPieceColor(row AS INTEGER, col AS INTEGER) AS INTEGER
    DIM idx AS INTEGER
    IF row < 1 OR row > 8 OR col < 1 OR col > 8 THEN
        GetPieceColor = -1
        RETURN
    END IF
    idx = (row - 1) * 8 + col
    GetPieceColor = board_color(idx)
END FUNCTION

FUNCTION IsSquareEmpty(row AS INTEGER, col AS INTEGER) AS INTEGER
    IsSquareEmpty = 0
    IF GetPieceType(row, col) = EMPTY THEN
        IsSquareEmpty = 1
    END IF
END FUNCTION

FUNCTION IsEnemyPiece(row AS INTEGER, col AS INTEGER, myColor AS INTEGER) AS INTEGER
    IsEnemyPiece = 0
    DIM pType AS INTEGER
    DIM pColor AS INTEGER

    pType = GetPieceType(row, col)
    IF pType = EMPTY OR pType = -1 THEN
        RETURN
    END IF

    pColor = GetPieceColor(row, col)
    IF pColor <> myColor THEN
        IsEnemyPiece = 1
    END IF
END FUNCTION

SUB AddMove(fromRow AS INTEGER, fromCol AS INTEGER, toRow AS INTEGER, toCol AS INTEGER)
    move_count = move_count + 1
    move_from_row(move_count) = fromRow
    move_from_col(move_count) = fromCol
    move_to_row(move_count) = toRow
    move_to_col(move_count) = toCol
END SUB

SUB ClearMoves()
    move_count = 0
END SUB

SUB GeneratePawnMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    DIM direction AS INTEGER
    DIM startRow AS INTEGER
    DIM newRow AS INTEGER

    REM Pawns move forward (up for white, down for black)
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

        REM Forward two squares from starting position
        IF row = startRow THEN
            newRow = row + direction + direction
            IF IsSquareEmpty(newRow, col) = 1 THEN
                AddMove(row, col, newRow, col)
            END IF
        END IF
    END IF

    REM Captures (diagonal)
    newRow = row + direction
    REM Left capture
    IF col > 1 THEN
        IF IsEnemyPiece(newRow, col - 1, pColor) = 1 THEN
            AddMove(row, col, newRow, col - 1)
        END IF
    END IF

    REM Right capture
    IF col < 8 THEN
        IF IsEnemyPiece(newRow, col + 1, pColor) = 1 THEN
            AddMove(row, col, newRow, col + 1)
        END IF
    END IF
END SUB

SUB GenerateKnightMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    REM Knight moves in L-shape: 2 squares in one direction, 1 in perpendicular
    DIM moves(8, 2) AS INTEGER
    DIM i AS INTEGER
    DIM newRow AS INTEGER
    DIM newCol AS INTEGER

    REM 8 possible knight moves
    moves(1, 1) = 2: moves(1, 2) = 1
    moves(2, 1) = 2: moves(2, 2) = -1
    moves(3, 1) = -2: moves(3, 2) = 1
    moves(4, 1) = -2: moves(4, 2) = -1
    moves(5, 1) = 1: moves(5, 2) = 2
    moves(6, 1) = 1: moves(6, 2) = -2
    moves(7, 1) = -1: moves(7, 2) = 2
    moves(8, 1) = -1: moves(8, 2) = -2

    FOR i = 1 TO 8
        newRow = row + moves(i, 1)
        newCol = col + moves(i, 2)

        IF newRow >= 1 AND newRow <= 8 AND newCol >= 1 AND newCol <= 8 THEN
            IF IsSquareEmpty(newRow, newCol) = 1 OR IsEnemyPiece(newRow, newCol, pColor) = 1 THEN
                AddMove(row, col, newRow, newCol)
            END IF
        END IF
    NEXT i
END SUB

SUB GenerateSlidingMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER, dr AS INTEGER, dc AS INTEGER)
    REM Generate moves in one direction until blocked
    DIM newRow AS INTEGER
    DIM newCol AS INTEGER
    DIM dist AS INTEGER

    FOR dist = 1 TO 7
        newRow = row + dr * dist
        newCol = col + dc * dist

        IF newRow < 1 OR newRow > 8 OR newCol < 1 OR newCol > 8 THEN
            RETURN
        END IF

        IF IsSquareEmpty(newRow, newCol) = 1 THEN
            AddMove(row, col, newRow, newCol)
        ELSE
            IF IsEnemyPiece(newRow, newCol, pColor) = 1 THEN
                AddMove(row, col, newRow, newCol)
            END IF
            RETURN
        END IF
    NEXT dist
END SUB

SUB GenerateRookMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    REM Rook moves horizontally and vertically
    GenerateSlidingMoves(row, col, pColor, 1, 0)   REM Up
    GenerateSlidingMoves(row, col, pColor, -1, 0)  REM Down
    GenerateSlidingMoves(row, col, pColor, 0, 1)   REM Right
    GenerateSlidingMoves(row, col, pColor, 0, -1)  REM Left
END SUB

SUB GenerateBishopMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    REM Bishop moves diagonally
    GenerateSlidingMoves(row, col, pColor, 1, 1)    REM Up-Right
    GenerateSlidingMoves(row, col, pColor, 1, -1)   REM Up-Left
    GenerateSlidingMoves(row, col, pColor, -1, 1)   REM Down-Right
    GenerateSlidingMoves(row, col, pColor, -1, -1)  REM Down-Left
END SUB

SUB GenerateQueenMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    REM Queen = Rook + Bishop
    GenerateRookMoves(row, col, pColor)
    GenerateBishopMoves(row, col, pColor)
END SUB

SUB GenerateKingMoves(row AS INTEGER, col AS INTEGER, pColor AS INTEGER)
    REM King moves one square in any direction
    DIM dr AS INTEGER
    DIM dc AS INTEGER
    DIM newRow AS INTEGER
    DIM newCol AS INTEGER

    FOR dr = -1 TO 1
        FOR dc = -1 TO 1
            IF dr = 0 AND dc = 0 THEN
                REM Skip staying in place
            ELSE
                newRow = row + dr
                newCol = col + dc

                IF newRow >= 1 AND newRow <= 8 AND newCol >= 1 AND newCol <= 8 THEN
                    IF IsSquareEmpty(newRow, newCol) = 1 OR IsEnemyPiece(newRow, newCol, pColor) = 1 THEN
                        AddMove(row, col, newRow, newCol)
                    END IF
                END IF
            END IF
        NEXT dc
    NEXT dr
END SUB

SUB GenerateAllMoves(playerColor AS INTEGER)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM pType AS INTEGER
    DIM pColor AS INTEGER

    ClearMoves()

    FOR row = 1 TO 8
        FOR col = 1 TO 8
            pType = GetPieceType(row, col)
            pColor = GetPieceColor(row, col)

            IF pType <> EMPTY AND pColor = playerColor THEN
                SELECT CASE pType
                    CASE 1  REM PAWN
                        GeneratePawnMoves(row, col, pColor)
                    CASE 2  REM KNIGHT
                        GenerateKnightMoves(row, col, pColor)
                    CASE 3  REM BISHOP
                        GenerateBishopMoves(row, col, pColor)
                    CASE 4  REM ROOK
                        GenerateRookMoves(row, col, pColor)
                    CASE 5  REM QUEEN
                        GenerateQueenMoves(row, col, pColor)
                    CASE 6  REM KING
                        GenerateKingMoves(row, col, pColor)
                END SELECT
            END IF
        NEXT col
    NEXT row
END SUB

SUB PrintMoves()
    DIM i AS INTEGER
    PRINT "Legal moves ("; move_count; "):"
    FOR i = 1 TO move_count
        PRINT move_from_row(i); ","; move_from_col(i);
        PRINT " to "; move_to_row(i); ","; move_to_col(i); " ";
        IF i MOD 8 = 0 THEN PRINT ""
    NEXT i
    PRINT ""
END SUB

REM Test move generation
PRINT "=== Chess Move Generation Test ==="
PRINT ""

InitBoard()

REM Set up a simple position
SetPiece(2, 4, PAWN, WHITE)
SetPiece(4, 4, PAWN, BLACK)
SetPiece(1, 2, KNIGHT, WHITE)
SetPiece(8, 7, ROOK, BLACK)

PRINT "Test position:"
PRINT "White: Pawn at d2, Knight at b1"
PRINT "Black: Pawn at d4, Rook at g8"
PRINT ""

GenerateAllMoves(WHITE)
PRINT "White's moves:"
PrintMoves()

GenerateAllMoves(BLACK)
PRINT "Black's moves:"
PrintMoves()

PRINT "Move generation test complete!"
