CLASS Counter
  DIM n AS INT
  SUB Inc(): n = n + 1: END SUB
  FUNCTION Val() AS INT: RETURN n: END FUNCTION
END CLASS
DIM c AS Counter
c = NEW Counter()
c.Inc
PRINT c.Val()
