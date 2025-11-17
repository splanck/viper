REM Test BUG-078: FOR loop variable not incrementing
DIM inning AS INTEGER

FOR inning = 1 TO 3
    PRINT "Inning: "; inning
NEXT

PRINT "Done"
