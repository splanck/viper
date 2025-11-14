DIM line$ AS STRING

OPEN "/tmp/test_multi.txt" FOR OUTPUT AS #1
PRINT #1, "Line 1"
PRINT #1, "Line 2"
PRINT #1, "Line 3"
CLOSE #1

OPEN "/tmp/test_multi.txt" FOR INPUT AS #1
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    PRINT line$
LOOP
CLOSE #1
