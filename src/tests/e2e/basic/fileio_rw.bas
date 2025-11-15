ON ERROR GOTO HandlePrint
PRINT #1, 42
ON ERROR GOTO 0
GOTO AfterPrint
HandlePrint:
  PRINT "caught"
  RESUME NEXT
AfterPrint:

ON ERROR GOTO HandleLine
LINE INPUT #1, A$
ON ERROR GOTO 0
END
HandleLine:
  PRINT "caught"
  RESUME NEXT
