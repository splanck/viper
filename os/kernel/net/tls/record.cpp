/**
 * @file record.cpp
 * @brief TLS 1.3 record layer implementation.
 *
 * @details
 * Implements the record layer declared in `record.hpp`. The implementation
 * supports:
 * - Plaintext records used prior to handshake key establishment.
 * - TLS 1.3 encrypted records using ChaCha20-Poly1305 (AEAD) with TLS nonce
 *   construction based on a per-direction IV and sequence number.
 *
 * The record layer relies on the kernel TCP stack and a polling model
 * (`net::network_poll`) to make progress on reads.
 */

#include "record.hpp"
#include "../ip/tcp.hpp"
#include "../network.hpp"

namespace viper::tls
{

// Helper functions
/**
 * @brief Write a 16-bit value in big-endian order.
 *
 * @param p Output pointer (must have space for 2 bytes).
 * @param v Value to write.
 */
static void write_u16_be(u8 *p, u16 v)
{
    p[0] = static_cast<u8>(v >> 8);
    p[1] = static_cast<u8>(v);
}

/**
 * @brief Read a 16-bit big-endian value from a byte buffer.
 *
 * @param p Input pointer (must have at least 2 bytes).
 * @return Parsed value in host order.
 */
static u16 read_u16_be(const u8 *p)
{
    return (static_cast<u16>(p[0]) << 8) | static_cast<u16>(p[1]);
}

/** @copydoc viper::tls::record_init */
void record_init(RecordLayer *rl, i32 socket_fd)
{
    rl->socket_fd = socket_fd;
    rl->keys_established = false;
    rl->cipher = CipherSuite::TLS_CHACHA20_POLY1305_SHA256;

    rl->write_keys.seq_num = 0;
    rl->read_keys.seq_num = 0;

    rl->read_buffer_len = 0;
    rl->read_buffer_pos = 0;

    // Zero out keys
    for (int i = 0; i < 32; i++)
    {
        rl->write_keys.key[i] = 0;
        rl->read_keys.key[i] = 0;
    }
    for (int i = 0; i < 12; i++)
    {
        rl->write_keys.iv[i] = 0;
        rl->read_keys.iv[i] = 0;
    }
}

/** @copydoc viper::tls::record_set_write_keys */
void record_set_write_keys(RecordLayer *rl, const u8 key[32], const u8 iv[12])
{
    for (int i = 0; i < 32; i++)
        rl->write_keys.key[i] = key[i];
    for (int i = 0; i < 12; i++)
        rl->write_keys.iv[i] = iv[i];
    rl->write_keys.seq_num = 0;
}

/** @copydoc viper::tls::record_set_read_keys */
void record_set_read_keys(RecordLayer *rl, const u8 key[32], const u8 iv[12])
{
    for (int i = 0; i < 32; i++)
        rl->read_keys.key[i] = key[i];
    for (int i = 0; i < 12; i++)
        rl->read_keys.iv[i] = iv[i];
    rl->read_keys.seq_num = 0;
    rl->keys_established = true;
}

/** @copydoc viper::tls::record_build_nonce */
void record_build_nonce(const u8 iv[12], u64 seq_num, u8 nonce[12])
{
    // First 4 bytes are from IV unchanged
    for (int i = 0; i < 4; i++)
    {
        nonce[i] = iv[i];
    }

    // XOR last 8 bytes with sequence number (big-endian)
    for (int i = 0; i < 8; i++)
    {
        nonce[4 + i] = iv[4 + i] ^ static_cast<u8>(seq_num >> (56 - i * 8));
    }
}

// Read exactly n bytes from socket
/**
 * @brief Read exactly `len` bytes from a TCP socket.
 *
 * @details
 * Repeatedly polls the network stack and reads from the TCP socket until the
 * requested number of bytes is read. If the socket returns <= 0 and no bytes
 * were read yet, returns that value.
 *
 * @param sock TCP socket handle.
 * @param buf Output buffer.
 * @param len Number of bytes to read.
 * @return Number of bytes read (== len) on success, or <= 0 on failure.
 */
static i64 read_exact(i32 sock, u8 *buf, usize len)
{
    usize total = 0;
    while (total < len)
    {
        net::network_poll();
        i64 n = net::tcp::socket_recv(sock, buf + total, len - total);
        if (n <= 0)
        {
            if (total == 0)
                return n;
            // Partial read - wait more
            for (int i = 0; i < 10000; i++)
            {
                asm volatile("" ::: "memory");
            }
            continue;
        }
        total += n;
    }
    return static_cast<i64>(total);
}

// Send all bytes
/**
 * @brief Send exactly `len` bytes on a TCP socket.
 *
 * @details
 * Calls the TCP send routine until all bytes are written or an error occurs.
 *
 * @param sock TCP socket handle.
 * @param buf Input bytes.
 * @param len Number of bytes to send.
 * @return Number of bytes sent (== len) on success, or <= 0 on failure.
 */
static i64 send_all(i32 sock, const u8 *buf, usize len)
{
    usize total = 0;
    while (total < len)
    {
        i64 n = net::tcp::socket_send(sock, buf + total, len - total);
        if (n <= 0)
            return n;
        total += n;
    }
    return static_cast<i64>(total);
}

/** @copydoc viper::tls::record_send_plaintext */
i64 record_send_plaintext(RecordLayer *rl, ContentType type, const void *data, usize len)
{
    if (len > TLS_MAX_PLAINTEXT)
    {
        return -1; // Too large
    }

    // Build record header
    u8 header[5];
    header[0] = static_cast<u8>(type);
    write_u16_be(header + 1, TLS_LEGACY_VERSION);
    write_u16_be(header + 3, static_cast<u16>(len));

    // Send header
    i64 result = send_all(rl->socket_fd, header, 5);
    if (result < 0)
        return result;

    // Send data
    if (len > 0)
    {
        result = send_all(rl->socket_fd, static_cast<const u8 *>(data), len);
        if (result < 0)
            return result;
    }

    return static_cast<i64>(len);
}

/** @copydoc viper::tls::record_recv_plaintext */
i64 record_recv_plaintext(RecordLayer *rl, ContentType *type_out, void *buffer, usize max_len)
{
    // Read header
    u8 header[5];
    i64 result = read_exact(rl->socket_fd, header, 5);
    if (result < 5)
        return -1;

    ContentType type = static_cast<ContentType>(header[0]);
    u16 version = read_u16_be(header + 1);
    u16 length = read_u16_be(header + 3);

    // Validate
    if (length > TLS_MAX_CIPHERTEXT)
    {
        return -1; // Record too large
    }

    // Version check (allow 0x0301, 0x0302, 0x0303)
    if (version < TLS_VERSION_1_0 || version > TLS_VERSION_1_2)
    {
        // For TLS 1.3, record layer uses 0x0303
    }

    // Read payload
    if (length > max_len)
    {
        return -1; // Buffer too small
    }

    result = read_exact(rl->socket_fd, static_cast<u8 *>(buffer), length);
    if (result < length)
        return -1;

    *type_out = type;
    return static_cast<i64>(length);
}

/** @copydoc viper::tls::record_send */
i64 record_send(RecordLayer *rl, ContentType type, const void *data, usize len)
{
    if (!rl->keys_established)
    {
        return record_send_plaintext(rl, type, data, len);
    }

    // TLS 1.3 encrypted record:
    // - Outer type is always ApplicationData
    // - Inner content: data || type (1 byte) || padding
    // - Encrypted with AEAD

    if (len > TLS_MAX_PLAINTEXT - 1)
    {
        return -1; // Too large (need room for inner type)
    }

    // Build inner plaintext: data || type
    u8 inner[TLS_MAX_PLAINTEXT + 1];
    const u8 *src = static_cast<const u8 *>(data);
    for (usize i = 0; i < len; i++)
    {
        inner[i] = src[i];
    }
    inner[len] = static_cast<u8>(type);
    usize inner_len = len + 1;

    // Build nonce
    u8 nonce[12];
    record_build_nonce(rl->write_keys.iv, rl->write_keys.seq_num, nonce);

    // Encrypt with ChaCha20-Poly1305
    // AAD: record header (5 bytes)
    u8 aad[5];
    aad[0] = static_cast<u8>(ContentType::ApplicationData);
    write_u16_be(aad + 1, TLS_LEGACY_VERSION);
    u16 ciphertext_len = static_cast<u16>(inner_len + crypto::CHACHA20_POLY1305_TAG_SIZE);
    write_u16_be(aad + 3, ciphertext_len);

    u8 ciphertext[TLS_MAX_CIPHERTEXT];
    crypto::chacha20_poly1305_encrypt(
        rl->write_keys.key, nonce, aad, 5, inner, inner_len, ciphertext);

    // Send record
    i64 result = send_all(rl->socket_fd, aad, 5);
    if (result < 0)
        return result;

    result = send_all(rl->socket_fd, ciphertext, ciphertext_len);
    if (result < 0)
        return result;

    rl->write_keys.seq_num++;

    return static_cast<i64>(len);
}

/** @copydoc viper::tls::record_recv */
i64 record_recv(RecordLayer *rl, ContentType *type_out, void *buffer, usize max_len)
{
    if (!rl->keys_established)
    {
        return record_recv_plaintext(rl, type_out, buffer, max_len);
    }

    // Read header
    u8 header[5];
    i64 result = read_exact(rl->socket_fd, header, 5);
    if (result < 5)
        return -1;

    ContentType outer_type = static_cast<ContentType>(header[0]);
    u16 length = read_u16_be(header + 3);

    // For TLS 1.3, outer type should be ApplicationData
    if (outer_type != ContentType::ApplicationData && outer_type != ContentType::Alert &&
        outer_type != ContentType::ChangeCipherSpec)
    {
        return -1;
    }

    if (length < crypto::CHACHA20_POLY1305_TAG_SIZE)
    {
        return -1; // Too short
    }

    if (length > TLS_MAX_CIPHERTEXT)
    {
        return -1;
    }

    // Read ciphertext
    u8 ciphertext[TLS_MAX_CIPHERTEXT];
    result = read_exact(rl->socket_fd, ciphertext, length);
    if (result < length)
        return -1;

    // Build nonce
    u8 nonce[12];
    record_build_nonce(rl->read_keys.iv, rl->read_keys.seq_num, nonce);

    // AAD is the record header
    u8 aad[5];
    aad[0] = header[0];
    aad[1] = header[1];
    aad[2] = header[2];
    aad[3] = header[3];
    aad[4] = header[4];

    // Decrypt
    u8 plaintext[TLS_MAX_PLAINTEXT + 1];
    i64 plaintext_len = crypto::chacha20_poly1305_decrypt(
        rl->read_keys.key, nonce, aad, 5, ciphertext, length, plaintext);

    if (plaintext_len < 0)
    {
        return -1; // Authentication failed
    }

    rl->read_keys.seq_num++;

    // Remove padding and extract inner type
    // Padding is zeros at the end; find last non-zero byte
    while (plaintext_len > 0 && plaintext[plaintext_len - 1] == 0)
    {
        plaintext_len--;
    }

    if (plaintext_len < 1)
    {
        return -1; // No type byte
    }

    // Last byte is inner content type
    *type_out = static_cast<ContentType>(plaintext[plaintext_len - 1]);
    plaintext_len--;

    // Copy to output buffer
    if (static_cast<usize>(plaintext_len) > max_len)
    {
        return -1; // Buffer too small
    }

    u8 *dst = static_cast<u8 *>(buffer);
    for (i64 i = 0; i < plaintext_len; i++)
    {
        dst[i] = plaintext[i];
    }

    return plaintext_len;
}

} // namespace viper::tls
