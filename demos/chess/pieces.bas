' pieces.bas - Chess piece constants and utilities
' Defines piece values and helper functions

' Piece type constants (absolute values)
CONST EMPTY AS INTEGER = 0
CONST PAWN AS INTEGER = 1
CONST KNIGHT AS INTEGER = 2
CONST BISHOP AS INTEGER = 3
CONST ROOK AS INTEGER = 4
CONST QUEEN AS INTEGER = 5
CONST KING AS INTEGER = 6

' Color constants for COLOR statement
CONST CLR_BLACK AS INTEGER = 0
CONST CLR_RED AS INTEGER = 1
CONST CLR_GREEN AS INTEGER = 2
CONST CLR_YELLOW AS INTEGER = 3
CONST CLR_BLUE AS INTEGER = 4
CONST CLR_MAGENTA AS INTEGER = 5
CONST CLR_CYAN AS INTEGER = 6
CONST CLR_WHITE AS INTEGER = 7
CONST CLR_GRAY AS INTEGER = 8
CONST CLR_BRIGHT_RED AS INTEGER = 9
CONST CLR_BRIGHT_GREEN AS INTEGER = 10
CONST CLR_BRIGHT_YELLOW AS INTEGER = 11
CONST CLR_BRIGHT_BLUE AS INTEGER = 12
CONST CLR_BRIGHT_MAGENTA AS INTEGER = 13
CONST CLR_BRIGHT_CYAN AS INTEGER = 14
CONST CLR_BRIGHT_WHITE AS INTEGER = 15

' Board square colors (backgrounds)
CONST SQ_LIGHT AS INTEGER = 3      ' Yellow/brown - good contrast
CONST SQ_DARK AS INTEGER = 4       ' Blue - classic chess look
CONST SQ_HIGHLIGHT AS INTEGER = 2  ' Green for selected

' Piece helper class
CLASS PieceHelper
    ' Get piece character for display (Unicode chess symbols)
    FUNCTION GetSymbol(piece AS INTEGER) AS STRING
        DIM p AS INTEGER
        p = piece
        IF p < 0 THEN p = 0 - p

        IF piece > 0 THEN
            ' White pieces (uppercase)
            IF p = PAWN THEN GetSymbol = "P"
            IF p = KNIGHT THEN GetSymbol = "N"
            IF p = BISHOP THEN GetSymbol = "B"
            IF p = ROOK THEN GetSymbol = "R"
            IF p = QUEEN THEN GetSymbol = "Q"
            IF p = KING THEN GetSymbol = "K"
            IF p = EMPTY THEN GetSymbol = " "
        ELSE
            ' Black pieces (lowercase)
            IF p = PAWN THEN GetSymbol = "p"
            IF p = KNIGHT THEN GetSymbol = "n"
            IF p = BISHOP THEN GetSymbol = "b"
            IF p = ROOK THEN GetSymbol = "r"
            IF p = QUEEN THEN GetSymbol = "q"
            IF p = KING THEN GetSymbol = "k"
            IF p = EMPTY THEN GetSymbol = " "
        END IF
    END FUNCTION

    ' Get fancy display symbol (same as GetSymbol for ASCII terminals)
    FUNCTION GetDisplaySymbol(piece AS INTEGER) AS STRING
        GetDisplaySymbol = Me.GetSymbol(piece)
    END FUNCTION

    ' Get piece value for evaluation
    FUNCTION GetValue(piece AS INTEGER) AS INTEGER
        DIM p AS INTEGER
        p = piece
        IF p < 0 THEN p = 0 - p

        IF p = PAWN THEN GetValue = 100
        IF p = KNIGHT THEN GetValue = 320
        IF p = BISHOP THEN GetValue = 330
        IF p = ROOK THEN GetValue = 500
        IF p = QUEEN THEN GetValue = 900
        IF p = KING THEN GetValue = 20000
        IF p = EMPTY THEN GetValue = 0
    END FUNCTION

    ' Check if piece is white (positive)
    FUNCTION IsWhite(piece AS INTEGER) AS INTEGER
        IsWhite = 0
        IF piece > 0 THEN IsWhite = 1
    END FUNCTION

    ' Check if piece is black (negative)
    FUNCTION IsBlack(piece AS INTEGER) AS INTEGER
        IsBlack = 0
        IF piece < 0 THEN IsBlack = 1
    END FUNCTION

    ' Get absolute piece type
    FUNCTION GetType(piece AS INTEGER) AS INTEGER
        GetType = piece
        IF piece < 0 THEN GetType = 0 - piece
    END FUNCTION
END CLASS
