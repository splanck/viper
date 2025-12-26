' Simple ON ERROR GOTO test
ON ERROR GOTO HandleError
OPEN "missing_file.txt" FOR INPUT AS #1
PRINT "opened file (unexpected)"
CLOSE #1
GOTO Done

HandleError:
  PRINT "caught error"
  RESUME NextLine

NextLine:
  PRINT "resumed after error"

Done:
  PRINT "done"
