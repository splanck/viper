ON ERROR GOTO H1
x = 1 : y = 0 : PRINT x \ y : PRINT "unreached"
H1:
  PRINT "dz" : RESUME NEXT
  PRINT "cont1"

ON ERROR GOTO H2
OPEN "missing.txt" FOR INPUT AS #1
H2:
  PRINT "fnf" : RESUME NEXT

ON ERROR GOTO H3
DIM a(1 TO 2) : PRINT a(3)
H3:
  PRINT "bounds" : RESUME NEXT
