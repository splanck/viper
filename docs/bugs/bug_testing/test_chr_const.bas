REM Test BUG-062 fix: CONST with CHR$() should be evaluated at compile time
CONST ESC$ = CHR$(27)
CONST COLOR_RED$ = CHR$(27) + "[31m"
CONST COLOR_RESET$ = CHR$(27) + "[0m"

PRINT COLOR_RED$ + "This should be red!" + COLOR_RESET$
PRINT "ESC character code: " + STR$(ASC(ESC$))
END
