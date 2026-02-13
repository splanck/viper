' API Audit: Viper.Network.RateLimiter (BASIC)
PRINT "=== API Audit: Viper.Network.RateLimiter ==="

PRINT "--- New ---"
DIM rl AS OBJECT = Viper.Network.RateLimiter.New(10, 5.0)

PRINT "--- Properties ---"
PRINT rl.Available
PRINT rl.Max
PRINT rl.Rate

PRINT "--- TryAcquire ---"
PRINT rl.TryAcquire()
PRINT rl.Available

PRINT "--- TryAcquireN ---"
PRINT rl.TryAcquireN(5)
PRINT rl.Available

PRINT "--- TryAcquireN exceed ---"
PRINT rl.TryAcquireN(100)
PRINT rl.Available

PRINT "--- Reset ---"
rl.Reset()
PRINT rl.Available

PRINT "=== RateLimiter Audit Complete ==="
END
