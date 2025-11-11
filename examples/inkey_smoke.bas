COLOR 7,0
CLS
PRINT "Polling once (non-blocking). Pressing a key quickly will print its code."
k$ = INKEY$()
IF k$ <> "" THEN PRINT "Key code: "; ASC(k$) ELSE PRINT "No key"
PRINT "Done."
