' Minimal Working Game Demo
PRINT "Mini Game - Press keys, Q to quit"
PRINT ""

DIM score AS INTEGER
DIM k AS STRING
score = 0

10 k = GETKEY$()
   IF LEN(k) > 0 THEN GOTO 20
   GOTO 10

20 DIM ch AS STRING
   ch = UCASE$(k)
   IF ch = "Q" THEN GOTO 100
   score = score + 1
   PRINT "Score: "; score; " (pressed "; ch; ")"
   GOTO 10

100 PRINT ""
    PRINT "Final score: "; score
    PRINT "Thanks for playing!"
