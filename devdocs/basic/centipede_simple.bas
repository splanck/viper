' Simplified Centipede Game
' Controls: A/D to move, SPACE to shoot, Q to quit

LET W = 80
LET H = 24

' Colors
LET FG_PLAYER = 14
LET FG_CENTI  = 10
LET FG_BULLET = 12
LET FG_HUD    = 7

' Bullets
LET MAXB = 8
DIM bX(MAXB)
DIM bY(MAXB)
DIM bOn(MAXB)

' Centipede segments
LET MAXSEG = 30
DIM segX(MAXSEG)
DIM segY(MAXSEG)
DIM segAlive(MAXSEG)

SUB DrawAt(r AS INTEGER, c AS INTEGER, ch AS STRING, fg AS INTEGER, bg AS INTEGER)
  COLOR fg, bg
  LOCATE r, c
  PRINT ch;
END SUB

SUB ClearScreen()
  COLOR 7, 0
  CLS
END SUB

CLASS Player
  x AS INTEGER
  y AS INTEGER
  
  SUB NEW(px AS INTEGER, py AS INTEGER)
    Me.x = px
    Me.y = py
  END SUB
  
  SUB MovePlayer(dx AS INTEGER)
    Me.x = Me.x + dx
    IF Me.x < 2 THEN Me.x = 2
    IF Me.x > W - 1 THEN Me.x = W - 1
  END SUB
  
  SUB Draw()
    DrawAt(Me.y, Me.x, "A", FG_PLAYER, 0)
  END SUB
END CLASS

CLASS Centipede
  len AS INTEGER
  dir AS INTEGER
  headRow AS INTEGER
  aliveCount AS INTEGER
  
  SUB NEW(length AS INTEGER, startX AS INTEGER, startRow AS INTEGER)
    DIM i AS INTEGER
    Me.len = length
    Me.dir = 1
    Me.headRow = startRow
    Me.aliveCount = length
    
    FOR i = 0 TO Me.len - 1
      segX(i) = startX - i
      segY(i) = startRow
      segAlive(i) = 1
    NEXT i
  END SUB
  
  SUB UpdateCenti()
    DIM i AS INTEGER
    DIM nx AS INTEGER
    
    ' Move tail segments
    FOR i = Me.len - 1 TO 1 STEP -1
      segX(i) = segX(i - 1)
      segY(i) = segY(i - 1)
    NEXT i
    
    ' Move head
    nx = segX(0) + Me.dir
    IF nx <= 2 OR nx >= W - 1 THEN
      Me.dir = -Me.dir
      segY(0) = segY(0) + 1
      nx = segX(0) + Me.dir
    END IF
    segX(0) = nx
  END SUB
  
  SUB Draw()
    DIM i AS INTEGER
    FOR i = Me.len - 1 TO 0 STEP -1
      IF segAlive(i) THEN
        IF i = 0 THEN
          DrawAt(segY(i), segX(i), "O", FG_CENTI, 0)
        ELSE
          DrawAt(segY(i), segX(i), "o", FG_CENTI, 0)
        END IF
      END IF
    NEXT i
  END SUB
  
  FUNCTION HitAt(x AS INTEGER, y AS INTEGER) AS INTEGER
    DIM i AS INTEGER
    FOR i = 0 TO Me.len - 1
      IF segAlive(i) AND segX(i) = x AND segY(i) = y THEN
        segAlive(i) = 0
        Me.aliveCount = Me.aliveCount - 1
        RETURN 1
      END IF
    NEXT i
    RETURN 0
  END FUNCTION
END CLASS

SUB FireBullet(px AS INTEGER, py AS INTEGER)
  DIM i AS INTEGER
  FOR i = 0 TO MAXB - 1
    IF NOT bOn(i) THEN
      bOn(i) = 1
      bX(i) = px
      bY(i) = py - 1
      RETURN
    END IF
  NEXT i
END SUB

FUNCTION StepBullets(c AS Centipede) AS INTEGER
  DIM i AS INTEGER
  DIM hits AS INTEGER
  hits = 0
  
  FOR i = 0 TO MAXB - 1
    IF bOn(i) THEN
      bY(i) = bY(i) - 1
      IF bY(i) < 2 THEN
        bOn(i) = 0
      ELSE
        IF c.HitAt(bX(i), bY(i)) THEN
          bOn(i) = 0
          hits = hits + 1
        END IF
      END IF
    END IF
  NEXT i
  RETURN hits
END FUNCTION

SUB DrawBullets()
  DIM i AS INTEGER
  FOR i = 0 TO MAXB - 1
    IF bOn(i) THEN
      DrawAt(bY(i), bX(i), "|", FG_BULLET, 0)
    END IF
  NEXT i
END SUB

SUB DrawHUD(score AS INTEGER)
  COLOR FG_HUD, 0
  LOCATE 1, 2
  PRINT "Centipede - A/D move, SPACE shoot, Q quit   Score: "; score; "   ";
END SUB

' Main game
DIM p AS Player
DIM c AS Centipede
DIM score AS INTEGER
DIM running AS INTEGER
DIM k AS STRING
DIM ch AS STRING
DIM i AS INTEGER
DIM touched AS INTEGER

score = 0
running = 1

ClearScreen()
p = NEW Player(W \ 2, H - 1)
c = NEW Centipede(12, 10, 3)

DO WHILE running
  k = GETKEY$()
  IF LEN(k) > 0 THEN
    ch = UCASE$(k)
    IF ch = "A" THEN
      p.MovePlayer(-1)
    END IF
    IF ch = "D" THEN
      p.MovePlayer(1)
    END IF
    IF ch = " " THEN
      FireBullet(p.x, p.y)
    END IF
    IF ch = "Q" THEN
      running = 0
    END IF
  END IF
  
  score = score + StepBullets(c)
  c.UpdateCenti()
  
  ' Check collision
  touched = 0
  FOR i = 0 TO c.len - 1
    IF segAlive(i) AND segY(i) >= p.y THEN
      touched = 1
    END IF
  NEXT i
  IF touched THEN
    running = 0
  END IF
  
  ' Draw everything
  ClearScreen()
  DrawHUD(score)
  c.Draw()
  DrawBullets()
  p.Draw()
  
  ' Check win condition
  IF c.aliveCount = 0 THEN
    LOCATE H \ 2, (W \ 2) - 4
    COLOR 11, 0
    PRINT "YOU WIN!";
    SLEEP 800
    running = 0
  END IF
  
  SLEEP 30
LOOP

COLOR 7, 0
LOCATE H \ 2 + 1, (W \ 2) - 8
PRINT "Game over. Score = "; score
LOCATE H \ 2 + 2, (W \ 2) - 12
PRINT "Press any key to exit...";
k = GETKEY$()
