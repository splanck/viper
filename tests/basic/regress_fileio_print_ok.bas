OPEN "viper_tmp.txt" FOR OUTPUT AS #1
PRINT #1, "hello"
CLOSE #1
PRINT "print_ok"
