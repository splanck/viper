' EXPECT_OUT: RESULT: ok
' COVER: Viper.Crypto.Legacy.Hash.Crc32
' COVER: Viper.Crypto.Legacy.Hash.Crc32Bytes
' COVER: Viper.Crypto.Legacy.Hash.Md5
' COVER: Viper.Crypto.Legacy.Hash.Md5Bytes
' COVER: Viper.Crypto.Legacy.Hash.Sha1
' COVER: Viper.Crypto.Legacy.Hash.Sha1Bytes
' COVER: Viper.Crypto.Hash.Sha256
' COVER: Viper.Crypto.Hash.Sha256Bytes
' COVER: Viper.Crypto.Legacy.Hash.HmacMd5
' COVER: Viper.Crypto.Legacy.Hash.HmacMd5Bytes
' COVER: Viper.Crypto.Legacy.Hash.HmacSha1
' COVER: Viper.Crypto.Legacy.Hash.HmacSha1Bytes
' COVER: Viper.Crypto.Hash.HmacSha256
' COVER: Viper.Crypto.Hash.HmacSha256Bytes
' COVER: Viper.Crypto.KeyDerive.Pbkdf2Sha256
' COVER: Viper.Crypto.KeyDerive.Pbkdf2Sha256Encoded
' COVER: Viper.Crypto.SecureRandom.Bytes
' COVER: Viper.Crypto.SecureRandom.Int

SUB FillBytes(s AS STRING, b AS Viper.IO.BinaryBuffer)
    DIM i AS INTEGER
    FOR i = 0 TO s.Length - 1
        b.WriteByte(Viper.String.Asc(Viper.String.MidLen(s, i + 1, 1)))
    NEXT i
END SUB

DIM data AS STRING
data = "Hello, World!"

DIM dataBytes AS Viper.IO.BinaryBuffer
dataBytes = Viper.IO.BinaryBuffer.NewCapacity(data.Length)
FillBytes(data, dataBytes)

Viper.Core.Diagnostics.AssertEq(Viper.Crypto.Legacy.Hash.Crc32(data), 3964322768, "crc32")
Viper.Core.Diagnostics.AssertEq(Viper.Crypto.Legacy.Hash.Crc32Bytes(dataBytes), 3964322768, "crc32bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.Md5(data), "65a8e27d8879283831b664bd8b7f0ad4", "md5")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.Sha1(data), "0a0a9f2a6772942557ab5355d76af442f8f65e01", "sha1")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.Sha256(data), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f", "sha256")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.Md5Bytes(dataBytes), "65a8e27d8879283831b664bd8b7f0ad4", "md5bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.Sha1Bytes(dataBytes), "0a0a9f2a6772942557ab5355d76af442f8f65e01", "sha1bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.Sha256Bytes(dataBytes), "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f", "sha256bytes")

DIM keyStr AS STRING
DIM msgStr AS STRING
keyStr = "key"
msgStr = "data"

DIM keyBytes AS Viper.IO.BinaryBuffer
DIM msgBytes AS Viper.IO.BinaryBuffer
keyBytes = Viper.IO.BinaryBuffer.NewCapacity(keyStr.Length)
msgBytes = Viper.IO.BinaryBuffer.NewCapacity(msgStr.Length)
FillBytes(keyStr, keyBytes)
FillBytes(msgStr, msgBytes)

Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.HmacMd5(keyStr, msgStr), "9d5c73ef85594d34ec4438b7c97e51d8", "hmac.md5")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.HmacSha1(keyStr, msgStr), "104152c5bfdca07bc633eebd46199f0255c9f49d", "hmac.sha1")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSha256(keyStr, msgStr), "5031fe3d989c6d1537a013fa6e739da23463fdaec3b70137d828e36ace221bd0", "hmac.sha256")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.HmacMd5Bytes(keyBytes, msgBytes), "9d5c73ef85594d34ec4438b7c97e51d8", "hmac.md5bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Legacy.Hash.HmacSha1Bytes(keyBytes, msgBytes), "104152c5bfdca07bc633eebd46199f0255c9f49d", "hmac.sha1bytes")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.Hash.HmacSha256Bytes(keyBytes, msgBytes), "5031fe3d989c6d1537a013fa6e739da23463fdaec3b70137d828e36ace221bd0", "hmac.sha256bytes")

DIM saltStr AS STRING
saltStr = "salt"
DIM saltBytes AS Viper.IO.BinaryBuffer
saltBytes = Viper.IO.BinaryBuffer.NewCapacity(saltStr.Length)
FillBytes(saltStr, saltBytes)

DIM pbBytes AS Viper.IO.BinaryBuffer
pbBytes = Viper.Crypto.KeyDerive.Pbkdf2Sha256("password", saltBytes, 1000, 32)
Viper.Core.Diagnostics.AssertEq(pbBytes.Length, 32, "pbkdf2.len")
Viper.Core.Diagnostics.AssertEqStr(Viper.Collections.Bytes.ToHex(pbBytes.ToBytes()), "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3", "pbkdf2.hex")
Viper.Core.Diagnostics.AssertEqStr(Viper.Crypto.KeyDerive.Pbkdf2Sha256Encoded("password", saltBytes, 1000, 32), "632c2812e46d4604102ba7618e9d6d7d2f8128f6266b4a03264d2a0460b7dcb3", "pbkdf2.str")

DIM randBytes AS Viper.IO.BinaryBuffer
randBytes = Viper.Crypto.SecureRandom.Bytes(16)
Viper.Core.Diagnostics.AssertEq(randBytes.Length, 16, "rand.bytes")

DIM r AS INTEGER
r = Viper.Crypto.SecureRandom.Int(1, 6)
Viper.Core.Diagnostics.Assert(r >= 1 AND r <= 6, "rand.int")

PRINT "RESULT: ok"
END
