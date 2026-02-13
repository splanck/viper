' ratelimiter_demo.bas
PRINT "=== Viper.Network.RateLimiter Demo ==="
DIM rl AS OBJECT
rl = NEW Viper.Network.RateLimiter(10, 1.0)
PRINT rl.Max
PRINT rl.Available
PRINT rl.TryAcquire()
PRINT rl.Available
rl.Reset()
PRINT rl.Available
PRINT "done"
END
