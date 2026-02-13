' vec3_demo.bas
PRINT "=== Viper.Math.Vec3 Demo ==="
DIM a AS OBJECT
DIM b AS OBJECT
a = NEW Viper.Math.Vec3(1.0, 2.0, 3.0)
b = NEW Viper.Math.Vec3(4.0, 5.0, 6.0)
PRINT a.X
PRINT a.Y
PRINT a.Z
PRINT a.Len()
PRINT a.LenSq()
DIM c AS OBJECT
c = a.Add(b)
PRINT c.X
DIM d AS OBJECT
d = a.Sub(b)
PRINT d.X
DIM e AS OBJECT
e = a.Mul(2.0)
PRINT e.X
DIM f AS OBJECT
f = a.Div(2.0)
PRINT f.X
PRINT a.Dot(b)
DIM cr AS OBJECT
cr = a.Cross(b)
PRINT cr.X
PRINT a.Dist(b)
DIM n AS OBJECT
n = a.Norm()
PRINT n.Len()
DIM l AS OBJECT
l = a.Lerp(b, 0.5)
PRINT l.X
DIM neg AS OBJECT
neg = a.Neg()
PRINT neg.X
PRINT "done"
END
