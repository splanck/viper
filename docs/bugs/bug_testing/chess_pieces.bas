REM chess_pieces.bas - Chess piece class definition module
REM To be included via ADDFILE in main program

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

        esc = CHR$(27)
        sym = ME.GetSymbol()

        IF ME.pieceType = 0 THEN
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
