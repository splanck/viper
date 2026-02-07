' EXPECT_OUT: RESULT: ok
' COVER: Viper.Crypto.Hash.CRC32
' COVER: Viper.Crypto.Hash.CRC32Bytes
' COVER: Viper.Crypto.Hash.MD5
' COVER: Viper.Crypto.Hash.MD5Bytes
' COVER: Viper.Crypto.Hash.SHA1
' COVER: Viper.Crypto.Hash.SHA1Bytes
' COVER: Viper.Crypto.Hash.SHA256
' COVER: Viper.Crypto.Hash.SHA256Bytes
' COVER: Viper.Crypto.Hash.HmacMD5
' COVER: Viper.Crypto.Hash.HmacMD5Bytes
' COVER: Viper.Crypto.Hash.HmacSHA1
' COVER: Viper.Crypto.Hash.HmacSHA1Bytes
' COVER: Viper.Crypto.Hash.HmacSHA256
' COVER: Viper.Crypto.Hash.HmacSHA256Bytes
' COVER: Viper.Crypto.KeyDerive.Pbkdf2SHA256
' COVER: Viper.Crypto.KeyDerive.Pbkdf2SHA256Str
' COVER: Viper.Crypto.Rand.Bytes
' COVER: Viper.Crypto.Rand.Int

SUB FillBytes(s AS STRING, b AS Viper.Collections.Bytes)
    DIM i AS INTEGER
    FOR i = 0 TO s.Length - 1
        b.Set(i, Viper.String.Asc(Viper.String.MidLen(s, i + 1, 1)))
    NEXT i
END SUB

DIM data AS STRING
data = "Hello, World!"

DIM dataBytes AS Viper.Collections.Bytes
dataBytes = NEW Viper.Collections.Bytes(data.Length)
FillBytes(data, dataBytes)

Viper.Core.Diagnostics.AssertEq(Viper.Crypto.Hash.CRC32(data), 3964322768, "crc32")
Viper.Core.Diagnostics.AssertEq(Viper.Crypto.Hash.CRC32Bytes(dataBytes), 3964322768, "crc32bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.MD5(data), "65a8e27d8879283831b664bd8b7f0ad4", "md5")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.SHA1(data), "0a0a9f2a6772942557ab5355d76af442f8f65e01", "sha1")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.SHA256(data), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f", "sha256")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.MD5Bytes(dataBytes), "65a8e27d8879283831b664bd8b7f0ad4", "md5bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.SHA1Bytes(dataBytes), "0a0a9f2a6772942557ab5355d76af442f8f65e01", "sha1bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.SHA256Bytes(dataBytes), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f", "sha256bytes")

DIM keyStr AS STRING
DIM msgStr AS STRING
keyStr = "key"
msgStr = "data"

DIM keyBytes AS Viper.Collections.Bytes
DIM msgBytes AS Viper.Collections.Bytes
keyBytes = NEW Viper.Collections.Bytes(keyStr.Length)
msgBytes = NEW Viper.Collections.Bytes(msgStr.Length)
FillBytes(keyStr, keyBytes)
FillBytes(msgStr, msgBytes)

Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacMD5(keyStr, msgStr), "9d5c73ef85594d34ec4438b7c97e51d8", "hmac.md5")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSHA1(keyStr, msgStr), "104152c5bfdca07bc633eebd46199f0255c9f49d", "hmac.sha1")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSHA256(keyStr, msgStr), "5031fe3d989c6d1537a013fa6e739da23463fdaec3b70137d828e36ace221bd0", "hmac.sha256")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacMD5Bytes(keyBytes, msgBytes), "9d5c73ef85594d34ec4438b7c97e51d8", "hmac.md5bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSHA1Bytes(keyBytes, msgBytes), "104152c5bfdca07bc633eebd46199f0255c9f49d", "hmac.sha1bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSHA256Bytes(keyBytes, msgBytes), "5031fe3d989c6d1537a013fa6e739da23463fdaec3b70137d828e36ace221bd0", "hmac.sha256bytes")

DIM saltStr AS STRING
saltStr = "salt"
DIM saltBytes AS Viper.Collections.Bytes
saltBytes = NEW Viper.Collections.Bytes(saltStr.Length)
FillBytes(saltStr, saltBytes)

DIM pbBytes AS Viper.Collections.Bytes
pbBytes = Viper.Crypto.KeyDerive.Pbkdf2SHA256("password", saltBytes, 1000, 32)
Viper.Core.Diagnostics.AssertEq(pbBytes.Len, 32, "pbkdf2.len")
Viper.Core.Diagnostics.AssertEqStr(pbBytes.ToHex(), "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3", "pbkdf2.hex")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("password", saltBytes, 1000, 32), "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3", "pbkdf2.str")

DIM randBytes AS Viper.Collections.Bytes
randBytes = Viper.Crypto.Rand.Bytes(16)
Viper.Core.Diagnostics.AssertEq(randBytes.Len, 16, "rand.bytes")

DIM r AS INTEGER
r = Viper.Crypto.Rand.Int(1, 6)
Viper.Core.Diagnostics.Assert(r >= 1 AND r <= 6, "rand.int")

PRINT "RESULT: ok"
END
