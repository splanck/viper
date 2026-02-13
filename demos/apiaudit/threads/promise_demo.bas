' promise_demo.bas
PRINT "=== Viper.Threads.Promise Demo ==="
DIM p AS OBJECT
p = NEW Viper.Threads.Promise()
PRINT p.IsDone
DIM f AS OBJECT
f = p.GetFuture()
PRINT f.IsDone
p.Set("hello")
PRINT p.IsDone
PRINT f.IsDone
PRINT "done"
END
