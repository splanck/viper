LET N% = 2
SELECT CASE N%
  CASE 1, 2, 3
    PRINT "small"
  CASE IS < 0
    PRINT "neg"
  CASE 10 TO 20
    PRINT "ten..twenty"
  CASE ELSE
    PRINT "other"
END SELECT
