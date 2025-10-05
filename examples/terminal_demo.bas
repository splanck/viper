COLOR 14,1
CLS
LOCATE 3, 10
PRINT "Press any key..."
k$ = INKEY$()
IF k$ = "" THEN k$ = GETKEY$()
COLOR 7,0
PRINT "You pressed code "; ASC(k$)
