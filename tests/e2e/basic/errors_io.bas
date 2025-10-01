ON ERROR GOTO Handler
OPEN "does_not_exist.txt" FOR INPUT AS #1
PRINT "this should not print"
END
Handler:
  PRINT "caught"
  RESUME NEXT
