' Test INTERFACE and IMPLEMENTS
INTERFACE IDrawable
    SUB Draw()
    FUNCTION GetName() AS STRING
END INTERFACE

CLASS Shape IMPLEMENTS IDrawable
    PUBLIC SUB Draw()
        PRINT "Drawing shape"
    END SUB

    PUBLIC FUNCTION GetName() AS STRING
        RETURN "Shape"
    END FUNCTION
END CLASS

DIM s AS Shape = NEW Shape()
s.Draw()
PRINT s.GetName()
