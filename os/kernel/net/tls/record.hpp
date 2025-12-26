#pragma once

/**
 * @file record.hpp
 * @brief TLS 1.3 record layer for ViperOS.
 *
 * @details
 * The record layer is responsible for framing TLS messages on top of the
 * transport (TCP). It provides:
 * - A plaintext mode used before keys are established (ClientHello/ServerHello).
 * - An encrypted mode used for the remainder of the TLS 1.3 handshake and for
 *   application data.
 *
 * This implementation currently focuses on the TLS 1.3 flow described in
 * RFC 8446 and uses ChaCha20-Poly1305 as the AEAD cipher for encrypted records.
 *
 * The record layer is used by @ref viper::tls::TlsSession and is not intended
 * as a general-purpose TLS library API.
 */

#include "../../include/types.hpp"
#include "crypto/chacha20.hpp"

namespace viper::tls
{

/**
 * @brief TLS record content types.
 *
 * @details
 * In TLS 1.3, encrypted records use an outer content type of ApplicationData,
 * and the true inner content type is appended inside the encrypted payload.
 */
enum class ContentType : u8
{
    Invalid = 0,
    ChangeCipherSpec = 20,
    Alert = 21,
    Handshake = 22,
    ApplicationData = 23,
};

/** @name TLS protocol version constants */
///@{
constexpr u16 TLS_VERSION_1_0 = 0x0301;
constexpr u16 TLS_VERSION_1_2 = 0x0303;
constexpr u16 TLS_VERSION_1_3 = 0x0304;
///@}

/**
 * @brief Legacy record-layer version value used for TLS 1.3.
 *
 * @details
 * TLS 1.3 uses `0x0303` (TLS 1.2) in the record header for compatibility.
 */
constexpr u16 TLS_LEGACY_VERSION = TLS_VERSION_1_2;

/** @name TLS record size limits */
///@{
constexpr usize TLS_MAX_PLAINTEXT = 16384; // 2^14
constexpr usize TLS_MAX_COMPRESSED = TLS_MAX_PLAINTEXT + 1024;
constexpr usize TLS_MAX_CIPHERTEXT = TLS_MAX_COMPRESSED + 1024;
constexpr usize TLS_RECORD_HEADER_SIZE = 5;

///@}

/**
 * @brief TLS record header (conceptual).
 *
 * @details
 * The record header is a 5-byte structure: type (1), legacy version (2), and
 * length (2). The implementation typically builds/parses the header using byte
 * helpers to avoid alignment issues.
 */
struct RecordHeader
{
    ContentType type;
    u16 version;
    u16 length;
};

/**
 * @brief Traffic keys for one record direction (read or write).
 *
 * @details
 * TLS 1.3 uses a per-direction traffic key and IV, plus a monotonically
 * increasing sequence number used to derive a per-record nonce.
 */
struct TrafficKeys
{
    u8 key[32];  // Encryption key
    u8 iv[12];   // Implicit IV (nonce base)
    u64 seq_num; // Sequence number for nonce
};

/**
 * @brief TLS 1.3 cipher suite identifiers.
 *
 * @details
 * The record layer currently uses ChaCha20-Poly1305; other suites are listed
 * for completeness and future expansion.
 */
enum class CipherSuite : u16
{
    TLS_AES_128_GCM_SHA256 = 0x1301,
    TLS_AES_256_GCM_SHA384 = 0x1302,
    TLS_CHACHA20_POLY1305_SHA256 = 0x1303,
};

/**
 * @brief Record layer state for one TLS session.
 *
 * @details
 * Stores socket identity, traffic keys, and a small read buffer used to hold
 * ciphertext/records read from the transport.
 */
struct RecordLayer
{
    // Traffic keys
    TrafficKeys write_keys;
    TrafficKeys read_keys;
    bool keys_established;

    // Cipher suite
    CipherSuite cipher;

    // Buffers
    u8 read_buffer[TLS_MAX_CIPHERTEXT + TLS_RECORD_HEADER_SIZE];
    usize read_buffer_len;
    usize read_buffer_pos;

    // Socket fd
    i32 socket_fd;
};

/**
 * @brief Initialize a record layer instance for a socket.
 *
 * @details
 * Clears internal buffers and marks the record layer as operating in plaintext
 * mode (no traffic keys established).
 *
 * @param rl Record layer instance to initialize.
 * @param socket_fd Connected TCP socket used for transport.
 */
void record_init(RecordLayer *rl, i32 socket_fd);

/**
 * @brief Set the write (client->server) traffic keys.
 *
 * @details
 * Installs the AEAD key and IV used to encrypt outbound records and resets the
 * write sequence number.
 *
 * @param rl Record layer instance.
 * @param key 32-byte AEAD key.
 * @param iv 12-byte IV (nonce base).
 */
void record_set_write_keys(RecordLayer *rl, const u8 key[32], const u8 iv[12]);

/**
 * @brief Set the read (server->client) traffic keys.
 *
 * @details
 * Installs the AEAD key and IV used to decrypt inbound records and resets the
 * read sequence number. This call also marks the record layer as "keys
 * established", enabling encrypted record processing.
 *
 * @param rl Record layer instance.
 * @param key 32-byte AEAD key.
 * @param iv 12-byte IV (nonce base).
 */
void record_set_read_keys(RecordLayer *rl, const u8 key[32], const u8 iv[12]);

/**
 * @brief Send a TLS record.
 *
 * @details
 * If traffic keys are not established, sends a plaintext TLS record with the
 * provided content type.
 *
 * If keys are established (TLS 1.3 encrypted mode), constructs an encrypted
 * record with an outer type of ApplicationData and appends the true inner
 * content type to the plaintext prior to AEAD encryption.
 *
 * @param rl Record layer instance.
 * @param type Inner content type (Handshake, ApplicationData, Alert, ...).
 * @param data Payload bytes.
 * @param len Payload length in bytes.
 * @return Bytes of plaintext payload sent on success, or a negative value on error.
 */
i64 record_send(RecordLayer *rl, ContentType type, const void *data, usize len);

/**
 * @brief Receive a TLS record.
 *
 * @details
 * If keys are not established, reads and returns a plaintext record.
 *
 * If keys are established, reads an encrypted TLS 1.3 record, authenticates and
 * decrypts it, removes padding, and returns the decrypted inner payload. The
 * extracted inner content type is returned via `type_out`.
 *
 * @param rl Record layer instance.
 * @param type_out Output content type of the decrypted record.
 * @param buffer Output buffer for decrypted payload.
 * @param max_len Output buffer capacity.
 * @return Number of decrypted payload bytes, or a negative value on error.
 */
i64 record_recv(RecordLayer *rl, ContentType *type_out, void *buffer, usize max_len);

/**
 * @brief Send a plaintext TLS record (no encryption).
 *
 * @details
 * Used for initial handshake messages prior to traffic key establishment. Most
 * callers should use @ref record_send, which selects plaintext/encrypted mode
 * automatically based on record layer state.
 *
 * @param rl Record layer instance.
 * @param type Record content type.
 * @param data Payload bytes.
 * @param len Payload length.
 * @return Payload length on success, or negative on error.
 */
i64 record_send_plaintext(RecordLayer *rl, ContentType type, const void *data, usize len);

/**
 * @brief Receive a plaintext TLS record (no encryption).
 *
 * @details
 * Reads and returns a plaintext record. Most callers should use @ref record_recv.
 *
 * @param rl Record layer instance.
 * @param type_out Output record content type.
 * @param buffer Output buffer for payload.
 * @param max_len Buffer capacity.
 * @return Payload length on success, or negative on error.
 */
i64 record_recv_plaintext(RecordLayer *rl, ContentType *type_out, void *buffer, usize max_len);

/**
 * @brief Build a per-record nonce from an IV and sequence number.
 *
 * @details
 * TLS 1.3 nonce construction XORs the sequence number (big-endian) into the
 * last 8 bytes of the 12-byte IV, leaving the first 4 bytes unchanged.
 *
 * @param iv Base IV (12 bytes).
 * @param seq_num Record sequence number.
 * @param nonce Output 12-byte nonce.
 */
void record_build_nonce(const u8 iv[12], u64 seq_num, u8 nonce[12]);

} // namespace viper::tls
