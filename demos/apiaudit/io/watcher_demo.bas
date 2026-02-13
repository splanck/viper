' watcher_demo.bas
PRINT "=== Viper.IO.Watcher Demo ==="
DIM w AS OBJECT
w = NEW Viper.IO.Watcher("/tmp")
PRINT w.Path
PRINT w.IsWatching
PRINT "done"
END
