REM Test if stack overflow is cumulative across multiple loops

PRINT "Testing cumulative string concatenation..."
PRINT

REM Do 5 loops of 10 iterations each
DIM i AS INTEGER
DIM j AS INTEGER

FOR j = 1 TO 5
    PRINT "Loop "; j; " of 5"
    DIM s AS STRING
    s = ""
    FOR i = 1 TO 10
        s = s + "X"
    NEXT i
    PRINT "  Length: "; LEN(s)
NEXT j

PRINT
PRINT "All 5 loops completed successfully!"
