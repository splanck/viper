' objectpool_demo.bas
PRINT "=== Viper.Game.ObjectPool Demo ==="
DIM pool AS OBJECT
pool = NEW Viper.Game.ObjectPool(10)
PRINT pool.Capacity
PRINT pool.ActiveCount
PRINT pool.FreeCount
DIM id AS INTEGER
id = pool.Acquire()
PRINT id
PRINT pool.ActiveCount
PRINT pool.IsActive(id)
PRINT pool.Release(id)
PRINT pool.ActiveCount
pool.Clear()
PRINT "done"
END
