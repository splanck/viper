REM Chess Game - Step 3: ANSI Colors and Fancy Board
REM Testing: String concatenation, CHR$, ANSI escape codes, complex methods

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
                symbol = " "  REM Empty
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

    FUNCTION GetColoredSymbol() AS STRING
        DIM result AS STRING
        DIM sym AS STRING
        DIM esc AS STRING

        esc = CHR$(27)  REM ANSI escape character
        sym = ME.GetSymbol()

        REM White pieces = bright white, Black pieces = yellow
        IF ME.pieceType = 0 THEN
            REM Empty square
            result = " "
        ELSE
            IF ME.pieceColor = 0 THEN
                REM White pieces: bright white (97)
                result = esc + "[97m" + sym + esc + "[0m"
            ELSE
                REM Black pieces: yellow (33)
                result = esc + "[33m" + sym + esc + "[0m"
            END IF
        END IF

        GetColoredSymbol = result
    END FUNCTION
END CLASS

REM ANSI helper functions
FUNCTION AnsiReset() AS STRING
    AnsiReset = CHR$(27) + "[0m"
END FUNCTION

FUNCTION AnsiColor(colorCode AS INTEGER) AS STRING
    AnsiColor = CHR$(27) + "[" + STR$(colorCode) + "m"
END FUNCTION

FUNCTION AnsiClear() AS STRING
    AnsiClear = CHR$(27) + "[2J" + CHR$(27) + "[H"
END FUNCTION

REM Initialize board
DIM board(64) AS ChessPiece
DIM r AS INTEGER
DIM c AS INTEGER
DIM idx AS INTEGER

REM Clear screen
PRINT AnsiClear();

PRINT AnsiColor(96); "=== ANSI Chess Board ==="; AnsiReset()
PRINT ""

REM Initialize all squares as empty
FOR r = 1 TO 8
    FOR c = 1 TO 8
        idx = (r - 1) * 8 + c
        board(idx) = NEW ChessPiece()
        board(idx).Init(0, 0, r, c)
    NEXT c
NEXT r

REM Setup starting position
REM White pieces (row 1-2)
board(1).Init(2, 0, 1, 1)   REM Rook
board(2).Init(3, 0, 1, 2)   REM Knight
board(3).Init(4, 0, 1, 3)   REM Bishop
board(4).Init(5, 0, 1, 4)   REM Queen
board(5).Init(6, 0, 1, 5)   REM King
board(6).Init(4, 0, 1, 6)   REM Bishop
board(7).Init(3, 0, 1, 7)   REM Knight
board(8).Init(2, 0, 1, 8)   REM Rook

FOR c = 1 TO 8
    idx = (2 - 1) * 8 + c
    board(idx).Init(1, 0, 2, c)  REM White pawns
NEXT c

REM Black pieces (row 7-8)
FOR c = 1 TO 8
    idx = (7 - 1) * 8 + c
    board(idx).Init(1, 1, 7, c)  REM Black pawns
NEXT c

idx = (8 - 1) * 8 + 1
board(idx).Init(2, 1, 8, 1)   REM Rook

idx = (8 - 1) * 8 + 2
board(idx).Init(3, 1, 8, 2)   REM Knight

idx = (8 - 1) * 8 + 3
board(idx).Init(4, 1, 8, 3)   REM Bishop

idx = (8 - 1) * 8 + 4
board(idx).Init(5, 1, 8, 4)   REM Queen

idx = (8 - 1) * 8 + 5
board(idx).Init(6, 1, 8, 5)   REM King

idx = (8 - 1) * 8 + 6
board(idx).Init(4, 1, 8, 6)   REM Bishop

idx = (8 - 1) * 8 + 7
board(idx).Init(3, 1, 8, 7)   REM Knight

idx = (8 - 1) * 8 + 8
board(idx).Init(2, 1, 8, 8)   REM Rook

REM Display board with colors
DIM isLightSquare AS INTEGER
FOR r = 8 TO 1 STEP -1
    PRINT AnsiColor(36); r; " "; AnsiReset();
    FOR c = 1 TO 8
        idx = (r - 1) * 8 + c

        REM Checkerboard pattern
        isLightSquare = ((r + c) MOD 2)

        IF isLightSquare = 0 THEN
            REM Dark square (green background)
            PRINT AnsiColor(42);
        ELSE
            REM Light square (white background)
            PRINT AnsiColor(47);
        END IF

        PRINT board(idx).GetColoredSymbol(); " ";
        PRINT AnsiReset();
    NEXT c
    PRINT ""
NEXT r

PRINT "  "; AnsiColor(36); "a b c d e f g h"; AnsiReset()
PRINT ""
PRINT AnsiColor(92); "Step 3 Complete: ANSI colors work!"; AnsiReset()
