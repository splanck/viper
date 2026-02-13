' API Audit: Viper.Option (BASIC)
PRINT "=== API Audit: Viper.Option ==="

' --- SomeI64 ---
PRINT "--- SomeI64 ---"
DIM o1 AS OBJECT = Viper.Option.SomeI64(42)
PRINT o1.IsSome
PRINT o1.IsNone
PRINT o1.UnwrapI64()

' --- SomeStr ---
PRINT "--- SomeStr ---"
DIM o2 AS OBJECT = Viper.Option.SomeStr("hello")
PRINT o2.UnwrapStr()

' --- SomeF64 ---
PRINT "--- SomeF64 ---"
DIM o3 AS OBJECT = Viper.Option.SomeF64(2.71)
PRINT o3.UnwrapF64()

' --- None ---
PRINT "--- None ---"
DIM o4 AS OBJECT = Viper.Option.None()
PRINT o4.IsSome
PRINT o4.IsNone

' --- UnwrapOrI64 ---
PRINT "--- UnwrapOrI64 ---"
PRINT o1.UnwrapOrI64(0)
PRINT o4.UnwrapOrI64(99)

' --- UnwrapOrStr ---
PRINT "--- UnwrapOrStr ---"
PRINT o2.UnwrapOrStr("default")
PRINT o4.UnwrapOrStr("default")

' --- UnwrapOrF64 ---
PRINT "--- UnwrapOrF64 ---"
PRINT o3.UnwrapOrF64(0.0)
PRINT o4.UnwrapOrF64(1.5)

' --- ToString ---
PRINT "--- ToString ---"
PRINT o1.ToString()
PRINT o4.ToString()

' NOTE: Option.Expect crashes in BASIC (BUG A-068 pattern)
' NOTE: Option.OkOrStr returns Result but BASIC tracks as Option (BUG A-069)

PRINT "=== Option Audit Complete ==="
END
