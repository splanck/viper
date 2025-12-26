' sum_no_linenos.bas — repro for "empty block" with unlabeled top-level statements
LET N = 10 : LET S1 = 0 : LET I = 1
WHILE I <= N
  LET S1 = S1 + I
  LET I = I + 1
WEND

LET S2 = 0
FOR J = 1 TO N
  LET S2 = S2 + J
NEXT J

PRINT "WHILE="; S1; " FOR="; S2
END
