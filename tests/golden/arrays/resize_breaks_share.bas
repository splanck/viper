10 DIM A(2)
15 DIM B(2)
20 LET A(0) = 1
30 LET A(1) = 2
40 LET B = A
50 REDIM A(4)     ' grow triggers copy-on-resize
60 LET A(0) = 9
70 PRINT B(0); A(0)
80 END
