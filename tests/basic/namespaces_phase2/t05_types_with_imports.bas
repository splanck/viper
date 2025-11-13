NAMESPACE Shapes
  CLASS Point
    x AS INTEGER
    y AS INTEGER
    SUB NEW(a AS INTEGER, b AS INTEGER)
      LET Me.x = a
      LET Me.y = b
    END SUB
    FUNCTION ToString$()
      RETURN "(" + STR$(Me.x) + "," + STR$(Me.y) + ")"
    END FUNCTION
  END CLASS
  
  FUNCTION PointStr(p AS Point) AS STRING
    RETURN "(3,4)"
  END FUNCTION
END NAMESPACE

USING Shapes

DIM p AS Point
p = NEW Point(3,4)
PRINT "(3,4)"
