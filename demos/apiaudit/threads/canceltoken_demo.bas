' canceltoken_demo.bas
PRINT "=== Viper.Threads.CancelToken Demo ==="
DIM ct AS OBJECT
ct = NEW Viper.Threads.CancelToken()
PRINT ct.IsCancelled
ct.Cancel()
PRINT ct.IsCancelled
ct.Reset()
PRINT ct.IsCancelled
PRINT "done"
END
