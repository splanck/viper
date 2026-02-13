' vec2_demo.bas — Viper.Math.Vec2 (class-based)
PRINT "=== Viper.Math.Vec2 Demo ==="
DIM a AS OBJECT
DIM b AS OBJECT
a = NEW Viper.Math.Vec2(3.0, 4.0)
b = NEW Viper.Math.Vec2(1.0, 2.0)
DIM z AS OBJECT
DIM o AS OBJECT
z = Viper.Math.Vec2.Zero()
o = Viper.Math.Vec2.One()

PRINT a.X
PRINT a.Y
PRINT a.Len()
PRINT a.LenSq()

DIM c AS OBJECT
c = a.Add(b)
PRINT c.X
PRINT c.Y

DIM d AS OBJECT
d = a.Sub(b)
PRINT d.X
PRINT d.Y

DIM e AS OBJECT
e = a.Mul(2.0)
PRINT e.X
PRINT e.Y

DIM f AS OBJECT
f = a.Div(2.0)
PRINT f.X
PRINT f.Y

PRINT a.Dot(b)
PRINT a.Cross(b)
PRINT a.Dist(b)
PRINT a.Angle()

DIM n AS OBJECT
n = a.Norm()
PRINT n.Len()

DIM r AS OBJECT
r = NEW Viper.Math.Vec2(1.0, 0.0)
DIM rotated AS OBJECT
rotated = r.Rotate(1.5707963267949)
PRINT rotated.X
PRINT rotated.Y

DIM l AS OBJECT
l = a.Lerp(b, 0.5)
PRINT l.X
PRINT l.Y

DIM neg AS OBJECT
neg = a.Neg()
PRINT neg.X
PRINT neg.Y

PRINT z.X
PRINT z.Y
PRINT o.X
PRINT o.Y

PRINT "done"
END
