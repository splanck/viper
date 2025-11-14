DIM x AS INTEGER
DIM y AS INTEGER
DIM s$ AS STRING

OPEN "/tmp/test_input.txt" FOR OUTPUT AS #1
PRINT #1, "42"
PRINT #1, "100"
PRINT #1, "Hello"
CLOSE #1

OPEN "/tmp/test_input.txt" FOR INPUT AS #1
INPUT #1, x
INPUT #1, y
INPUT #1, s$
CLOSE #1

PRINT x
PRINT y
PRINT s$
