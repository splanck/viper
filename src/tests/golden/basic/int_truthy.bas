' int_truthy.bas — verify integer truthiness in IF conditions

DIM X AS INTEGER

X = 1
IF X THEN
  PRINT "T1"
ELSE
  PRINT "F1"
END IF

X = 0
IF X THEN
  PRINT "T2"
ELSE
  PRINT "F2"
END IF

