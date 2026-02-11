' test_crypto.bas — Viper.Crypto.Hash + Rand + Password
PRINT Viper.Crypto.Hash.MD5("hello")
PRINT Viper.Crypto.Hash.SHA1("hello")
PRINT Viper.Crypto.Hash.SHA256("hello")
PRINT Viper.Crypto.Hash.CRC32("hello")
PRINT Viper.Crypto.Hash.HmacMD5("hello", "key")
PRINT Viper.Crypto.Hash.HmacSHA1("hello", "key")
PRINT Viper.Crypto.Hash.HmacSHA256("hello", "key")

DIM r1 AS INTEGER
LET r1 = Viper.Crypto.Rand.Int(1, 100)
PRINT r1 > 0
PRINT r1 < 101

DIM rb AS OBJECT
LET rb = Viper.Crypto.Rand.Bytes(16)
PRINT rb.Len

DIM pw AS STRING
LET pw = Viper.Crypto.Password.Hash("secret")
PRINT Viper.String.Has(pw, "$")
PRINT Viper.Crypto.Password.Verify("secret", pw)
PRINT Viper.Crypto.Password.Verify("wrong", pw)

PRINT "done"
END
