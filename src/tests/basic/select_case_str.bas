DIM S$
S$="cat": GOSUB T
S$="dog": GOSUB T
S$="emu": GOSUB T
END
T:
  SELECT CASE S$
    CASE "cat": PRINT "meow"
    CASE "dog": PRINT "woof"
    CASE ELSE:  PRINT "???"
  END SELECT
RETURN
