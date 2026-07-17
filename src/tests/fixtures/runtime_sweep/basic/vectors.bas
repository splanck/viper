' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Math.Vec2.New
' COVER: Zanna.Math.Vec2.X
' COVER: Zanna.Math.Vec2.Y
' COVER: Zanna.Math.Vec2.Add
' COVER: Zanna.Math.Vec2.Heading
' COVER: Zanna.Math.Vec2.Cross
' COVER: Zanna.Math.Vec2.Dist
' COVER: Zanna.Math.Vec2.Div
' COVER: Zanna.Math.Vec2.Dot
' COVER: Zanna.Math.Vec2.Len
' COVER: Zanna.Math.Vec2.LengthSquared
' COVER: Zanna.Math.Vec2.Lerp
' COVER: Zanna.Math.Vec2.Mul
' COVER: Zanna.Math.Vec2.Negate
' COVER: Zanna.Math.Vec2.Norm
' COVER: Zanna.Math.Vec2.Rotate
' COVER: Zanna.Math.Vec2.Sub
' COVER: Zanna.Math.Vec3.New
' COVER: Zanna.Math.Vec3.X
' COVER: Zanna.Math.Vec3.Y
' COVER: Zanna.Math.Vec3.Z
' COVER: Zanna.Math.Vec3.Add
' COVER: Zanna.Math.Vec3.Cross
' COVER: Zanna.Math.Vec3.Dist
' COVER: Zanna.Math.Vec3.Div
' COVER: Zanna.Math.Vec3.Dot
' COVER: Zanna.Math.Vec3.Len
' COVER: Zanna.Math.Vec3.LengthSquared
' COVER: Zanna.Math.Vec3.Lerp
' COVER: Zanna.Math.Vec3.Mul
' COVER: Zanna.Math.Vec3.Negate
' COVER: Zanna.Math.Vec3.Norm
' COVER: Zanna.Math.Vec3.Sub
' COVER: Zanna.Math.Vec2.Zero
' COVER: Zanna.Math.Vec2.One
' COVER: Zanna.Math.Vec3.Zero
' COVER: Zanna.Math.Vec3.One

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Zanna.Math.Abs(actual - expected) > eps THEN
        Zanna.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM v1 AS Zanna.Math.Vec2
DIM v2 AS Zanna.Math.Vec2
DIM v3 AS Zanna.Math.Vec2
DIM v4 AS Zanna.Math.Vec2

v1 = NEW Zanna.Math.Vec2(3.0, 4.0)
v2 = NEW Zanna.Math.Vec2(1.0, 2.0)

Zanna.Core.Diagnostics.AssertEqNum(v1.Length(), 5.0, "vec2.len")
Zanna.Core.Diagnostics.AssertEqNum(v1.LengthSquared(), 25.0, "vec2.lensq")

v3 = v1.Add(v2)
Zanna.Core.Diagnostics.AssertEqNum(v3.X, 4.0, "vec2.add.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, 6.0, "vec2.add.y")

v3 = v1.Sub(v2)
Zanna.Core.Diagnostics.AssertEqNum(v3.X, 2.0, "vec2.sub.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, 2.0, "vec2.sub.y")

v3 = v1.Mul(2.0)
Zanna.Core.Diagnostics.AssertEqNum(v3.X, 6.0, "vec2.mul.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, 8.0, "vec2.mul.y")

v3 = v1.Div(2.0)
Zanna.Core.Diagnostics.AssertEqNum(v3.X, 1.5, "vec2.div.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, 2.0, "vec2.div.y")

Zanna.Core.Diagnostics.AssertEqNum(v1.Dot(v2), 11.0, "vec2.dot")
Zanna.Core.Diagnostics.AssertEqNum(v1.Cross(v2), 2.0, "vec2.cross")
AssertApprox(v1.Dist(v2), 2.8284271, 0.0001, "vec2.dist")

v3 = v1.Lerp(v2, 0.5)
Zanna.Core.Diagnostics.AssertEqNum(v3.X, 2.0, "vec2.lerp.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, 3.0, "vec2.lerp.y")

v4 = v1.Norm()
AssertApprox(v4.Length(), 1.0, 0.0001, "vec2.norm.len")

DIM angle AS DOUBLE
angle = v2.Heading()
AssertApprox(angle, Zanna.Math.Atan2(v2.Y, v2.X), 0.0001, "vec2.angle")

DIM right AS Zanna.Math.Vec2
DIM up AS Zanna.Math.Vec2
right = NEW Zanna.Math.Vec2(1.0, 0.0)
up = right.Rotate(Zanna.Math.Pi / 2.0)
AssertApprox(up.X, 0.0, 0.0001, "vec2.rotate.x")
AssertApprox(up.Y, 1.0, 0.0001, "vec2.rotate.y")

v3 = v2.Negate()
Zanna.Core.Diagnostics.AssertEqNum(v3.X, -1.0, "vec2.neg.x")
Zanna.Core.Diagnostics.AssertEqNum(v3.Y, -2.0, "vec2.neg.y")

DIM a AS Zanna.Math.Vec3
DIM b AS Zanna.Math.Vec3
DIM c AS Zanna.Math.Vec3

assertApprox(Zanna.Math.Sqrt(14.0), 3.7416573, 0.0001, "vec3.lenref")

a = NEW Zanna.Math.Vec3(1.0, 2.0, 3.0)
b = NEW Zanna.Math.Vec3(2.0, 0.0, 1.0)

Zanna.Core.Diagnostics.AssertEqNum(a.LengthSquared(), 14.0, "vec3.lensq")
AssertApprox(a.Length(), Zanna.Math.Sqrt(14.0), 0.0001, "vec3.len")

c = a.Add(b)
Zanna.Core.Diagnostics.AssertEqNum(c.X, 3.0, "vec3.add.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 2.0, "vec3.add.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, 4.0, "vec3.add.z")

c = a.Sub(b)
Zanna.Core.Diagnostics.AssertEqNum(c.X, -1.0, "vec3.sub.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 2.0, "vec3.sub.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, 2.0, "vec3.sub.z")

c = a.Mul(2.0)
Zanna.Core.Diagnostics.AssertEqNum(c.X, 2.0, "vec3.mul.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 4.0, "vec3.mul.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, 6.0, "vec3.mul.z")

c = a.Div(2.0)
Zanna.Core.Diagnostics.AssertEqNum(c.X, 0.5, "vec3.div.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 1.0, "vec3.div.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, 1.5, "vec3.div.z")

Zanna.Core.Diagnostics.AssertEqNum(a.Dot(b), 5.0, "vec3.dot")

c = a.Cross(b)
Zanna.Core.Diagnostics.AssertEqNum(c.X, 2.0, "vec3.cross.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 5.0, "vec3.cross.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, -4.0, "vec3.cross.z")

Zanna.Core.Diagnostics.AssertEqNum(a.Dist(b), 3.0, "vec3.dist")

c = a.Lerp(b, 0.5)
Zanna.Core.Diagnostics.AssertEqNum(c.X, 1.5, "vec3.lerp.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, 1.0, "vec3.lerp.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, 2.0, "vec3.lerp.z")

c = a.Norm()
AssertApprox(c.Length(), 1.0, 0.0001, "vec3.norm")

c = a.Negate()
Zanna.Core.Diagnostics.AssertEqNum(c.X, -1.0, "vec3.neg.x")
Zanna.Core.Diagnostics.AssertEqNum(c.Y, -2.0, "vec3.neg.y")
Zanna.Core.Diagnostics.AssertEqNum(c.Z, -3.0, "vec3.neg.z")

' Test Vec2.Zero and Vec2.One
DIM zero2 AS Zanna.Math.Vec2
DIM one2 AS Zanna.Math.Vec2
zero2 = Zanna.Math.Vec2.Zero()
one2 = Zanna.Math.Vec2.One()
Zanna.Core.Diagnostics.AssertEqNum(zero2.X, 0.0, "vec2.zero.x")
Zanna.Core.Diagnostics.AssertEqNum(zero2.Y, 0.0, "vec2.zero.y")
Zanna.Core.Diagnostics.AssertEqNum(one2.X, 1.0, "vec2.one.x")
Zanna.Core.Diagnostics.AssertEqNum(one2.Y, 1.0, "vec2.one.y")

' Test Vec3.Zero and Vec3.One
DIM zero3 AS Zanna.Math.Vec3
DIM one3 AS Zanna.Math.Vec3
zero3 = Zanna.Math.Vec3.Zero()
one3 = Zanna.Math.Vec3.One()
Zanna.Core.Diagnostics.AssertEqNum(zero3.X, 0.0, "vec3.zero.x")
Zanna.Core.Diagnostics.AssertEqNum(zero3.Y, 0.0, "vec3.zero.y")
Zanna.Core.Diagnostics.AssertEqNum(zero3.Z, 0.0, "vec3.zero.z")
Zanna.Core.Diagnostics.AssertEqNum(one3.X, 1.0, "vec3.one.x")
Zanna.Core.Diagnostics.AssertEqNum(one3.Y, 1.0, "vec3.one.y")
Zanna.Core.Diagnostics.AssertEqNum(one3.Z, 1.0, "vec3.one.z")

PRINT "RESULT: ok"
END
