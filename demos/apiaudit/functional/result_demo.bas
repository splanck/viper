' API Audit: Viper.Result (BASIC)
PRINT "=== API Audit: Viper.Result ==="

' --- OkI64 ---
PRINT "--- OkI64 ---"
DIM r1 AS OBJECT = Viper.Result.OkI64(42)
PRINT r1.IsOk
PRINT r1.IsErr
PRINT r1.UnwrapI64()

' --- OkStr ---
PRINT "--- OkStr ---"
DIM r2 AS OBJECT = Viper.Result.OkStr("hello")
PRINT r2.UnwrapStr()

' --- OkF64 ---
PRINT "--- OkF64 ---"
DIM r3 AS OBJECT = Viper.Result.OkF64(3.14)
PRINT r3.UnwrapF64()

' --- ErrStr ---
PRINT "--- ErrStr ---"
DIM r4 AS OBJECT = Viper.Result.ErrStr("something failed")
PRINT r4.IsOk
PRINT r4.IsErr
PRINT r4.UnwrapErrStr()

' --- UnwrapOrI64 ---
PRINT "--- UnwrapOrI64 ---"
PRINT r1.UnwrapOrI64(0)
PRINT r4.UnwrapOrI64(99)

' --- UnwrapOrStr ---
PRINT "--- UnwrapOrStr ---"
PRINT r2.UnwrapOrStr("default")
PRINT r4.UnwrapOrStr("default")

' --- UnwrapOrF64 ---
PRINT "--- UnwrapOrF64 ---"
PRINT r3.UnwrapOrF64(0.0)
PRINT r4.UnwrapOrF64(1.5)

' --- ToString ---
PRINT "--- ToString ---"
PRINT r1.ToString()
PRINT r4.ToString()

' NOTE: Result.Expect(msg) crashes in BASIC VM (BUG A-068 - string arg marshaling)

PRINT "=== Result Audit Complete ==="
END
