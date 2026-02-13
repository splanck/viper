' hash_demo.bas
PRINT "=== Viper.Crypto.Hash Demo ==="
PRINT Viper.Crypto.Hash.MD5("hello")
PRINT Viper.Crypto.Hash.SHA1("hello")
PRINT Viper.Crypto.Hash.SHA256("hello")
PRINT Viper.Crypto.Hash.CRC32("hello")
PRINT Viper.Crypto.Hash.HmacSHA256("hello", "secret")
PRINT "done"
END
