REM ============================================================================
REM  CHESS BOARD - Board state and display
REM ============================================================================

REM Global board state
DIM board(7, 7) AS Piece
DIM currentTurn AS INTEGER
DIM whiteCanCastleKing AS INTEGER
DIM whiteCanCastleQueen AS INTEGER
DIM blackCanCastleKing AS INTEGER
DIM blackCanCastleQueen AS INTEGER
DIM enPassantCol AS INTEGER
DIM halfMoveClock AS INTEGER
DIM fullMoveNumber AS INTEGER
DIM gameOver AS INTEGER
DIM winner AS INTEGER
DIM moveHistory AS Viper.Collections.List
DIM aiDepth AS INTEGER
DIM playerColor AS INTEGER
DIM nodesSearched AS INTEGER

REM Global return values for FindKing (workaround for no BYREF)
DIM foundKingRow AS INTEGER
DIM foundKingCol AS INTEGER

REM ANSI escape
DIM ESC AS STRING
ESC = CHR$(27)

REM ============================================================================
REM  INITIALIZE BOARD
REM ============================================================================
SUB InitBoard()
    DIM row AS INTEGER
    DIM col AS INTEGER

    REM Clear board
    FOR row = 0 TO 7
        FOR col = 0 TO 7
            board(row, col) = NEW Piece()
            board(row, col).Init(0, 0)
        NEXT col
    NEXT row

    REM Place white pieces (row 0 and 1)
    board(0, 0) = NEW Piece()
    board(0, 0).Init(4, 1)
    board(0, 1) = NEW Piece()
    board(0, 1).Init(2, 1)
    board(0, 2) = NEW Piece()
    board(0, 2).Init(3, 1)
    board(0, 3) = NEW Piece()
    board(0, 3).Init(5, 1)
    board(0, 4) = NEW Piece()
    board(0, 4).Init(6, 1)
    board(0, 5) = NEW Piece()
    board(0, 5).Init(3, 1)
    board(0, 6) = NEW Piece()
    board(0, 6).Init(2, 1)
    board(0, 7) = NEW Piece()
    board(0, 7).Init(4, 1)

    FOR col = 0 TO 7
        board(1, col) = NEW Piece()
        board(1, col).Init(1, 1)
    NEXT col

    REM Place black pieces (row 6 and 7)
    FOR col = 0 TO 7
        board(6, col) = NEW Piece()
        board(6, col).Init(1, -1)
    NEXT col

    board(7, 0) = NEW Piece()
    board(7, 0).Init(4, -1)
    board(7, 1) = NEW Piece()
    board(7, 1).Init(2, -1)
    board(7, 2) = NEW Piece()
    board(7, 2).Init(3, -1)
    board(7, 3) = NEW Piece()
    board(7, 3).Init(5, -1)
    board(7, 4) = NEW Piece()
    board(7, 4).Init(6, -1)
    board(7, 5) = NEW Piece()
    board(7, 5).Init(3, -1)
    board(7, 6) = NEW Piece()
    board(7, 6).Init(2, -1)
    board(7, 7) = NEW Piece()
    board(7, 7).Init(4, -1)

    REM Initialize game state
    currentTurn = 1
    whiteCanCastleKing = 1
    whiteCanCastleQueen = 1
    blackCanCastleKing = 1
    blackCanCastleQueen = 1
    enPassantCol = -1
    halfMoveClock = 0
    fullMoveNumber = 1
    gameOver = 0
    winner = 0

    REM Initialize move history using List
    moveHistory = NEW Viper.Collections.List()
END SUB

REM ============================================================================
REM  BOARD DISPLAY - Using StringBuilder
REM ============================================================================
SUB DrawBoard()
    DIM sb AS Viper.Text.StringBuilder
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM sym AS STRING

    sb = NEW Viper.Text.StringBuilder()

    REM Clear screen
    PRINT ESC; "[2J"; ESC; "[H";

    REM Draw title
    PRINT ""
    PRINT ESC; "[1;36m"; "    VIPER CHESS"; ESC; "[0m"
    PRINT ""

    REM Draw column headers
    PRINT "      a   b   c   d   e   f   g   h"
    PRINT "    +---+---+---+---+---+---+---+---+"

    REM Draw board from top (row 7) to bottom (row 0)
    FOR row = 7 TO 0 STEP -1
        sb.Clear()
        sb.Append("  ")
        sb.Append(LTRIM$(STR$(row + 1)))
        sb.Append(" |")

        FOR col = 0 TO 7
            piece = board(row, col)
            sym = piece.GetSymbol()

            sb.Append(" ")
            IF piece.GetType() > 0 THEN
                sb.Append(sym)
            ELSE
                sb.Append(".")
            END IF
            sb.Append(" |")
        NEXT col

        sb.Append(" ")
        sb.Append(LTRIM$(STR$(row + 1)))

        PRINT sb.ToString()
        PRINT "    +---+---+---+---+---+---+---+---+"
    NEXT row

    PRINT "      a   b   c   d   e   f   g   h"
    PRINT ""

    REM Display game info
    IF currentTurn = 1 THEN
        PRINT "  Turn: WHITE"
    ELSE
        PRINT "  Turn: BLACK"
    END IF

    PRINT "  Move: "; fullMoveNumber

    IF moveHistory.Count > 0 THEN
        DIM lastMove AS Move
        lastMove = moveHistory.get_Item(moveHistory.Count - 1)
        PRINT "  Last: "; lastMove.ToAlgebraic()
    END IF

    PRINT ""
END SUB

REM ============================================================================
REM  BOARD HELPERS
REM ============================================================================
FUNCTION IsValidSquare(row AS INTEGER, col AS INTEGER) AS INTEGER
    IF row >= 0 AND row <= 7 AND col >= 0 AND col <= 7 THEN
        IsValidSquare = 1
    ELSE
        IsValidSquare = 0
    END IF
END FUNCTION

FUNCTION IsEmptySquare(row AS INTEGER, col AS INTEGER) AS INTEGER
    IF board(row, col).GetType() = 0 THEN
        IsEmptySquare = 1
    ELSE
        IsEmptySquare = 0
    END IF
END FUNCTION

FUNCTION IsEnemyPiece(row AS INTEGER, col AS INTEGER, clr AS INTEGER) AS INTEGER
    DIM piece AS Piece
    piece = board(row, col)
    IF piece.GetType() > 0 AND piece.GetColor() <> clr THEN
        IsEnemyPiece = 1
    ELSE
        IsEnemyPiece = 0
    END IF
END FUNCTION

FUNCTION IsFriendlyPiece(row AS INTEGER, col AS INTEGER, clr AS INTEGER) AS INTEGER
    DIM piece AS Piece
    piece = board(row, col)
    IF piece.GetType() > 0 AND piece.GetColor() = clr THEN
        IsFriendlyPiece = 1
    ELSE
        IsFriendlyPiece = 0
    END IF
END FUNCTION

REM ============================================================================
REM  FIND KING POSITION - Sets foundKingRow/foundKingCol globals
REM ============================================================================
SUB FindKing(clr AS INTEGER)
    DIM row AS INTEGER
    DIM col AS INTEGER
    DIM piece AS Piece
    DIM found AS INTEGER

    foundKingRow = -1
    foundKingCol = -1
    found = 0

    row = 0
    WHILE row <= 7 AND found = 0
        col = 0
        WHILE col <= 7 AND found = 0
            piece = board(row, col)
            IF piece.GetType() = 6 AND piece.GetColor() = clr THEN
                foundKingRow = row
                foundKingCol = col
                found = 1
            END IF
            col = col + 1
        WEND
        row = row + 1
    WEND
END SUB
