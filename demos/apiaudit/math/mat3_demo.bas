' =============================================================================
' API Audit: Viper.Math.Mat3 - 3x3 Matrix Operations (BASIC)
' =============================================================================
' Tests: Identity, Zero, Translate, Scale, ScaleUniform, Rotate, Shear,
'        Get, Row, Col, Add, Sub, Mul, MulScalar, TransformPoint,
'        TransformVec, Transpose, Det, Inverse, Neg, Eq
' =============================================================================

PRINT "=== API Audit: Viper.Math.Mat3 ==="

' --- Identity ---
PRINT "--- Identity ---"
DIM id AS OBJECT
id = Viper.Math.Mat3.Identity()
PRINT "Identity [0,0]: "; Viper.Math.Mat3.Get(id, 0, 0)
PRINT "Identity [1,1]: "; Viper.Math.Mat3.Get(id, 1, 1)
PRINT "Identity [2,2]: "; Viper.Math.Mat3.Get(id, 2, 2)
PRINT "Identity [0,1]: "; Viper.Math.Mat3.Get(id, 0, 1)

' --- Zero ---
PRINT "--- Zero ---"
DIM zr AS OBJECT
zr = Viper.Math.Mat3.Zero()
PRINT "Zero [0,0]: "; Viper.Math.Mat3.Get(zr, 0, 0)
PRINT "Zero [1,1]: "; Viper.Math.Mat3.Get(zr, 1, 1)
PRINT "Zero [2,2]: "; Viper.Math.Mat3.Get(zr, 2, 2)

' --- Translate ---
PRINT "--- Translate ---"
DIM tr AS OBJECT
tr = Viper.Math.Mat3.Translate(5.0, 10.0)
PRINT "Translate(5,10) [0,2]: "; Viper.Math.Mat3.Get(tr, 0, 2)
PRINT "Translate(5,10) [1,2]: "; Viper.Math.Mat3.Get(tr, 1, 2)
PRINT "Translate(5,10) [0,0]: "; Viper.Math.Mat3.Get(tr, 0, 0)

' --- Scale ---
PRINT "--- Scale ---"
DIM sc AS OBJECT
sc = Viper.Math.Mat3.Scale(2.0, 3.0)
PRINT "Scale(2,3) [0,0]: "; Viper.Math.Mat3.Get(sc, 0, 0)
PRINT "Scale(2,3) [1,1]: "; Viper.Math.Mat3.Get(sc, 1, 1)
PRINT "Scale(2,3) [2,2]: "; Viper.Math.Mat3.Get(sc, 2, 2)

' --- ScaleUniform ---
PRINT "--- ScaleUniform ---"
DIM su AS OBJECT
su = Viper.Math.Mat3.ScaleUniform(4.0)
PRINT "ScaleUniform(4) [0,0]: "; Viper.Math.Mat3.Get(su, 0, 0)
PRINT "ScaleUniform(4) [1,1]: "; Viper.Math.Mat3.Get(su, 1, 1)

' --- Rotate ---
PRINT "--- Rotate ---"
DIM rot AS OBJECT
rot = Viper.Math.Mat3.Rotate(1.5707963267948966)
PRINT "Rotate(Pi/2) [0,0]: "; Viper.Math.Mat3.Get(rot, 0, 0)
PRINT "Rotate(Pi/2) [0,1]: "; Viper.Math.Mat3.Get(rot, 0, 1)
PRINT "Rotate(Pi/2) [1,0]: "; Viper.Math.Mat3.Get(rot, 1, 0)
PRINT "Rotate(Pi/2) [1,1]: "; Viper.Math.Mat3.Get(rot, 1, 1)

' --- Shear ---
PRINT "--- Shear ---"
DIM sh AS OBJECT
sh = Viper.Math.Mat3.Shear(1.0, 0.5)
PRINT "Shear(1.0, 0.5) [0,1]: "; Viper.Math.Mat3.Get(sh, 0, 1)
PRINT "Shear(1.0, 0.5) [1,0]: "; Viper.Math.Mat3.Get(sh, 1, 0)

' --- Row ---
PRINT "--- Row ---"
DIM row0 AS Viper.Math.Vec3
row0 = Viper.Math.Mat3.Row(id, 0)
PRINT "Identity Row(0) X: "; row0.X
PRINT "Identity Row(0) Y: "; row0.Y
PRINT "Identity Row(0) Z: "; row0.Z

' --- Col ---
PRINT "--- Col ---"
DIM col1 AS Viper.Math.Vec3
col1 = Viper.Math.Mat3.Col(id, 1)
PRINT "Identity Col(1) X: "; col1.X
PRINT "Identity Col(1) Y: "; col1.Y
PRINT "Identity Col(1) Z: "; col1.Z

' --- Add ---
PRINT "--- Add ---"
DIM msum AS OBJECT
msum = Viper.Math.Mat3.Add(id, id)
PRINT "Identity + Identity [0,0]: "; Viper.Math.Mat3.Get(msum, 0, 0)
PRINT "Identity + Identity [1,1]: "; Viper.Math.Mat3.Get(msum, 1, 1)

' --- Sub ---
PRINT "--- Sub ---"
DIM mdiff AS OBJECT
mdiff = Viper.Math.Mat3.Sub(id, id)
PRINT "Identity - Identity [0,0]: "; Viper.Math.Mat3.Get(mdiff, 0, 0)
PRINT "Identity - Identity [1,1]: "; Viper.Math.Mat3.Get(mdiff, 1, 1)

' --- Mul ---
PRINT "--- Mul ---"
DIM prod AS OBJECT
prod = Viper.Math.Mat3.Mul(sc, tr)
PRINT "Scale(2,3) * Translate(5,10) [0,2]: "; Viper.Math.Mat3.Get(prod, 0, 2)
PRINT "Scale(2,3) * Translate(5,10) [1,2]: "; Viper.Math.Mat3.Get(prod, 1, 2)
DIM prodId AS OBJECT
prodId = Viper.Math.Mat3.Mul(id, sc)
PRINT "Identity * Scale [0,0]: "; Viper.Math.Mat3.Get(prodId, 0, 0)

' --- MulScalar ---
PRINT "--- MulScalar ---"
DIM ms AS OBJECT
ms = Viper.Math.Mat3.MulScalar(id, 3.0)
PRINT "Identity * 3.0 [0,0]: "; Viper.Math.Mat3.Get(ms, 0, 0)
PRINT "Identity * 3.0 [0,1]: "; Viper.Math.Mat3.Get(ms, 0, 1)

' --- TransformPoint ---
PRINT "--- TransformPoint ---"
DIM pt AS Viper.Math.Vec2
pt = Viper.Math.Vec2.New(1.0, 0.0)
DIM tpt AS Viper.Math.Vec2
tpt = Viper.Math.Mat3.TransformPoint(tr, pt)
PRINT "Translate(5,10) * Point(1,0) X: "; tpt.X
PRINT "Translate(5,10) * Point(1,0) Y: "; tpt.Y

' --- TransformVec ---
PRINT "--- TransformVec ---"
DIM tv AS Viper.Math.Vec2
tv = Viper.Math.Mat3.TransformVec(tr, pt)
PRINT "Translate(5,10) * Vec(1,0) X: "; tv.X
PRINT "Translate(5,10) * Vec(1,0) Y: "; tv.Y

' --- Transpose ---
PRINT "--- Transpose ---"
DIM tt AS OBJECT
tt = Viper.Math.Mat3.Transpose(sh)
PRINT "Transpose(Shear) [0,1]: "; Viper.Math.Mat3.Get(tt, 0, 1)
PRINT "Transpose(Shear) [1,0]: "; Viper.Math.Mat3.Get(tt, 1, 0)

' --- Det ---
PRINT "--- Det ---"
PRINT "Det(Identity): "; Viper.Math.Mat3.Det(id)
PRINT "Det(Scale(2,3)): "; Viper.Math.Mat3.Det(sc)

' --- Inverse ---
PRINT "--- Inverse ---"
DIM inv AS OBJECT
inv = Viper.Math.Mat3.Inverse(sc)
PRINT "Inverse(Scale(2,3)) [0,0]: "; Viper.Math.Mat3.Get(inv, 0, 0)
PRINT "Inverse(Scale(2,3)) [1,1]: "; Viper.Math.Mat3.Get(inv, 1, 1)
DIM invId AS OBJECT
invId = Viper.Math.Mat3.Inverse(id)
PRINT "Inverse(Identity) [0,0]: "; Viper.Math.Mat3.Get(invId, 0, 0)

' --- Neg ---
PRINT "--- Neg ---"
DIM ng AS OBJECT
ng = Viper.Math.Mat3.Neg(id)
PRINT "Neg(Identity) [0,0]: "; Viper.Math.Mat3.Get(ng, 0, 0)
PRINT "Neg(Identity) [1,1]: "; Viper.Math.Mat3.Get(ng, 1, 1)

' --- Eq ---
PRINT "--- Eq ---"
PRINT "Eq(Identity, Identity, 0.001): "; Viper.Math.Mat3.Eq(id, id, 0.001)
PRINT "Eq(Identity, Zero, 0.001): "; Viper.Math.Mat3.Eq(id, zr, 0.001)

PRINT "=== Mat3 Audit Complete ==="
END
