' password_demo.bas
PRINT "=== Viper.Crypto.Password Demo ==="
DIM h AS STRING
h = Viper.Crypto.Password.Hash("secret")
PRINT (LEN(h) > 0)
PRINT Viper.Crypto.Password.Verify("secret", h)
PRINT Viper.Crypto.Password.Verify("wrong", h)
PRINT "done"
END
