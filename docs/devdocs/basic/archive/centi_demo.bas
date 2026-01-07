' Simple Centipede Demo
' Press Q to quit, SPACE to see action

DIM px AS INTEGER
DIM py AS INTEGER
DIM score AS INTEGER
DIM running AS BOOLEAN
DIM k AS STRING

px = 40
py = 20
score = 0
running = TRUE

CLS

DO WHILE running
  k = GETKEY$()
  IF LEN(k) > 0 THEN
    DIM ch AS STRING
    ch = UCASE$(k)
    IF ch = "A" THEN px = px - 1
    IF ch = "D" THEN px = px + 1
    IF ch = " " THEN score = score + 10
    IF ch = "Q" THEN running = FALSE
  END IF
  
  CLS
  COLOR 7, 0
  LOCATE 1, 1
  PRINT "Centipede Demo - A/D move, SPACE +10, Q quit"
  LOCATE 2, 1
  PRINT "Score: "; score
  
  COLOR 14, 0
  LOCATE py, px
  PRINT "A";
  
  COLOR 10, 0
  LOCATE 5, 10
  PRINT "Oooooooooo";
  
  IF score >= 100 THEN
    LOCATE 12, 35
    COLOR 11, 0
    PRINT "YOU WIN!";
    SLEEP 800
    running = FALSE
  END IF
  
  SLEEP 30
LOOP

COLOR 7, 0
CLS
PRINT "Game over. Final score: "; score
