' box_demo.bas — Viper.Core.Box
PRINT "=== Viper.Core.Box Demo ==="

DIM bi AS OBJECT
DIM bf AS OBJECT
DIM bb AS OBJECT
DIM bs AS OBJECT
bi = Viper.Core.Box.I64(42)
bf = Viper.Core.Box.F64(3.14)
bb = Viper.Core.Box.I1(1)
bs = Viper.Core.Box.Str("hello")

PRINT Viper.Core.Box.ToI64(bi)
PRINT Viper.Core.Box.ToF64(bf)
PRINT Viper.Core.Box.ToI1(bb)
PRINT Viper.Core.Box.ToStr(bs)

PRINT Viper.Core.Box.Type(bi)
PRINT Viper.Core.Box.Type(bf)
PRINT Viper.Core.Box.Type(bb)
PRINT Viper.Core.Box.Type(bs)

PRINT Viper.Core.Box.EqI64(bi, 42)
PRINT Viper.Core.Box.EqI64(bi, 99)
PRINT Viper.Core.Box.EqF64(bf, 3.14)
PRINT Viper.Core.Box.EqStr(bs, "hello")
PRINT Viper.Core.Box.EqStr(bs, "world")

PRINT "done"
END
