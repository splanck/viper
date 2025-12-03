' Centipede Game - Flat Structure
' Controls: A/D to move, SPACE to shoot, Q to quit

DIM W AS INTEGER
DIM H AS INTEGER
W = 80
H = 24

' Player
DIM px AS INTEGER
DIM py AS INTEGER
px = W \ 2
py = H - 1

' Centipede
DIM cLen AS INTEGER
DIM cDir AS INTEGER
DIM cAlive AS INTEGER
cLen = 12
cDir = 1
cAlive = cLen

DIM segX(30)
DIM segY(30)
DIM segOn(30)

' Initialize centipede
DIM i AS INTEGER
FOR i = 0 TO cLen - 1
  segX(i) = 10 - i
  segY(i) = 3
  segOn(i) = 1
NEXT i

' Bullets
DIM bX(8)
DIM bY(8)
DIM bOn(8)

DIM score AS INTEGER
DIM running AS INTEGER
DIM k AS STRING
DIM ch AS STRING
DIM nx AS INTEGER
DIM touched AS INTEGER

score = 0
running = 1

CLS

DO WHILE running
  k = GETKEY$()
  IF LEN(k) > 0 THEN
    ch = UCASE$(k)
    IF ch = "A" THEN
      px = px - 1
      IF px < 2 THEN px = 2
    END IF
    IF ch = "D" THEN
      px = px + 1
      IF px > W - 1 THEN px = W - 1
    END IF
    IF ch = " " THEN
      FOR i = 0 TO 7
        IF NOT bOn(i) THEN
          bOn(i) = 1
          bX(i) = px
          bY(i) = py - 1
          i = 999
        END IF
      NEXT i
    END IF
    IF ch = "Q" THEN
      running = 0
    END IF
  END IF
  
  ' Update bullets
  FOR i = 0 TO 7
    IF bOn(i) THEN
      bY(i) = bY(i) - 1
      IF bY(i) < 2 THEN
        bOn(i) = 0
      ELSE
        DIM j AS INTEGER
        FOR j = 0 TO cLen - 1
          IF segOn(j) AND segX(j) = bX(i) AND segY(j) = bY(i) THEN
            segOn(j) = 0
            bOn(i) = 0
            cAlive = cAlive - 1
            score = score + 1
          END IF
        NEXT j
      END IF
    END IF
  NEXT i
  
  ' Move centipede tail
  FOR i = cLen - 1 TO 1 STEP -1
    segX(i) = segX(i - 1)
    segY(i) = segY(i - 1)
  NEXT i
  
  ' Move centipede head
  nx = segX(0) + cDir
  IF nx <= 2 OR nx >= W - 1 THEN
    cDir = -cDir
    segY(0) = segY(0) + 1
    nx = segX(0) + cDir
  END IF
  segX(0) = nx
  
  ' Check collision
  touched = 0
  FOR i = 0 TO cLen - 1
    IF segOn(i) AND segY(i) >= py THEN
      touched = 1
    END IF
  NEXT i
  IF touched THEN
    running = 0
  END IF
  
  ' Draw
  CLS
  COLOR 7, 0
  LOCATE 1, 2
  PRINT "Centipede - A/D move, SPACE shoot, Q quit   Score: "; score
  
  ' Draw centipede
  COLOR 10, 0
  FOR i = 0 TO cLen - 1
    IF segOn(i) THEN
      LOCATE segY(i), segX(i)
      IF i = 0 THEN
        PRINT "O";
      ELSE
        PRINT "o";
      END IF
    END IF
  NEXT i
  
  ' Draw bullets
  COLOR 12, 0
  FOR i = 0 TO 7
    IF bOn(i) THEN
      LOCATE bY(i), bX(i)
      PRINT "|";
    END IF
  NEXT i
  
  ' Draw player
  COLOR 14, 0
  LOCATE py, px
  PRINT "A";
  
  ' Check win
  IF cAlive = 0 THEN
    LOCATE H \ 2, (W \ 2) - 4
    COLOR 11, 0
    PRINT "YOU WIN!";
    SLEEP 800
    running = 0
  END IF
  
  SLEEP 30
LOOP

COLOR 7, 0
CLS
LOCATE H \ 2, (W \ 2) - 8
PRINT "Game over. Score = "; score
LOCATE H \ 2 + 1, (W \ 2) - 12
PRINT "Press any key to exit...";
k = GETKEY$()
