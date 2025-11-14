' labels, GOTO, GOSUB/RETURN
DIM s$
s$ = "dog"
GOTO AfterSetup

Speak:
  SELECT CASE s$
    CASE "cat": PRINT "meow"
    CASE "dog": PRINT "woof"
    CASE ELSE:  PRINT "???"
  END SELECT
RETURN

AfterSetup:
GOSUB Speak
s$ = "emu": GOSUB Speak
END
