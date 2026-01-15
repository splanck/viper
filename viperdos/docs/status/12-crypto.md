# Cryptography Subsystem

**Status:** Complete for TLS 1.3 and SSH-2
**Location:** `user/libtls/`, `user/libssh/`, `kernel/net/tls/crypto/`
**SLOC:** ~8,000

## Overview

ViperDOS implements cryptographic primitives for both kernel-space and user-space protocols. In microkernel mode:

- **libtls** (user-space): TLS 1.3 client library for applications
- **libssh** (user-space): SSH-2 and SFTP client library
- **Kernel crypto**: Available when `VIPER_KERNEL_ENABLE_TLS=1` (disabled in microkernel mode)

The implementation prioritizes correctness and security over performance, using constant-time algorithms where appropriate.

## Kernel Crypto (`kernel/net/tls/crypto/`)

### Hash Functions

#### SHA-256 (`sha256.cpp`, `sha256.hpp`)
**Status:** Complete

**Implemented:**
- Full SHA-256 hash (256-bit output)
- Incremental hashing via context (init/update/final)
- HMAC-SHA256 construction
- Used for TLS 1.3 PRF (HKDF)

**API:**
```cpp
void sha256(const void *data, size_t len, uint8_t out[32]);
void sha256_init(Sha256Context *ctx);
void sha256_update(Sha256Context *ctx, const void *data, size_t len);
void sha256_final(Sha256Context *ctx, uint8_t out[32]);
void hmac_sha256(const void *key, size_t key_len,
                 const void *data, size_t len, uint8_t out[32]);
```

#### SHA-384 (`sha384.cpp`, `sha384.hpp`)
**Status:** Complete

**Implemented:**
- Full SHA-384 hash (384-bit output)
- Used for TLS 1.3 with AES-256-GCM

#### SHA-1 (`sha1.cpp`, `sha1.hpp`)
**Status:** Complete

**Implemented:**
- Full SHA-1 hash (160-bit output)
- Required for SSH legacy compatibility (hmac-sha1)
- Not used for security-critical operations (only compatibility)

**API:**
```cpp
void sha1(const void *data, size_t len, uint8_t out[20]);
```

### Symmetric Encryption

#### AES-GCM (`aes_gcm.cpp`, `aes_gcm.hpp`)
**Status:** Complete

**Implemented:**
- AES-128-GCM and AES-256-GCM
- AEAD encryption with authentication tag
- Incremental encryption for streaming
- Used as primary TLS 1.3 cipher suite

**API:**
```cpp
class AesGcm {
    void init(const uint8_t *key, size_t key_len);
    bool encrypt(const uint8_t *nonce, size_t nonce_len,
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *plain, size_t plain_len,
                 uint8_t *cipher, uint8_t tag[16]);
    bool decrypt(const uint8_t *nonce, size_t nonce_len,
                 const uint8_t *aad, size_t aad_len,
                 const uint8_t *cipher, size_t cipher_len,
                 const uint8_t *tag,
                 uint8_t *plain);
};
```

#### AES-CTR (`aes_ctr.cpp`, `aes_ctr.hpp`)
**Status:** Complete

**Implemented:**
- AES-128-CTR and AES-256-CTR stream cipher
- Used for SSH transport encryption
- Counter mode with incrementing nonce

**API:**
```cpp
class AesCtr {
    void init(const uint8_t *key, size_t key_len,
              const uint8_t *iv, size_t iv_len);
    void crypt(const uint8_t *in, uint8_t *out, size_t len);
};
```

#### ChaCha20-Poly1305 (`chacha20.cpp`, `chacha20.hpp`)
**Status:** Complete

**Implemented:**
- ChaCha20 stream cipher
- Poly1305 MAC
- AEAD construction (ChaCha20-Poly1305)
- Alternative TLS 1.3 cipher suite

### Key Exchange

#### X25519 (`x25519.cpp`, `x25519.hpp`)
**Status:** Complete

**Implemented:**
- Curve25519 ECDH key agreement
- Scalar multiplication on Edwards curve
- Used for TLS 1.3 and SSH key exchange

**API:**
```cpp
void x25519_keygen(uint8_t public_key[32], uint8_t private_key[32]);
void x25519_scalarmult(uint8_t shared[32],
                       const uint8_t private_key[32],
                       const uint8_t peer_public[32]);
```

### Digital Signatures

#### Ed25519 (`ed25519.cpp`, `ed25519.hpp`)
**Status:** Complete

**Implemented:**
- Ed25519 signature generation
- Ed25519 signature verification
- Used for SSH ssh-ed25519 authentication
- RFC 8032 compliant

**API:**
```cpp
void ed25519_sign(const uint8_t *private_key,
                  const uint8_t *public_key,
                  const uint8_t *message, size_t msg_len,
                  uint8_t signature[64]);
bool ed25519_verify(const uint8_t *public_key,
                    const uint8_t *message, size_t msg_len,
                    const uint8_t *signature);
```

#### RSA (`rsa.cpp`, `rsa.hpp`)
**Status:** Complete

**Implemented:**
- RSA signature verification (PKCS#1 v1.5)
- RSA signature generation (for SSH)
- Big integer arithmetic (add, sub, mul, mod)
- Used for X.509 certificate verification and SSH

**API:**
```cpp
bool rsa_verify_pkcs1(const uint8_t *n, size_t n_len,
                      const uint8_t *e, size_t e_len,
                      const uint8_t *signature, size_t sig_len,
                      const uint8_t *message, size_t msg_len,
                      HashType hash);
bool rsa_sign_pkcs1(const uint8_t *n, size_t n_len,
                    const uint8_t *d, size_t d_len,
                    const uint8_t *message, size_t msg_len,
                    uint8_t *signature, size_t *sig_len,
                    HashType hash);
```

### Key Derivation

#### HKDF (`hkdf.cpp`, `hkdf.hpp`)
**Status:** Complete

**Implemented:**
- HKDF-Extract (from IKM + salt to PRK)
- HKDF-Expand (from PRK + info to OKM)
- HKDF-Expand-Label for TLS 1.3
- Based on HMAC-SHA256/SHA384

### Random Number Generation

#### Random (`random.cpp`, `random.hpp`)
**Status:** Complete

**Implemented:**
- Hardware RNG via VirtIO-RNG
- Fallback to timer-based entropy
- Used for key generation and nonces

**API:**
```cpp
void random_bytes(void *buf, size_t len);
```

---

## User-Space Crypto (`user/libssh/ssh_crypto.c`)

The SSH library includes a simplified user-space crypto implementation:

### Hash Functions
- `ssh_sha256()` - SHA-256 hash
- `ssh_sha1()` - SHA-1 hash (legacy compatibility)
- `ssh_hmac_sha256()` - HMAC-SHA256
- `ssh_hmac_sha1()` - HMAC-SHA1

### Symmetric Encryption
- `ssh_aes_ctr_init()` - Initialize AES-CTR context
- `ssh_aes_ctr_crypt()` - Encrypt/decrypt with AES-CTR

### Key Exchange
- `ssh_x25519_keygen()` - Generate X25519 keypair
- `ssh_x25519_scalarmult()` - X25519 shared secret

### Signatures
- `ssh_ed25519_sign()` - Ed25519 signature
- `ssh_rsa_sign()` - RSA PKCS#1 v1.5 signature

---

## TLS 1.3 Implementation (`kernel/net/tls/`)

### TLS Handshake
**Status:** Complete (client mode)

**Implemented:**
- ClientHello with supported_versions, key_share, signature_algorithms
- ServerHello processing
- EncryptedExtensions, Certificate, CertificateVerify
- Finished message exchange
- Key schedule derivation (early, handshake, application keys)

### Certificate Verification (`kernel/net/tls/cert/`)

#### X.509 Parser (`x509.cpp`, `x509.hpp`)
**Status:** Complete

**Implemented:**
- ASN.1 DER parsing
- Certificate field extraction (subject, issuer, validity, public key)
- Extension parsing (Basic Constraints, Key Usage, SAN)

#### Certificate Chain Verification (`verify.cpp`, `verify.hpp`)
**Status:** Complete

**Implemented:**
- Chain building (leaf to root)
- Signature verification for each certificate
- Validity period checking
- Basic constraints checking
- Root CA trust anchor verification

#### CA Store (`ca_store.cpp`, `ca_store.hpp`)
**Status:** Complete

**Implemented:**
- Built-in root certificates (compiled from Mozilla)
- Certificate lookup by subject
- DER format parsing

### Cipher Suites
**Supported:**
- TLS_AES_128_GCM_SHA256 (0x1301)
- TLS_AES_256_GCM_SHA384 (0x1302)
- TLS_CHACHA20_POLY1305_SHA256 (0x1303)

### Key Groups
**Supported:**
- x25519 (0x001d)

---

## SSH-2 Implementation (`user/libssh/`)

### Transport Layer (`ssh.c`)
**Status:** Complete

**Implemented:**
- Version exchange (SSH-2.0)
- Key exchange init (curve25519-sha256)
- New keys derivation
- Encrypted packet send/receive
- MAC verification (hmac-sha256, hmac-sha1)

**Algorithms:**
| Category | Supported |
|----------|-----------|
| Key Exchange | curve25519-sha256 |
| Host Key | ssh-ed25519, ssh-rsa |
| Encryption | aes128-ctr, aes256-ctr |
| MAC | hmac-sha256, hmac-sha1 |
| Compression | none |

### Authentication (`ssh_auth.c`)
**Status:** Complete

**Implemented:**
- Password authentication
- Public key authentication (Ed25519, RSA)
- OpenSSH private key format parsing (openssh-key-v1)
- Key file loading (~/.ssh/id_ed25519, ~/.ssh/id_rsa)

### Channels (`ssh_channel.c`)
**Status:** Complete

**Implemented:**
- Channel open (session)
- PTY request (xterm, 80x24)
- Shell request
- Exec request
- Subsystem request (for SFTP)
- Channel data read/write
- Exit status handling
- Window size adjustment

### SFTP Protocol (`sftp.c`)
**Status:** Complete (v3)

**Implemented:**
- SFTP v3 protocol
- File operations: open, close, read, write
- Directory operations: opendir, readdir, mkdir, rmdir
- File metadata: stat, fstat, setstat
- Path operations: realpath, rename, remove
- Symbolic links: readlink, symlink

---

## Security Considerations

### Constant-Time Operations
- Field arithmetic in Ed25519/X25519 uses constant-time algorithms
- AES uses table lookups (timing leak possible on some platforms)

### Key Material Handling
- Keys are stored in kernel memory for TLS
- SSH keys are loaded from files into user-space memory
- No explicit memory zeroization on key destruction (TODO)

### Random Number Quality
- VirtIO-RNG provides hardware entropy when available
- Timer-based fallback may have reduced entropy

---

## Priority Recommendations: Next 5 Steps

### 1. Memory Zeroization
**Impact:** Security-critical key material protection
- Explicit memset_s() for key buffers after use
- Compiler-resistant zeroization (no dead store elimination)
- Session key cleanup on TLS/SSH close
- Private key memory protection

### 2. ECDSA Support (secp256r1)
**Impact:** Compatibility with most TLS certificates
- Finite field arithmetic for P-256
- ECDSA signature verification
- Required for many commercial certificates
- More common than Ed25519 in practice

### 3. TLS Session Resumption
**Impact:** Faster HTTPS connections
- Session ticket storage
- 0-RTT early data support (TLS 1.3)
- Reduced handshake latency
- Better user experience for web browsing

### 4. NEON Hardware Acceleration
**Impact:** Significant performance improvement
- AES-NI equivalent via ARMv8 crypto extensions
- SHA-256 acceleration via dedicated instructions
- ChaCha20 vectorization with NEON
- 3-10x speedup for crypto operations

### 5. TLS Server Mode
**Impact:** Enable HTTPS hosting
- Server-side handshake state machine
- Certificate chain sending
- Private key signing for authentication
- Required for web servers on ViperDOS

---

## Files

### Kernel Crypto
| File | Lines | Description |
|------|-------|-------------|
| `sha256.cpp` | ~300 | SHA-256 implementation |
| `sha384.cpp` | ~200 | SHA-384 implementation |
| `sha1.cpp` | ~250 | SHA-1 implementation |
| `aes_gcm.cpp` | ~600 | AES-GCM AEAD |
| `aes_ctr.cpp` | ~200 | AES-CTR stream cipher |
| `chacha20.cpp` | ~400 | ChaCha20-Poly1305 |
| `x25519.cpp` | ~350 | X25519 ECDH |
| `ed25519.cpp` | ~800 | Ed25519 signatures |
| `rsa.cpp` | ~500 | RSA signatures |
| `hkdf.cpp` | ~150 | HKDF key derivation |
| `random.cpp` | ~100 | Random number generation |

### TLS
| File | Lines | Description |
|------|-------|-------------|
| `tls.cpp` | ~1,200 | TLS 1.3 state machine |
| `record.cpp` | ~400 | TLS record layer |
| `x509.cpp` | ~600 | X.509 certificate parsing |
| `verify.cpp` | ~400 | Chain verification |
| `ca_store.cpp` | ~200 | Root CA storage |

### SSH
| File | Lines | Description |
|------|-------|-------------|
| `ssh.c` | ~900 | SSH transport layer |
| `ssh_auth.c` | ~500 | Authentication |
| `ssh_channel.c` | ~600 | Channel management |
| `ssh_crypto.c` | ~1,500 | User-space crypto |
| `sftp.c` | ~800 | SFTP protocol |
