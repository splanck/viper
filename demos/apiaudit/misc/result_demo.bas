' result_demo.bas
PRINT "=== Viper.Result Demo ==="
DIM ok AS OBJECT
ok = Viper.Result.OkStr("hello")
PRINT ok.IsOk
PRINT ok.IsErr
PRINT ok.UnwrapStr()
PRINT ok.ToString()
DIM err AS OBJECT
err = Viper.Result.ErrStr("oops")
PRINT err.IsOk
PRINT err.IsErr
PRINT err.UnwrapErrStr()
PRINT err.ToString()
PRINT "done"
END
