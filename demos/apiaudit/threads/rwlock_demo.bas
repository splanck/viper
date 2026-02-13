' rwlock_demo.bas
PRINT "=== Viper.Threads.RwLock Demo ==="
DIM lk AS OBJECT
lk = NEW Viper.Threads.RwLock()
PRINT lk.Readers
PRINT lk.IsWriteLocked
PRINT lk.TryReadEnter()
PRINT lk.Readers
lk.ReadExit()
PRINT lk.TryWriteEnter()
PRINT lk.IsWriteLocked
lk.WriteExit()
PRINT "done"
END
