' conditionals and multi-way select
DIM n AS INTEGER
n = 7
IF n < 0 THEN
  PRINT "negative"
ELSEIF n = 0 THEN
  PRINT "zero"
ELSE
  PRINT "positive"
END IF

SELECT CASE n
  CASE 1 TO 3
    PRINT "small"
  CASE IS >= 10
    PRINT "big"
  CASE 4, 5, 6, 7, 8, 9
    PRINT "mid"
  CASE ELSE
    PRINT "other"
END SELECT
