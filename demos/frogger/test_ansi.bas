DIM ESC AS STRING
ESC = CHR(27)

SUB ClearScreen()
    PRINT ESC; "[2J"
END SUB

SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H";
END SUB

SUB PrintColorAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    GotoXY(row, col)
    PRINT text;
END SUB

ClearScreen()
PrintColorAt(1, 1, "Hello")
PrintColorAt(5, 10, "World")
PrintColorAt(10, 1, "Done")
PRINT ""
