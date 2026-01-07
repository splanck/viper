REM Test FOR variable used in SUB
DIM inning AS INTEGER

SUB ShowInfo()
    PRINT "  In SUB, inning = "; inning
END SUB

FOR inning = 1 TO 3
    PRINT "Main: inning = "; inning
    ShowInfo()
NEXT

PRINT "Done"
