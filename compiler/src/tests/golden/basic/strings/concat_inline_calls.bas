CLASS C
  DIM n$ AS STRING
  SUB Init(x$ AS STRING): n$ = x$: END SUB
  FUNCTION Name$() AS STRING: Name$ = n$: END FUNCTION
END CLASS

DIM c AS C: c = NEW C(): c.Init("Z")
PRINT "[" + c.Name$() + "]"
PRINT c.Name$() + 1
PRINT c.Name$() + c.Name$()

