' =============================================================================
' API Audit: Viper.Crypto.Hash - Cryptographic Hash Functions
' =============================================================================
' Tests: CRC32, MD5, SHA1, SHA256, HmacMD5, HmacSHA1, HmacSHA256
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Hash ==="
PRINT "CRC32 of 'hello': "; Viper.Crypto.Hash.CRC32("hello")
PRINT "MD5 of 'hello': "; Viper.Crypto.Hash.MD5("hello")
PRINT "SHA1 of 'hello': "; Viper.Crypto.Hash.SHA1("hello")
PRINT "SHA256 of 'hello': "; Viper.Crypto.Hash.SHA256("hello")
PRINT "HmacMD5('hello','key'): "; Viper.Crypto.Hash.HmacMD5("hello", "key")
PRINT "HmacSHA1('hello','key'): "; Viper.Crypto.Hash.HmacSHA1("hello", "key")
PRINT "HmacSHA256('hello','key'): "; Viper.Crypto.Hash.HmacSHA256("hello", "key")
PRINT "=== Hash Demo Complete ==="
END
