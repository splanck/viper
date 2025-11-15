REM Test: Case-insensitive resolution of qualified procedure names

NAMESPACE A.B
  FUNCTION F$()
    F$ = "called"
  END FUNCTION
END NAMESPACE

PRINT a.b.f()
PRINT "done"
END
