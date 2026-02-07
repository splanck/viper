' Viper.Math Demo - Mathematical Functions and Vectors
' This demo showcases math utilities, random numbers, and vectors

' === Basic Math Functions ===
PRINT "=== Basic Math Functions ==="
PRINT "Abs(-42): "; Viper.Math.Abs(-42)
PRINT "Min(5, 3): "; Viper.Math.Min(5, 3)
PRINT "Max(5, 3): "; Viper.Math.Max(5, 3)
PRINT "Clamp(10, 0, 5): "; Viper.Math.Clamp(10, 0, 5)
PRINT

' === Powers and Roots ===
PRINT "=== Powers and Roots ==="
PRINT "Sqrt(16): "; Viper.Math.Sqrt(16)
PRINT "Pow(2, 8): "; Viper.Math.Pow(2, 8)
PRINT "Log(100): "; Viper.Math.Log(100)
PRINT "Log10(1000): "; Viper.Math.Log10(1000)
PRINT "Exp(1): "; Viper.Math.Exp(1)
PRINT

' === Trigonometry ===
PRINT "=== Trigonometry ==="
PRINT "Sin(0): "; Viper.Math.Sin(0)
PRINT "Cos(0): "; Viper.Math.Cos(0)
PRINT "Tan(0): "; Viper.Math.Tan(0)
PRINT "Pi: "; Viper.Math.Pi
PRINT "Deg(Pi): "; Viper.Math.Deg(Viper.Math.Pi)
PRINT "Rad(180): "; Viper.Math.Rad(180)
PRINT

' === Rounding ===
PRINT "=== Rounding ==="
PRINT "Floor(3.7): "; Viper.Math.Floor(3.7)
PRINT "Ceil(3.2): "; Viper.Math.Ceil(3.2)
PRINT "Round(3.5): "; Viper.Math.Round(3.5)
PRINT "Trunc(3.9): "; Viper.Math.Trunc(3.9)
PRINT

' === Random Numbers ===
PRINT "=== Random Numbers ==="
PRINT "Random.Next(): "; Viper.Math.Random.Next()
PRINT "Random.Next(): "; Viper.Math.Random.Next()
PRINT "Random.NextInt(100): "; Viper.Math.Random.NextInt(100)
PRINT "Random.NextInt(100): "; Viper.Math.Random.NextInt(100)
PRINT

' === Bit Operations ===
PRINT "=== Bit Operations ==="
PRINT "Bits.And(12, 10): "; Viper.Math.Bits.And(12, 10)
PRINT "Bits.Or(12, 10): "; Viper.Math.Bits.Or(12, 10)
PRINT "Bits.Xor(12, 10): "; Viper.Math.Bits.Xor(12, 10)
PRINT "Bits.Not(0): "; Viper.Math.Bits.Not(0)
PRINT "Bits.Shl(1, 4): "; Viper.Math.Bits.Shl(1, 4)
PRINT "Bits.Shr(16, 2): "; Viper.Math.Bits.Shr(16, 2)
PRINT "Bits.Count(255): "; Viper.Math.Bits.Count(255)
PRINT

' === Vec2 (2D Vectors) ===
PRINT "=== Vec2 (2D Vectors) ==="
DIM v1 AS Viper.Math.Vec2
DIM v2 AS Viper.Math.Vec2
DIM v3 AS Viper.Math.Vec2
v1 = NEW Viper.Math.Vec2(3.0, 4.0)
v2 = NEW Viper.Math.Vec2(1.0, 2.0)
PRINT "v1 = (3, 4), v2 = (1, 2)"
PRINT "v1.Len: "; v1.Len()
v3 = v1.Add(v2)
PRINT "v1.Add(v2): ("; v3.X; ", "; v3.Y; ")"
PRINT "v1.Dot(v2): "; v1.Dot(v2)
PRINT

' === Vec3 (3D Vectors) ===
PRINT "=== Vec3 (3D Vectors) ==="
DIM a AS Viper.Math.Vec3
DIM b AS Viper.Math.Vec3
DIM c AS Viper.Math.Vec3
a = NEW Viper.Math.Vec3(1.0, 2.0, 3.0)
b = NEW Viper.Math.Vec3(2.0, 0.0, 1.0)
PRINT "a = (1, 2, 3), b = (2, 0, 1)"
PRINT "a.Len: "; a.Len()
c = a.Cross(b)
PRINT "a.Cross(b): ("; c.X; ", "; c.Y; ", "; c.Z; ")"

END
