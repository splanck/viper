' test_result_option.bas — Result, Option functional types
DIM ok AS OBJECT
DIM err AS OBJECT
DIM some AS OBJECT
DIM none AS OBJECT

ok = Zanna.Result.OkI64(42)
err = Zanna.Result.ErrStr("boom")
some = Zanna.Option.SomeStr("hi")
none = Zanna.Option.None()

PRINT Zanna.Result.UnwrapI64(ok)
PRINT Zanna.Result.UnwrapErrStr(err)
PRINT Zanna.Option.UnwrapStr(some)
PRINT ok.IsOk
PRINT none.IsNone
PRINT "done"
END
