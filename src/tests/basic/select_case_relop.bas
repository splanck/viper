DIM N
FOR N = -1 TO 1
  SELECT CASE N
    CASE IS < 0
      PRINT "neg"
    CASE 0
      PRINT "zero"
    CASE IS > 0
      PRINT "pos"
  END SELECT
NEXT
