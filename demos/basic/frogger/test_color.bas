DIM ESC AS STRING
DIM RESET AS STRING
DIM COLOR_RED AS STRING
DIM COLOR_GREEN AS STRING

ESC = CHR(27)
RESET = "[0m"
COLOR_RED = "[31m"
COLOR_GREEN = "[32m"

SUB ClearScreen()
    PRINT ESC; "[2J"
END SUB

SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H";
END SUB

SUB PrintColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    GotoXY(row, col)
    PRINT ESC; clr; text; ESC; RESET;
END SUB

ClearScreen()
PrintColorAt(1, 1, COLOR_RED, "RED TEXT")
PrintColorAt(3, 1, COLOR_GREEN, "GREEN TEXT")
PrintColorAt(5, 1, COLOR_RED, "More red")
PRINT ""
PRINT "Done"
