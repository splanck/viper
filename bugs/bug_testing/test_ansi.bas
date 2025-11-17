REM Test ANSI codes
CONST ESC$ = CHR$(27)
CONST RESET$ = ESC$ + "[0m"
CONST RED$ = ESC$ + "[31m"

PRINT RED$; "Red text"; RESET$
PRINT "Normal text"
