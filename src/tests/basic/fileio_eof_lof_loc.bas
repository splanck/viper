OPEN "viper_tmp.txt" FOR OUTPUT AS #1
WRITE #1, "X"
CLOSE #1

OPEN "viper_tmp.txt" FOR INPUT AS #1
PRINT "EOF start:"; EOF(#1)  ' expect 0
LINE INPUT #1, A$
PRINT "AFTER READ:"; A$
PRINT "EOF end:"; EOF(#1)    ' expect -1
PRINT "LOF:"; LOF(#1)        ' > 0
PRINT "LOC:"; LOC(#1)        ' >= 0
CLOSE #1
