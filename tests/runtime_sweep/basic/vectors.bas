' EXPECT_OUT: RESULT: ok
' COVER: Viper.Vec2.New
' COVER: Viper.Vec2.X
' COVER: Viper.Vec2.Y
' COVER: Viper.Vec2.Add
' COVER: Viper.Vec2.Angle
' COVER: Viper.Vec2.Cross
' COVER: Viper.Vec2.Dist
' COVER: Viper.Vec2.Div
' COVER: Viper.Vec2.Dot
' COVER: Viper.Vec2.Len
' COVER: Viper.Vec2.LenSq
' COVER: Viper.Vec2.Lerp
' COVER: Viper.Vec2.Mul
' COVER: Viper.Vec2.Neg
' COVER: Viper.Vec2.Norm
' COVER: Viper.Vec2.Rotate
' COVER: Viper.Vec2.Sub
' COVER: Viper.Vec3.New
' COVER: Viper.Vec3.X
' COVER: Viper.Vec3.Y
' COVER: Viper.Vec3.Z
' COVER: Viper.Vec3.Add
' COVER: Viper.Vec3.Cross
' COVER: Viper.Vec3.Dist
' COVER: Viper.Vec3.Div
' COVER: Viper.Vec3.Dot
' COVER: Viper.Vec3.Len
' COVER: Viper.Vec3.LenSq
' COVER: Viper.Vec3.Lerp
' COVER: Viper.Vec3.Mul
' COVER: Viper.Vec3.Neg
' COVER: Viper.Vec3.Norm
' COVER: Viper.Vec3.Sub

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM v1 AS Viper.Vec2
DIM v2 AS Viper.Vec2
DIM v3 AS Viper.Vec2
DIM v4 AS Viper.Vec2

v1 = NEW Viper.Vec2(3.0, 4.0)
v2 = NEW Viper.Vec2(1.0, 2.0)

Viper.Diagnostics.AssertEqNum(v1.Len(), 5.0, "vec2.len")
Viper.Diagnostics.AssertEqNum(v1.LenSq(), 25.0, "vec2.lensq")

v3 = v1.Add(v2)
Viper.Diagnostics.AssertEqNum(v3.X, 4.0, "vec2.add.x")
Viper.Diagnostics.AssertEqNum(v3.Y, 6.0, "vec2.add.y")

v3 = v1.Sub(v2)
Viper.Diagnostics.AssertEqNum(v3.X, 2.0, "vec2.sub.x")
Viper.Diagnostics.AssertEqNum(v3.Y, 2.0, "vec2.sub.y")

v3 = v1.Mul(2.0)
Viper.Diagnostics.AssertEqNum(v3.X, 6.0, "vec2.mul.x")
Viper.Diagnostics.AssertEqNum(v3.Y, 8.0, "vec2.mul.y")

v3 = v1.Div(2.0)
Viper.Diagnostics.AssertEqNum(v3.X, 1.5, "vec2.div.x")
Viper.Diagnostics.AssertEqNum(v3.Y, 2.0, "vec2.div.y")

Viper.Diagnostics.AssertEqNum(v1.Dot(v2), 11.0, "vec2.dot")
Viper.Diagnostics.AssertEqNum(v1.Cross(v2), 2.0, "vec2.cross")
AssertApprox(v1.Dist(v2), 2.8284271, 0.0001, "vec2.dist")

v3 = v1.Lerp(v2, 0.5)
Viper.Diagnostics.AssertEqNum(v3.X, 2.0, "vec2.lerp.x")
Viper.Diagnostics.AssertEqNum(v3.Y, 3.0, "vec2.lerp.y")

v4 = v1.Norm()
AssertApprox(v4.Len(), 1.0, 0.0001, "vec2.norm.len")

DIM angle AS DOUBLE
angle = v2.Angle()
AssertApprox(angle, Viper.Math.Atan2(v2.Y, v2.X), 0.0001, "vec2.angle")

DIM right AS Viper.Vec2
DIM up AS Viper.Vec2
right = NEW Viper.Vec2(1.0, 0.0)
up = right.Rotate(Viper.Math.Pi / 2.0)
AssertApprox(up.X, 0.0, 0.0001, "vec2.rotate.x")
AssertApprox(up.Y, 1.0, 0.0001, "vec2.rotate.y")

v3 = v2.Neg()
Viper.Diagnostics.AssertEqNum(v3.X, -1.0, "vec2.neg.x")
Viper.Diagnostics.AssertEqNum(v3.Y, -2.0, "vec2.neg.y")

DIM a AS Viper.Vec3
DIM b AS Viper.Vec3
DIM c AS Viper.Vec3

assertApprox(Viper.Math.Sqrt(14.0), 3.7416573, 0.0001, "vec3.lenref")

a = NEW Viper.Vec3(1.0, 2.0, 3.0)
b = NEW Viper.Vec3(2.0, 0.0, 1.0)

Viper.Diagnostics.AssertEqNum(a.LenSq(), 14.0, "vec3.lensq")
AssertApprox(a.Len(), Viper.Math.Sqrt(14.0), 0.0001, "vec3.len")

c = a.Add(b)
Viper.Diagnostics.AssertEqNum(c.X, 3.0, "vec3.add.x")
Viper.Diagnostics.AssertEqNum(c.Y, 2.0, "vec3.add.y")
Viper.Diagnostics.AssertEqNum(c.Z, 4.0, "vec3.add.z")

c = a.Sub(b)
Viper.Diagnostics.AssertEqNum(c.X, -1.0, "vec3.sub.x")
Viper.Diagnostics.AssertEqNum(c.Y, 2.0, "vec3.sub.y")
Viper.Diagnostics.AssertEqNum(c.Z, 2.0, "vec3.sub.z")

c = a.Mul(2.0)
Viper.Diagnostics.AssertEqNum(c.X, 2.0, "vec3.mul.x")
Viper.Diagnostics.AssertEqNum(c.Y, 4.0, "vec3.mul.y")
Viper.Diagnostics.AssertEqNum(c.Z, 6.0, "vec3.mul.z")

c = a.Div(2.0)
Viper.Diagnostics.AssertEqNum(c.X, 0.5, "vec3.div.x")
Viper.Diagnostics.AssertEqNum(c.Y, 1.0, "vec3.div.y")
Viper.Diagnostics.AssertEqNum(c.Z, 1.5, "vec3.div.z")

Viper.Diagnostics.AssertEqNum(a.Dot(b), 5.0, "vec3.dot")

c = a.Cross(b)
Viper.Diagnostics.AssertEqNum(c.X, 2.0, "vec3.cross.x")
Viper.Diagnostics.AssertEqNum(c.Y, 5.0, "vec3.cross.y")
Viper.Diagnostics.AssertEqNum(c.Z, -4.0, "vec3.cross.z")

Viper.Diagnostics.AssertEqNum(a.Dist(b), 3.0, "vec3.dist")

c = a.Lerp(b, 0.5)
Viper.Diagnostics.AssertEqNum(c.X, 1.5, "vec3.lerp.x")
Viper.Diagnostics.AssertEqNum(c.Y, 1.0, "vec3.lerp.y")
Viper.Diagnostics.AssertEqNum(c.Z, 2.0, "vec3.lerp.z")

c = a.Norm()
AssertApprox(c.Len(), 1.0, 0.0001, "vec3.norm")

c = a.Neg()
Viper.Diagnostics.AssertEqNum(c.X, -1.0, "vec3.neg.x")
Viper.Diagnostics.AssertEqNum(c.Y, -2.0, "vec3.neg.y")
Viper.Diagnostics.AssertEqNum(c.Z, -3.0, "vec3.neg.z")

PRINT "RESULT: ok"
END
