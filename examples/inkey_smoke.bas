COLOR 7,0
CLS
PRINT "Polling once (non-blocking). Pressing a key quickly will print its code."

GetInput:
k$ = INKEY$()
IF k$ = "q" THEN END
IF k$ <> "" THEN PRINT "Key code: "; ASC(k$)
GOTO GetInput

PRINT "Done."
