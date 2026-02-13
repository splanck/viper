' option_demo.bas
PRINT "=== Viper.Option Demo ==="
DIM some AS OBJECT
some = Viper.Option.SomeStr("hi")
PRINT some.IsSome
PRINT some.IsNone
PRINT some.UnwrapStr()
PRINT some.ToString()
DIM none AS OBJECT
none = Viper.Option.None()
PRINT none.IsSome
PRINT none.IsNone
PRINT none.UnwrapOrStr("default")
PRINT none.ToString()
PRINT "done"
END
