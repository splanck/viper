' =============================================================================
' API Audit: Viper.Math.Spline - Curve Interpolation (BASIC)
' =============================================================================
' Tests: CatmullRom, Bezier, Linear, Eval, Tangent, PointCount, PointAt,
'        ArcLength, Sample
' =============================================================================

PRINT "=== API Audit: Viper.Math.Spline ==="

' Build control points for CatmullRom and Linear splines
DIM points AS OBJECT
points = Viper.Collections.Seq.New()
DIM p0 AS Viper.Math.Vec2
p0 = Viper.Math.Vec2.New(0.0, 0.0)
DIM p1 AS Viper.Math.Vec2
p1 = Viper.Math.Vec2.New(1.0, 2.0)
DIM p2 AS Viper.Math.Vec2
p2 = Viper.Math.Vec2.New(3.0, 3.0)
DIM p3 AS Viper.Math.Vec2
p3 = Viper.Math.Vec2.New(5.0, 1.0)
DIM p4 AS Viper.Math.Vec2
p4 = Viper.Math.Vec2.New(7.0, 4.0)
Viper.Collections.Seq.Push(points, p0)
Viper.Collections.Seq.Push(points, p1)
Viper.Collections.Seq.Push(points, p2)
Viper.Collections.Seq.Push(points, p3)
Viper.Collections.Seq.Push(points, p4)

' --- CatmullRom ---
PRINT "--- CatmullRom ---"
DIM cr AS OBJECT
cr = Viper.Math.Spline.CatmullRom(points)
PRINT "CatmullRom created"

' --- Eval ---
PRINT "--- Eval (CatmullRom) ---"
DIM ev0 AS Viper.Math.Vec2
ev0 = Viper.Math.Spline.Eval(cr, 0.0)
PRINT "Eval(0.0) X: "; ev0.X
PRINT "Eval(0.0) Y: "; ev0.Y
DIM ev5 AS Viper.Math.Vec2
ev5 = Viper.Math.Spline.Eval(cr, 0.5)
PRINT "Eval(0.5) X: "; ev5.X
PRINT "Eval(0.5) Y: "; ev5.Y
DIM ev1 AS Viper.Math.Vec2
ev1 = Viper.Math.Spline.Eval(cr, 1.0)
PRINT "Eval(1.0) X: "; ev1.X
PRINT "Eval(1.0) Y: "; ev1.Y

' --- Tangent ---
PRINT "--- Tangent (CatmullRom) ---"
DIM t0 AS Viper.Math.Vec2
t0 = Viper.Math.Spline.Tangent(cr, 0.0)
PRINT "Tangent(0.0) X: "; t0.X
PRINT "Tangent(0.0) Y: "; t0.Y
DIM t5 AS Viper.Math.Vec2
t5 = Viper.Math.Spline.Tangent(cr, 0.5)
PRINT "Tangent(0.5) X: "; t5.X
PRINT "Tangent(0.5) Y: "; t5.Y
DIM t1 AS Viper.Math.Vec2
t1 = Viper.Math.Spline.Tangent(cr, 1.0)
PRINT "Tangent(1.0) X: "; t1.X
PRINT "Tangent(1.0) Y: "; t1.Y

' --- PointCount ---
PRINT "--- PointCount ---"
PRINT "PointCount(CatmullRom): "; Viper.Math.Spline.get_PointCount(cr)

' --- PointAt ---
PRINT "--- PointAt ---"
DIM pa0 AS Viper.Math.Vec2
pa0 = Viper.Math.Spline.PointAt(cr, 0)
PRINT "PointAt(0) X: "; pa0.X
PRINT "PointAt(0) Y: "; pa0.Y
DIM pa2 AS Viper.Math.Vec2
pa2 = Viper.Math.Spline.PointAt(cr, 2)
PRINT "PointAt(2) X: "; pa2.X
PRINT "PointAt(2) Y: "; pa2.Y

' --- ArcLength ---
PRINT "--- ArcLength ---"
PRINT "ArcLength(0.0, 1.0, 100): "; Viper.Math.Spline.ArcLength(cr, 0.0, 1.0, 100)
PRINT "ArcLength(0.0, 0.5, 50): "; Viper.Math.Spline.ArcLength(cr, 0.0, 0.5, 50)

' --- Sample ---
PRINT "--- Sample ---"
DIM samples AS OBJECT
samples = Viper.Math.Spline.Sample(cr, 5)
PRINT "Sample(5) returned a Seq"

' --- Linear ---
PRINT "--- Linear ---"
DIM lin AS OBJECT
lin = Viper.Math.Spline.Linear(points)
PRINT "Linear created"
DIM le0 AS Viper.Math.Vec2
le0 = Viper.Math.Spline.Eval(lin, 0.0)
PRINT "Linear Eval(0.0) X: "; le0.X
PRINT "Linear Eval(0.0) Y: "; le0.Y
DIM le5 AS Viper.Math.Vec2
le5 = Viper.Math.Spline.Eval(lin, 0.5)
PRINT "Linear Eval(0.5) X: "; le5.X
PRINT "Linear Eval(0.5) Y: "; le5.Y
DIM le1 AS Viper.Math.Vec2
le1 = Viper.Math.Spline.Eval(lin, 1.0)
PRINT "Linear Eval(1.0) X: "; le1.X
PRINT "Linear Eval(1.0) Y: "; le1.Y

' --- Bezier ---
PRINT "--- Bezier ---"
DIM bp0 AS Viper.Math.Vec2
bp0 = Viper.Math.Vec2.New(0.0, 0.0)
DIM bp1 AS Viper.Math.Vec2
bp1 = Viper.Math.Vec2.New(0.5, 1.0)
DIM bp2 AS Viper.Math.Vec2
bp2 = Viper.Math.Vec2.New(1.5, 1.0)
DIM bp3 AS Viper.Math.Vec2
bp3 = Viper.Math.Vec2.New(2.0, 0.0)
DIM bez AS OBJECT
bez = Viper.Math.Spline.Bezier(bp0, bp1, bp2, bp3)
PRINT "Bezier created"
DIM be0 AS Viper.Math.Vec2
be0 = Viper.Math.Spline.Eval(bez, 0.0)
PRINT "Bezier Eval(0.0) X: "; be0.X
PRINT "Bezier Eval(0.0) Y: "; be0.Y
DIM be5 AS Viper.Math.Vec2
be5 = Viper.Math.Spline.Eval(bez, 0.5)
PRINT "Bezier Eval(0.5) X: "; be5.X
PRINT "Bezier Eval(0.5) Y: "; be5.Y
DIM be1 AS Viper.Math.Vec2
be1 = Viper.Math.Spline.Eval(bez, 1.0)
PRINT "Bezier Eval(1.0) X: "; be1.X
PRINT "Bezier Eval(1.0) Y: "; be1.Y

' Bezier tangent
PRINT "--- Bezier Tangent ---"
DIM bt5 AS Viper.Math.Vec2
bt5 = Viper.Math.Spline.Tangent(bez, 0.5)
PRINT "Bezier Tangent(0.5) X: "; bt5.X
PRINT "Bezier Tangent(0.5) Y: "; bt5.Y

' Bezier arc length
PRINT "--- Bezier ArcLength ---"
PRINT "Bezier ArcLength(0.0, 1.0, 100): "; Viper.Math.Spline.ArcLength(bez, 0.0, 1.0, 100)

PRINT "=== Spline Audit Complete ==="
END
