' =============================================================================
' API Audit: Viper.Crypto.Hash - Hash Functions
' =============================================================================
' Tests: SHA256, HmacSHA256, legacy CRC32, legacy MD5/SHA1/HMACs
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Hash ==="
PRINT "SHA256 of 'hello': "; Viper.Crypto.Hash.SHA256("hello")
PRINT "HmacSHA256('hello','key'): "; Viper.Crypto.Hash.HmacSHA256("hello", "key")
PRINT "--- Legacy.Hash compatibility ---"
PRINT "CRC32 of 'hello': "; Viper.Crypto.Legacy.Hash.CRC32("hello")
PRINT "MD5 of 'hello': "; Viper.Crypto.Legacy.Hash.MD5("hello")
PRINT "SHA1 of 'hello': "; Viper.Crypto.Legacy.Hash.SHA1("hello")
PRINT "HmacMD5('hello','key'): "; Viper.Crypto.Legacy.Hash.HmacMD5("hello", "key")
PRINT "HmacSHA1('hello','key'): "; Viper.Crypto.Legacy.Hash.HmacSHA1("hello", "key")
PRINT "=== Hash Demo Complete ==="
END
