' Test 06b: Integer arrays
DIM scores(5) AS INTEGER
DIM i AS INTEGER

scores(0) = 100
scores(1) = 250
scores(2) = 75
scores(3) = 500
scores(4) = 33

PRINT "Scores:"
FOR i = 0 TO 4
    PRINT "  "; scores(i)
NEXT i
END
