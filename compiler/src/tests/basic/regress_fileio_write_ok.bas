OPEN "viper_tmp.txt" FOR OUTPUT AS #1
WRITE #1, "A", 42, "B"
CLOSE #1
PRINT "ok"
