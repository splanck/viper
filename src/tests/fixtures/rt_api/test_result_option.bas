' test_result_option.bas — Result, Option functional types
DIM ok AS OBJECT
DIM err AS OBJECT
DIM some AS OBJECT
DIM none AS OBJECT

ok = Viper.Result.OkI64(42)
err = Viper.Result.ErrStr("boom")
some = Viper.Option.SomeStr("hi")
none = Viper.Option.None()

PRINT Viper.Result.UnwrapI64(ok)
PRINT Viper.Result.UnwrapErrStr(err)
PRINT Viper.Option.UnwrapStr(some)
PRINT ok.IsOk
PRINT none.IsNone
PRINT "done"
END
