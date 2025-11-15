' ANSI Color Constants and Helper Functions
' Testing: CONST strings, CHR$, string concatenation

CONST ESC$ = CHR$(27)
CONST RESET$ = ESC$ + "[0m"
CONST BOLD$ = ESC$ + "[1m"

' Colors
CONST BLACK$ = ESC$ + "[30m"
CONST RED$ = ESC$ + "[31m"
CONST GREEN$ = ESC$ + "[32m"
CONST YELLOW$ = ESC$ + "[33m"
CONST BLUE$ = ESC$ + "[34m"
CONST MAGENTA$ = ESC$ + "[35m"
CONST CYAN$ = ESC$ + "[36m"
CONST WHITE$ = ESC$ + "[37m"

' Bright colors
CONST BRIGHT_RED$ = ESC$ + "[91m"
CONST BRIGHT_GREEN$ = ESC$ + "[92m"
CONST BRIGHT_YELLOW$ = ESC$ + "[93m"
CONST BRIGHT_BLUE$ = ESC$ + "[94m"
CONST BRIGHT_MAGENTA$ = ESC$ + "[95m"
CONST BRIGHT_CYAN$ = ESC$ + "[96m"

SUB PrintColor(text AS STRING, clr AS STRING)
    PRINT clr + text + RESET$;
END SUB

SUB PrintLn(text AS STRING, clr AS STRING)
    PRINT clr + text + RESET$
END SUB
