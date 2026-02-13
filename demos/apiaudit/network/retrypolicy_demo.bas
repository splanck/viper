' retrypolicy_demo.bas
PRINT "=== Viper.Network.RetryPolicy Demo ==="
DIM r AS OBJECT
r = NEW Viper.Network.RetryPolicy(3, 100)
PRINT r.MaxRetries
PRINT r.Attempt
PRINT r.CanRetry
PRINT r.IsExhausted
PRINT r.TotalAttempts
PRINT r.NextDelay()
r.Reset()
PRINT r.Attempt
PRINT "done"
END
