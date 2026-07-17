' =============================================================================
' API Audit: Zanna.Crypto.Cipher - Symmetric Encryption
' =============================================================================
' Tests: GenerateKey, Encrypt, DecryptResult, TryDecrypt,
'        EncryptWithKey, DecryptWithKeyResult, DeriveKey
' =============================================================================

PRINT "=== API Audit: Zanna.Crypto.Cipher ==="

' --- GenerateKey ---
PRINT "--- GenerateKey ---"
DIM key AS OBJECT
key = Zanna.Crypto.Cipher.GenerateKey()
PRINT "Key generated"
PRINT "Key length: "; Zanna.Collections.Bytes.get_Length(key)

' --- Encrypt / Decrypt with password ---
PRINT "--- Encrypt / Decrypt (password) ---"
DIM plain AS OBJECT
plain = Zanna.Collections.Bytes.FromStr("secret message")
DIM enc AS OBJECT
enc = Zanna.Crypto.Cipher.Encrypt(plain, "mypassword")
PRINT "Encrypted successfully"
DIM dec AS OBJECT
DIM decResult AS OBJECT
decResult = Zanna.Crypto.Cipher.DecryptResult(enc, "mypassword")
dec = decResult.Unwrap()
PRINT "Decrypted: "; Zanna.Collections.Bytes.ToStr(dec)

DIM bad AS OBJECT
bad = Zanna.Crypto.Cipher.TryDecrypt(enc, "wrong-password")
PRINT "Wrong password IsNone: "; bad.IsNone

' --- EncryptWithKey / DecryptWithKey ---
PRINT "--- EncryptWithKey / DecryptWithKey ---"
DIM plain2 AS OBJECT
plain2 = Zanna.Collections.Bytes.FromStr("key-based secret")
DIM enc2 AS OBJECT
enc2 = Zanna.Crypto.Cipher.EncryptWithKey(plain2, key)
PRINT "Encrypted with key"
DIM dec2 AS OBJECT
DIM dec2Result AS OBJECT
dec2Result = Zanna.Crypto.Cipher.DecryptWithKeyResult(enc2, key)
dec2 = dec2Result.Unwrap()
PRINT "Decrypted with key: "; Zanna.Collections.Bytes.ToStr(dec2)

' --- DeriveKey ---
PRINT "--- DeriveKey ---"
DIM salt AS OBJECT
salt = Zanna.Crypto.SecureRandom.Bytes(16)
DIM derived AS OBJECT
derived = Zanna.Crypto.Cipher.DeriveKey("password", salt)
PRINT "Derived key length: "; Zanna.Collections.Bytes.get_Length(derived)

PRINT "=== Cipher Demo Complete ==="
END
