DIM i AS INTEGER
DIM running AS BOOLEAN
i = 0
running = TRUE

DO WHILE running
  CLS
  PRINT "Iteration "; i
  i = i + 1
  IF i >= 3 THEN running = FALSE
LOOP
