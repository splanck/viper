' Verify that static constructor runs during module init before main body executes

CLASS A
  STATIC SUB NEW()
    PRINT "init"
  END SUB
END CLASS

PRINT "main"
END

