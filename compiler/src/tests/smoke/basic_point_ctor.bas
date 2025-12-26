' File: tests/smoke/basic_point_ctor.bas
' Purpose: Smoke test point class with constructor args, parameter passing, method returns.
' Expectation: prints point state before/after move and destructor message.

CLASS Point
  x AS INTEGER
  y AS INTEGER

  SUB NEW(ix AS INTEGER, iy AS INTEGER)
    LET Me.x = ix
    LET Me.y = iy
  END SUB

  SUB Move(dx AS INTEGER, dy AS INTEGER)
    Me.x = Me.x + dx
    Me.y = Me.y + dy
  END SUB

  FUNCTION ToString$()
    RETURN "(" + STR$(Me.x) + "," + STR$(Me.y) + ")"
  END FUNCTION

  FUNCTION Length#()
    RETURN SQR(Me.x * Me.x + Me.y * Me.y)
  END FUNCTION

  DESTRUCTOR
    PRINT "Point destroyed at "; Me.x; ","; Me.y
  END DESTRUCTOR
END CLASS

DIM p AS Point
p = NEW Point(3, 4)
PRINT "p="; p.ToString$(); " len="; p.Length#()
p.Move(10, -2)
PRINT "p moved to "; p.ToString$(); " len="; p.Length#()
DELETE p
END
