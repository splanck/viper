' eof_compare.bas
OPEN "tmp_eof.txt" FOR OUTPUT AS #1
PRINT #1, "hello"
CLOSE #1

OPEN "tmp_eof.txt" FOR INPUT AS #1
DIM LINE$
DO WHILE EOF(#1) = 0
  LINE INPUT #1, LINE$
  PRINT "READ:"; LINE$
LOOP
PRINT "EOF now ="; EOF(#1)
CLOSE #1
