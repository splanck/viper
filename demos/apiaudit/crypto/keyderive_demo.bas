' keyderive_demo.bas
PRINT "=== Viper.Crypto.KeyDerive Demo ==="
DIM result AS STRING
result = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("password", "salt", 1000, 32)
PRINT LEN(result)
PRINT "done"
END
