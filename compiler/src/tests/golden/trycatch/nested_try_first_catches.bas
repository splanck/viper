10 TRY
20   TRY
30     OPEN "missing.txt" FOR INPUT AS #1
40     PRINT "opened"
50   CATCH
60     PRINT "inner"
70   END TRY
80   PRINT "outer-body"
90 CATCH
100  PRINT "outer"
110 END TRY
