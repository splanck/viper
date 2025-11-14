DIM running AS BOOLEAN
DIM k AS STRING
DIM ch AS STRING
DIM score AS INTEGER

running = TRUE
score = 0

DO WHILE running
  k = GETKEY$()
  IF LEN(k) > 0 THEN
    ch = UCASE$(k)
    IF ch = "Q" THEN running = FALSE
    IF ch = " " THEN score = score + 1
  END IF
  
  CLS
  PRINT "Score: "; score; " (Q to quit, SPACE to score)"
  
  IF score >= 5 THEN running = FALSE
LOOP

PRINT "Final score: "; score
