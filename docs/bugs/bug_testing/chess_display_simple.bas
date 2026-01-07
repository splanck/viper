REM chess_display_simple.bas - ANSI display helper functions (no array params)
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
