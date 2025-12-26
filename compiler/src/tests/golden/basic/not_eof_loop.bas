' not_eof_loop.bas — verify DO WHILE NOT EOF(#) works

OPEN "tmp_eof2.txt" FOR OUTPUT AS #1
PRINT #1, "hello"
CLOSE #1

OPEN "tmp_eof2.txt" FOR INPUT AS #1
DIM LINE$
DO WHILE NOT EOF(#1)
  LINE INPUT #1, LINE$
  PRINT "READ:"; LINE$
LOOP
PRINT "EOF now = "; EOF(#1)
CLOSE #1
