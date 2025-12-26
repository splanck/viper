REM chess_display.bas - ANSI display helper functions module
REM To be included via ADDFILE in main program

FUNCTION AnsiReset() AS STRING
    AnsiReset = CHR$(27) + "[0m"
END FUNCTION

FUNCTION AnsiColor(colorCode AS INTEGER) AS STRING
    AnsiColor = CHR$(27) + "[" + STR$(colorCode) + "m"
END FUNCTION

FUNCTION AnsiClear() AS STRING
    AnsiClear = CHR$(27) + "[2J" + CHR$(27) + "[H"
END FUNCTION

SUB DisplayBoard(board() AS ChessPiece)
    DIM r AS INTEGER
    DIM c AS INTEGER
    DIM idx AS INTEGER
    DIM isLightSquare AS INTEGER

    PRINT AnsiClear();
    PRINT AnsiColor(96); "=== Modular Chess Board (ADDFILE Test) ==="; AnsiReset()
    PRINT ""

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
END SUB
