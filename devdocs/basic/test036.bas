DIM line1$ AS STRING
DIM line2$ AS STRING

OPEN "/tmp/test_output.txt" FOR OUTPUT AS #1
PRINT #1, "Hello from BASIC"
PRINT #1, "Line 2"
CLOSE #1

OPEN "/tmp/test_output.txt" FOR INPUT AS #1
LINE INPUT #1, line1$
LINE INPUT #1, line2$
CLOSE #1

PRINT line1$
PRINT line2$
