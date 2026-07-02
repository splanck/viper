' =============================================================================
' API Audit: Viper.Crypto.Cipher - Symmetric Encryption
' =============================================================================
' Tests: GenerateKey, Encrypt, DecryptResult, TryDecrypt,
'        EncryptWithKey, DecryptWithKeyResult, DeriveKey
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Cipher ==="

' --- GenerateKey ---
PRINT "--- GenerateKey ---"
DIM key AS OBJECT
key = Viper.Crypto.Cipher.GenerateKey()
PRINT "Key generated"
PRINT "Key length: "; Viper.Collections.Bytes.get_Length(key)

' --- Encrypt / Decrypt with password ---
PRINT "--- Encrypt / Decrypt (password) ---"
DIM plain AS OBJECT
plain = Viper.Collections.Bytes.FromStr("secret message")
DIM enc AS OBJECT
enc = Viper.Crypto.Cipher.Encrypt(plain, "mypassword")
PRINT "Encrypted successfully"
DIM dec AS OBJECT
DIM decResult AS OBJECT
decResult = Viper.Crypto.Cipher.DecryptResult(enc, "mypassword")
dec = decResult.Unwrap()
PRINT "Decrypted: "; Viper.Collections.Bytes.ToStr(dec)

DIM bad AS OBJECT
bad = Viper.Crypto.Cipher.TryDecrypt(enc, "wrong-password")
PRINT "Wrong password IsNone: "; bad.IsNone

' --- EncryptWithKey / DecryptWithKey ---
PRINT "--- EncryptWithKey / DecryptWithKey ---"
DIM plain2 AS OBJECT
plain2 = Viper.Collections.Bytes.FromStr("key-based secret")
DIM enc2 AS OBJECT
enc2 = Viper.Crypto.Cipher.EncryptWithKey(plain2, key)
PRINT "Encrypted with key"
DIM dec2 AS OBJECT
DIM dec2Result AS OBJECT
dec2Result = Viper.Crypto.Cipher.DecryptWithKeyResult(enc2, key)
dec2 = dec2Result.Unwrap()
PRINT "Decrypted with key: "; Viper.Collections.Bytes.ToStr(dec2)

' --- DeriveKey ---
PRINT "--- DeriveKey ---"
DIM salt AS OBJECT
salt = Viper.Crypto.Rand.Bytes(16)
DIM derived AS OBJECT
derived = Viper.Crypto.Cipher.DeriveKey("password", salt)
PRINT "Derived key length: "; Viper.Collections.Bytes.get_Length(derived)

PRINT "=== Cipher Demo Complete ==="
END
