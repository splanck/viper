' =============================================================================
' API Audit: Viper.Crypto.Hash - Hash Functions
' =============================================================================
' Tests: SHA256, HmacSHA256, legacy CRC32, legacy MD5/SHA1/HMACs
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Hash ==="
PRINT "SHA256 of 'hello': "; Viper.Crypto.Hash.Sha256("hello")
PRINT "HmacSHA256('hello','key'): "; Viper.Crypto.Hash.HmacSha256("hello", "key")
PRINT "--- Legacy.Hash compatibility ---"
PRINT "CRC32 of 'hello': "; Viper.Crypto.Legacy.Hash.Crc32("hello")
PRINT "MD5 of 'hello': "; Viper.Crypto.Legacy.Hash.Md5("hello")
PRINT "SHA1 of 'hello': "; Viper.Crypto.Legacy.Hash.Sha1("hello")
PRINT "HmacMD5('hello','key'): "; Viper.Crypto.Legacy.Hash.HmacMd5("hello", "key")
PRINT "HmacSHA1('hello','key'): "; Viper.Crypto.Legacy.Hash.HmacSha1("hello", "key")
PRINT "=== Hash Demo Complete ==="
END
