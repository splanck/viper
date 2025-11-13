' ON ERROR GOTO, RESUME, RESUME NEXT
ON ERROR GOTO FailOpen
OPEN "definitely_missing_12345.txt" FOR INPUT AS #1
PRINT "opened file (unexpected)"
CLOSE #1
GOTO AfterOpen

FailOpen:
  PRINT "caught open error; ERR="; ERR
  RESUME NextTest

NextTest:
' Demonstrate RESUME NEXT to skip a failing line
ON ERROR GOTO HandlePrint
PRINT #2, 42          ' #2 not open, will raise
ON ERROR GOTO 0
GOTO Done

HandlePrint:
  PRINT "caught print error; ERR="; ERR
  RESUME NEXT

AfterOpen:
  PRINT "should not reach here"
Done:
  PRINT "done"
