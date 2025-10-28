LET S$ = "dog": GOSUB Speak
LET S$ = "emu": GOSUB Speak
END

Speak:
  SELECT CASE S$
    CASE "cat": PRINT "meow"
    CASE "dog": PRINT "woof"
    CASE ELSE:  PRINT "???"
  END SELECT
RETURN
