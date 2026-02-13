' rand_demo.bas
PRINT "=== Viper.Crypto.Rand Demo ==="
DIM n AS LONG
n = Viper.Crypto.Rand.Int(1, 100)
PRINT (n >= 1)
PRINT (n <= 100)
PRINT "done"
END
