' =============================================================================
' API Audit: Viper.Crypto.Cipher - Symmetric Encryption
' =============================================================================
' Tests: GenerateKey, Encrypt, Decrypt, EncryptWithKey, DecryptWithKey,
'        DeriveKey
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Cipher ==="

' --- GenerateKey ---
PRINT "--- GenerateKey ---"
DIM key AS OBJECT
key = Viper.Crypto.Cipher.GenerateKey()
PRINT "Key generated"
PRINT "Key length: "; Viper.Collections.Bytes.get_Len(key)

' --- Encrypt / Decrypt with password ---
PRINT "--- Encrypt / Decrypt (password) ---"
DIM plain AS OBJECT
plain = Viper.Collections.Bytes.FromStr("secret message")
DIM enc AS OBJECT
enc = Viper.Crypto.Cipher.Encrypt(plain, "mypassword")
PRINT "Encrypted successfully"
DIM dec AS OBJECT
dec = Viper.Crypto.Cipher.Decrypt(enc, "mypassword")
PRINT "Decrypted: "; Viper.Collections.Bytes.ToStr(dec)

' --- EncryptWithKey / DecryptWithKey ---
PRINT "--- EncryptWithKey / DecryptWithKey ---"
DIM plain2 AS OBJECT
plain2 = Viper.Collections.Bytes.FromStr("key-based secret")
DIM enc2 AS OBJECT
enc2 = Viper.Crypto.Cipher.EncryptWithKey(plain2, key)
PRINT "Encrypted with key"
DIM dec2 AS OBJECT
dec2 = Viper.Crypto.Cipher.DecryptWithKey(enc2, key)
PRINT "Decrypted with key: "; Viper.Collections.Bytes.ToStr(dec2)

' --- DeriveKey ---
PRINT "--- DeriveKey ---"
DIM salt AS OBJECT
salt = Viper.Crypto.Rand.Bytes(16)
DIM derived AS OBJECT
derived = Viper.Crypto.Cipher.DeriveKey("password", salt)
PRINT "Derived key length: "; Viper.Collections.Bytes.get_Len(derived)

PRINT "=== Cipher Demo Complete ==="
END
