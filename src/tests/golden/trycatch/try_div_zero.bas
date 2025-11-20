10 TRY
20   LET Z = 0
25   LET X = 1 / Z
30   PRINT "nope"
40 CATCH e
50   PRINT "caught "; STR$(ERR())
60 END TRY
70 PRINT "after"
