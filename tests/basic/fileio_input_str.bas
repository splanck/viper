OPEN "tmp_input.txt" FOR OUTPUT AS #1
PRINT #1, "Alice,42"
CLOSE #1
OPEN "tmp_input.txt" FOR INPUT AS #1
INPUT #1, N$
PRINT N$
CLOSE #1
