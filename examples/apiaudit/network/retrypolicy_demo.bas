' API Audit: Viper.Network.RetryPolicy (BASIC)
PRINT "=== API Audit: Viper.Network.RetryPolicy ==="

PRINT "--- Exponential ---"
DIM rp AS OBJECT = Viper.Network.RetryPolicy.Exponential(3, 100, 5000)

PRINT "--- Properties ---"
PRINT rp.MaxRetries
PRINT rp.Attempt
PRINT rp.CanRetry
PRINT rp.IsExhausted

PRINT "--- NextDelay ---"
PRINT rp.NextDelay()
PRINT rp.Attempt
PRINT rp.NextDelay()
PRINT rp.Attempt
PRINT rp.NextDelay()
PRINT rp.Attempt

PRINT "--- Exhausted ---"
PRINT rp.CanRetry
PRINT rp.IsExhausted

PRINT "--- TotalAttempts ---"
PRINT rp.TotalAttempts

PRINT "--- Reset ---"
rp.Reset()
PRINT rp.Attempt
PRINT rp.CanRetry

PRINT "=== RetryPolicy Audit Complete ==="
END
