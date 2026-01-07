DIM running AS BOOLEAN
DIM k AS STRING
running = TRUE

DO WHILE running
  k = GETKEY$()
  IF LEN(k) > 0 THEN
    PRINT "Key: "; k
    running = FALSE
  END IF
LOOP
PRINT "Done"
