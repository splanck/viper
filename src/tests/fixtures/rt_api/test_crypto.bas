' test_crypto.bas — Viper.Crypto.Hash + Rand + Password
PRINT Viper.Crypto.Hash.Md5("hello")
PRINT Viper.Crypto.Hash.Sha1("hello")
PRINT Viper.Crypto.Hash.Sha256("hello")
PRINT Viper.Crypto.Hash.Crc32("hello")
PRINT Viper.Crypto.Hash.HmacMd5("hello", "key")
PRINT Viper.Crypto.Hash.HmacSha1("hello", "key")
PRINT Viper.Crypto.Hash.HmacSha256("hello", "key")

DIM r1 AS INTEGER
LET r1 = Viper.Crypto.SecureRandom.Int(1, 100)
PRINT r1 > 0
PRINT r1 < 101

DIM rb AS OBJECT
LET rb = Viper.Crypto.SecureRandom.Bytes(16)
PRINT rb.Length

DIM pw AS STRING
LET pw = Viper.Crypto.Password.Hash("secret")
PRINT Viper.String.Has(pw, "$")
PRINT Viper.Crypto.Password.Verify("secret", pw)
PRINT Viper.Crypto.Password.Verify("wrong", pw)

PRINT "done"
END
