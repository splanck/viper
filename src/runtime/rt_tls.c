//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_tls.c
/// @brief TLS 1.3 client implementation.
///
/// Implements TLS 1.3 using ChaCha20-Poly1305 AEAD and X25519 key exchange.
//
//===----------------------------------------------------------------------===//

#include "rt_tls.h"
#include "rt_crypto.h"

#include <stdlib.h>
#include <string.h>

// Platform-specific socket includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_ERRNO WSAGetLastError()
#define EINTR_CHECK (SOCKET_ERRNO == WSAEINTR)
#define EAGAIN_CHECK (SOCKET_ERRNO == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET(s) close(s)
#define SOCKET_ERRNO errno
#define EINTR_CHECK (errno == EINTR)
#define EAGAIN_CHECK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

// TLS constants
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

// Content types
#define TLS_CONTENT_CHANGE_CIPHER 20
#define TLS_CONTENT_ALERT 21
#define TLS_CONTENT_HANDSHAKE 22
#define TLS_CONTENT_APPLICATION 23

// Handshake types
#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_ENCRYPTED_EXTENSIONS 8
#define TLS_HS_CERTIFICATE 11
#define TLS_HS_CERTIFICATE_VERIFY 15
#define TLS_HS_FINISHED 20

// Cipher suite
#define TLS_CHACHA20_POLY1305_SHA256 0x1303

// Extensions
#define TLS_EXT_SERVER_NAME 0
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_KEY_SHARE 51

// Max sizes
#define TLS_MAX_RECORD_SIZE 16384
#define TLS_MAX_CIPHERTEXT (TLS_MAX_RECORD_SIZE + 256)

// Handshake states
typedef enum
{
    TLS_STATE_INITIAL,
    TLS_STATE_CLIENT_HELLO_SENT,
    TLS_STATE_SERVER_HELLO_RECEIVED,
    TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS,
    TLS_STATE_WAIT_CERTIFICATE,
    TLS_STATE_WAIT_CERTIFICATE_VERIFY,
    TLS_STATE_WAIT_FINISHED,
    TLS_STATE_CONNECTED,
    TLS_STATE_CLOSED,
    TLS_STATE_ERROR
} tls_state_t;

// Traffic keys
typedef struct
{
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t seq_num;
} traffic_keys_t;

// TLS session structure
struct rt_tls_session
{
    socket_t socket_fd;
    tls_state_t state;
    const char *error;

    // Configuration
    char hostname[256];
    int verify_cert;

    // Handshake state
    uint8_t client_private_key[32];
    uint8_t client_public_key[32];
    uint8_t server_public_key[32];
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint16_t cipher_suite;

    // Key schedule
    uint8_t handshake_secret[32];
    uint8_t client_handshake_traffic_secret[32];
    uint8_t server_handshake_traffic_secret[32];
    uint8_t master_secret[32];
    uint8_t client_application_traffic_secret[32];
    uint8_t server_application_traffic_secret[32];

    // Transcript hash
    uint8_t transcript_hash[32];
    uint8_t transcript_buffer[8192];
    size_t transcript_len;

    // Record layer
    traffic_keys_t write_keys;
    traffic_keys_t read_keys;
    int keys_established;

    // Read buffer
    uint8_t read_buffer[TLS_MAX_CIPHERTEXT];
    size_t read_buffer_len;
    size_t read_buffer_pos;

    // Decrypted application data buffer
    uint8_t app_buffer[TLS_MAX_RECORD_SIZE];
    size_t app_buffer_len;
    size_t app_buffer_pos;
};

// Helper: write big-endian uint16
static void write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

// Helper: write big-endian uint24
static void write_u24(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 16) & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = v & 0xFF;
}

// Helper: read big-endian uint16
static uint16_t read_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

// Helper: read big-endian uint24
static uint32_t read_u24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

// Update transcript hash
static void transcript_update(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (session->transcript_len + len <= sizeof(session->transcript_buffer))
    {
        memcpy(session->transcript_buffer + session->transcript_len, data, len);
        session->transcript_len += len;
    }
    // Compute running hash
    rt_sha256(session->transcript_buffer, session->transcript_len, session->transcript_hash);
}

// Derive handshake traffic keys
static void derive_handshake_keys(rt_tls_session_t *session, const uint8_t shared_secret[32])
{
    uint8_t zero_key[32] = {0};
    uint8_t early_secret[32];
    uint8_t derived[32];

    // early_secret = HKDF-Extract(0, 0)
    rt_hkdf_extract(NULL, 0, zero_key, 32, early_secret);

    // derived = Derive-Secret(early_secret, "derived", "")
    uint8_t empty_hash[32];
    rt_sha256(NULL, 0, empty_hash);
    rt_hkdf_expand_label(early_secret, "derived", empty_hash, 32, derived, 32);

    // handshake_secret = HKDF-Extract(derived, shared_secret)
    rt_hkdf_extract(derived, 32, shared_secret, 32, session->handshake_secret);

    // client_handshake_traffic_secret
    rt_hkdf_expand_label(session->handshake_secret,
                         "c hs traffic",
                         session->transcript_hash,
                         32,
                         session->client_handshake_traffic_secret,
                         32);

    // server_handshake_traffic_secret
    rt_hkdf_expand_label(session->handshake_secret,
                         "s hs traffic",
                         session->transcript_hash,
                         32,
                         session->server_handshake_traffic_secret,
                         32);

    // Derive keys
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "key", NULL, 0, session->read_keys.key, 32);
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "key", NULL, 0, session->write_keys.key, 32);
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;

    session->keys_established = 1;
}

// Derive application traffic keys
static void derive_application_keys(rt_tls_session_t *session)
{
    uint8_t derived[32];
    uint8_t zero_key[32] = {0};
    uint8_t empty_hash[32];

    rt_sha256(NULL, 0, empty_hash);
    rt_hkdf_expand_label(session->handshake_secret, "derived", empty_hash, 32, derived, 32);

    // master_secret = HKDF-Extract(derived, 0)
    rt_hkdf_extract(derived, 32, zero_key, 32, session->master_secret);

    // client_application_traffic_secret
    rt_hkdf_expand_label(session->master_secret,
                         "c ap traffic",
                         session->transcript_hash,
                         32,
                         session->client_application_traffic_secret,
                         32);

    // server_application_traffic_secret
    rt_hkdf_expand_label(session->master_secret,
                         "s ap traffic",
                         session->transcript_hash,
                         32,
                         session->server_application_traffic_secret,
                         32);

    // Derive application keys
    rt_hkdf_expand_label(
        session->server_application_traffic_secret, "key", NULL, 0, session->read_keys.key, 32);
    rt_hkdf_expand_label(
        session->server_application_traffic_secret, "iv", NULL, 0, session->read_keys.iv, 12);
    session->read_keys.seq_num = 0;

    rt_hkdf_expand_label(
        session->client_application_traffic_secret, "key", NULL, 0, session->write_keys.key, 32);
    rt_hkdf_expand_label(
        session->client_application_traffic_secret, "iv", NULL, 0, session->write_keys.iv, 12);
    session->write_keys.seq_num = 0;
}

// Build nonce from IV and sequence number
static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12])
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
    {
        nonce[12 - 1 - i] ^= (seq >> (i * 8)) & 0xFF;
    }
}

// Send TLS record
static int send_record(rt_tls_session_t *session, uint8_t content_type, const uint8_t *data, size_t len)
{
    uint8_t record[5 + TLS_MAX_CIPHERTEXT];
    size_t record_len;

    if (session->keys_established)
    {
        // Encrypted record
        uint8_t plaintext[TLS_MAX_RECORD_SIZE + 1];
        memcpy(plaintext, data, len);
        plaintext[len] = content_type; // Inner content type

        uint8_t aad[5];
        aad[0] = TLS_CONTENT_APPLICATION;
        write_u16(aad + 1, TLS_VERSION_1_2);
        write_u16(aad + 3, (uint16_t)(len + 1 + 16)); // plaintext + content type + tag

        uint8_t nonce[12];
        build_nonce(session->write_keys.iv, session->write_keys.seq_num, nonce);

        record[0] = TLS_CONTENT_APPLICATION;
        write_u16(record + 1, TLS_VERSION_1_2);
        size_t ciphertext_len = rt_chacha20_poly1305_encrypt(
            session->write_keys.key, nonce, aad, 5, plaintext, len + 1, record + 5);
        write_u16(record + 3, (uint16_t)ciphertext_len);
        record_len = 5 + ciphertext_len;

        session->write_keys.seq_num++;
    }
    else
    {
        // Plaintext record
        record[0] = content_type;
        write_u16(record + 1, TLS_VERSION_1_2);
        write_u16(record + 3, (uint16_t)len);
        memcpy(record + 5, data, len);
        record_len = 5 + len;
    }

    size_t sent = 0;
    while (sent < record_len)
    {
        int n = send(session->socket_fd, (const char *)(record + sent), (int)(record_len - sent), 0);
        if (n < 0)
        {
            if (EINTR_CHECK)
                continue;
            session->error = "send failed";
            return RT_TLS_ERROR_SOCKET;
        }
        sent += n;
    }

    return RT_TLS_OK;
}

// Receive TLS record
static int recv_record(rt_tls_session_t *session, uint8_t *content_type, uint8_t *data, size_t *data_len)
{
    // Read header
    uint8_t header[5];
    size_t pos = 0;
    while (pos < 5)
    {
        int n = recv(session->socket_fd, (char *)(header + pos), (int)(5 - pos), 0);
        if (n < 0)
        {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv header failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0)
        {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    uint8_t type = header[0];
    size_t length = read_u16(header + 3);

    if (length > TLS_MAX_CIPHERTEXT)
    {
        session->error = "record too large";
        return RT_TLS_ERROR;
    }

    // Read payload
    uint8_t payload[TLS_MAX_CIPHERTEXT];
    pos = 0;
    while (pos < length)
    {
        int n = recv(session->socket_fd, (char *)(payload + pos), (int)(length - pos), 0);
        if (n < 0)
        {
            if (EINTR_CHECK || EAGAIN_CHECK)
                continue;
            session->error = "recv payload failed";
            return RT_TLS_ERROR_SOCKET;
        }
        if (n == 0)
        {
            session->error = "connection closed";
            return RT_TLS_ERROR_CLOSED;
        }
        pos += n;
    }

    if (session->keys_established && type == TLS_CONTENT_APPLICATION)
    {
        // Decrypt
        uint8_t aad[5];
        memcpy(aad, header, 5);

        uint8_t nonce[12];
        build_nonce(session->read_keys.iv, session->read_keys.seq_num, nonce);

        long plaintext_len = rt_chacha20_poly1305_decrypt(
            session->read_keys.key, nonce, aad, 5, payload, length, data);
        if (plaintext_len < 0)
        {
            session->error = "decryption failed";
            return RT_TLS_ERROR;
        }

        session->read_keys.seq_num++;

        // Remove padding and get inner content type
        while (plaintext_len > 0 && data[plaintext_len - 1] == 0)
            plaintext_len--;
        if (plaintext_len == 0)
        {
            session->error = "empty inner record";
            return RT_TLS_ERROR;
        }
        *content_type = data[plaintext_len - 1];
        *data_len = plaintext_len - 1;
    }
    else
    {
        // Plaintext record
        *content_type = type;
        memcpy(data, payload, length);
        *data_len = length;
    }

    return RT_TLS_OK;
}

// Build and send ClientHello
static int send_client_hello(rt_tls_session_t *session)
{
    uint8_t msg[512];
    size_t pos = 0;

    // Legacy version
    write_u16(msg + pos, TLS_VERSION_1_2);
    pos += 2;

    // Random
    rt_crypto_random_bytes(session->client_random, 32);
    memcpy(msg + pos, session->client_random, 32);
    pos += 32;

    // Session ID (empty for TLS 1.3)
    msg[pos++] = 0;

    // Cipher suites
    write_u16(msg + pos, 2);
    pos += 2;
    write_u16(msg + pos, TLS_CHACHA20_POLY1305_SHA256);
    pos += 2;

    // Compression methods
    msg[pos++] = 1;
    msg[pos++] = 0; // null

    // Extensions
    size_t ext_start = pos;
    pos += 2; // Length placeholder

    // SNI extension
    if (session->hostname[0] != '\0')
    {
        size_t name_len = strlen(session->hostname);
        write_u16(msg + pos, TLS_EXT_SERVER_NAME);
        pos += 2;
        write_u16(msg + pos, (uint16_t)(name_len + 5));
        pos += 2;
        write_u16(msg + pos, (uint16_t)(name_len + 3));
        pos += 2;
        msg[pos++] = 0; // DNS hostname
        write_u16(msg + pos, (uint16_t)name_len);
        pos += 2;
        memcpy(msg + pos, session->hostname, name_len);
        pos += name_len;
    }

    // Supported versions
    write_u16(msg + pos, TLS_EXT_SUPPORTED_VERSIONS);
    pos += 2;
    write_u16(msg + pos, 3);
    pos += 2;
    msg[pos++] = 2;
    write_u16(msg + pos, TLS_VERSION_1_3);
    pos += 2;

    // Key share (X25519)
    rt_x25519_keygen(session->client_private_key, session->client_public_key);

    write_u16(msg + pos, TLS_EXT_KEY_SHARE);
    pos += 2;
    write_u16(msg + pos, 36);
    pos += 2;
    write_u16(msg + pos, 34); // client shares length
    pos += 2;
    write_u16(msg + pos, 0x001D); // x25519
    pos += 2;
    write_u16(msg + pos, 32);
    pos += 2;
    memcpy(msg + pos, session->client_public_key, 32);
    pos += 32;

    // Fill in extensions length
    write_u16(msg + ext_start, (uint16_t)(pos - ext_start - 2));

    // Wrap in handshake header
    uint8_t hs[4 + 512];
    hs[0] = TLS_HS_CLIENT_HELLO;
    write_u24(hs + 1, (uint32_t)pos);
    memcpy(hs + 4, msg, pos);

    // Update transcript
    transcript_update(session, hs, 4 + pos);

    // Send
    int rc = send_record(session, TLS_CONTENT_HANDSHAKE, hs, 4 + pos);
    if (rc != RT_TLS_OK)
        return rc;

    session->state = TLS_STATE_CLIENT_HELLO_SENT;
    return RT_TLS_OK;
}

// Process ServerHello
static int process_server_hello(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len < 38)
    {
        session->error = "ServerHello too short";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip version (2)
    memcpy(session->server_random, data + 2, 32);

    size_t pos = 34;

    // Session ID
    uint8_t session_id_len = data[pos++];
    pos += session_id_len;

    // Cipher suite
    session->cipher_suite = read_u16(data + pos);
    pos += 2;

    if (session->cipher_suite != TLS_CHACHA20_POLY1305_SHA256)
    {
        session->error = "unsupported cipher suite";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Skip compression
    pos++;

    // Parse extensions
    if (pos + 2 > len)
    {
        session->error = "no extensions";
        return RT_TLS_ERROR_HANDSHAKE;
    }
    uint16_t ext_len = read_u16(data + pos);
    pos += 2;

    size_t ext_end = pos + ext_len;
    int found_key_share = 0;

    while (pos + 4 <= ext_end)
    {
        uint16_t ext_type = read_u16(data + pos);
        uint16_t ext_data_len = read_u16(data + pos + 2);
        pos += 4;

        if (ext_type == TLS_EXT_KEY_SHARE && ext_data_len >= 36)
        {
            uint16_t group = read_u16(data + pos);
            uint16_t key_len = read_u16(data + pos + 2);
            if (group == 0x001D && key_len == 32)
            {
                memcpy(session->server_public_key, data + pos + 4, 32);
                found_key_share = 1;
            }
        }
        pos += ext_data_len;
    }

    if (!found_key_share)
    {
        session->error = "no key share";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    // Compute shared secret and derive handshake keys
    uint8_t shared_secret[32];
    rt_x25519(session->client_private_key, session->server_public_key, shared_secret);
    derive_handshake_keys(session, shared_secret);

    session->state = TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS;
    return RT_TLS_OK;
}

// Send Finished message
static int send_finished(rt_tls_session_t *session)
{
    uint8_t finished_key[32];
    rt_hkdf_expand_label(
        session->client_handshake_traffic_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t verify_data[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, verify_data);

    uint8_t msg[4 + 32];
    msg[0] = TLS_HS_FINISHED;
    write_u24(msg + 1, 32);
    memcpy(msg + 4, verify_data, 32);

    transcript_update(session, msg, 36);

    return send_record(session, TLS_CONTENT_HANDSHAKE, msg, 36);
}

// Verify server Finished
static int verify_finished(rt_tls_session_t *session, const uint8_t *data, size_t len)
{
    if (len != 32)
    {
        session->error = "invalid Finished length";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    uint8_t finished_key[32];
    rt_hkdf_expand_label(
        session->server_handshake_traffic_secret, "finished", NULL, 0, finished_key, 32);

    uint8_t expected[32];
    rt_hmac_sha256(finished_key, 32, session->transcript_hash, 32, expected);

    if (memcmp(data, expected, 32) != 0)
    {
        session->error = "Finished verification failed";
        return RT_TLS_ERROR_HANDSHAKE;
    }

    return RT_TLS_OK;
}

//=============================================================================
// Public API
//=============================================================================

void rt_tls_config_init(rt_tls_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->verify_cert = 1;
    config->timeout_ms = 30000;
}

rt_tls_session_t *rt_tls_new(int socket_fd, const rt_tls_config_t *config)
{
    rt_tls_session_t *session = (rt_tls_session_t *)calloc(1, sizeof(rt_tls_session_t));
    if (!session)
        return NULL;

    session->socket_fd = socket_fd;
    session->state = TLS_STATE_INITIAL;
    session->verify_cert = config ? config->verify_cert : 1;

    if (config && config->hostname)
    {
        strncpy(session->hostname, config->hostname, sizeof(session->hostname) - 1);
    }

    return session;
}

int rt_tls_handshake(rt_tls_session_t *session)
{
    if (!session)
        return RT_TLS_ERROR_INVALID_ARG;

    if (session->state != TLS_STATE_INITIAL)
    {
        session->error = "invalid state for handshake";
        return RT_TLS_ERROR;
    }

    // Send ClientHello
    int rc = send_client_hello(session);
    if (rc != RT_TLS_OK)
        return rc;

    // Process handshake messages
    while (session->state != TLS_STATE_CONNECTED && session->state != TLS_STATE_ERROR)
    {
        uint8_t content_type;
        uint8_t data[TLS_MAX_RECORD_SIZE];
        size_t data_len;

        rc = recv_record(session, &content_type, data, &data_len);
        if (rc != RT_TLS_OK)
            return rc;

        if (content_type == TLS_CONTENT_ALERT)
        {
            session->error = "received alert";
            session->state = TLS_STATE_ERROR;
            return RT_TLS_ERROR_HANDSHAKE;
        }

        if (content_type != TLS_CONTENT_HANDSHAKE)
        {
            session->error = "unexpected content type";
            return RT_TLS_ERROR_HANDSHAKE;
        }

        // Parse handshake messages
        size_t pos = 0;
        while (pos + 4 <= data_len)
        {
            uint8_t hs_type = data[pos];
            uint32_t hs_len = read_u24(data + pos + 1);

            if (pos + 4 + hs_len > data_len)
            {
                session->error = "incomplete handshake message";
                return RT_TLS_ERROR_HANDSHAKE;
            }

            // Update transcript before processing
            transcript_update(session, data + pos, 4 + hs_len);

            const uint8_t *hs_data = data + pos + 4;

            switch (hs_type)
            {
            case TLS_HS_SERVER_HELLO:
                rc = process_server_hello(session, hs_data, hs_len);
                if (rc != RT_TLS_OK)
                    return rc;
                break;

            case TLS_HS_ENCRYPTED_EXTENSIONS:
                // Skip - we don't process any extensions
                session->state = TLS_STATE_WAIT_CERTIFICATE;
                break;

            case TLS_HS_CERTIFICATE:
                // Skip certificate validation for now
                session->state = TLS_STATE_WAIT_CERTIFICATE_VERIFY;
                break;

            case TLS_HS_CERTIFICATE_VERIFY:
                // Skip signature verification for now
                session->state = TLS_STATE_WAIT_FINISHED;
                break;

            case TLS_HS_FINISHED:
                rc = verify_finished(session, hs_data, hs_len);
                if (rc != RT_TLS_OK)
                    return rc;

                // Derive application keys
                derive_application_keys(session);

                // Send our Finished
                rc = send_finished(session);
                if (rc != RT_TLS_OK)
                    return rc;

                session->state = TLS_STATE_CONNECTED;
                break;

            default:
                // Skip unknown messages
                break;
            }

            pos += 4 + hs_len;
        }
    }

    return session->state == TLS_STATE_CONNECTED ? RT_TLS_OK : RT_TLS_ERROR_HANDSHAKE;
}

long rt_tls_send(rt_tls_session_t *session, const void *data, size_t len)
{
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    if (len == 0)
        return 0;

    // Send in chunks
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0)
    {
        size_t chunk = remaining > TLS_MAX_RECORD_SIZE ? TLS_MAX_RECORD_SIZE : remaining;
        int rc = send_record(session, TLS_CONTENT_APPLICATION, ptr, chunk);
        if (rc != RT_TLS_OK)
            return rc;
        ptr += chunk;
        remaining -= chunk;
    }

    return (long)len;
}

long rt_tls_recv(rt_tls_session_t *session, void *buffer, size_t len)
{
    if (!session || session->state != TLS_STATE_CONNECTED)
        return RT_TLS_ERROR;

    // Return buffered data first
    if (session->app_buffer_pos < session->app_buffer_len)
    {
        size_t avail = session->app_buffer_len - session->app_buffer_pos;
        size_t copy = avail < len ? avail : len;
        memcpy(buffer, session->app_buffer + session->app_buffer_pos, copy);
        session->app_buffer_pos += copy;
        return (long)copy;
    }

    // Receive new record
    uint8_t content_type;
    size_t data_len;

    int rc = recv_record(session, &content_type, session->app_buffer, &data_len);
    if (rc != RT_TLS_OK)
        return rc;

    if (content_type == TLS_CONTENT_ALERT)
    {
        session->state = TLS_STATE_CLOSED;
        return 0;
    }

    if (content_type != TLS_CONTENT_APPLICATION)
    {
        // Handle other content types (e.g., post-handshake messages)
        return rt_tls_recv(session, buffer, len); // Retry
    }

    session->app_buffer_len = data_len;
    session->app_buffer_pos = 0;

    size_t copy = data_len < len ? data_len : len;
    memcpy(buffer, session->app_buffer, copy);
    session->app_buffer_pos = copy;

    return (long)copy;
}

void rt_tls_close(rt_tls_session_t *session)
{
    if (!session)
        return;

    if (session->state == TLS_STATE_CONNECTED)
    {
        // Send close_notify alert
        uint8_t alert[2] = {1, 0}; // warning, close_notify
        send_record(session, TLS_CONTENT_ALERT, alert, 2);
    }

    session->state = TLS_STATE_CLOSED;
    free(session);
}

const char *rt_tls_get_error(rt_tls_session_t *session)
{
    if (!session)
        return "null session";
    return session->error ? session->error : "no error";
}

int rt_tls_get_socket(rt_tls_session_t *session)
{
    if (!session)
        return -1;
    return (int)session->socket_fd;
}

rt_tls_session_t *rt_tls_connect(const char *host, uint16_t port, const rt_tls_config_t *config)
{
#ifdef _WIN32
    // Initialize Winsock
    extern void rt_net_init_wsa(void);
    rt_net_init_wsa();
#endif

    // Resolve hostname
    struct hostent *he = gethostbyname(host);
    if (!he)
        return NULL;

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return NULL;

    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], 4);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        CLOSE_SOCKET(sock);
        return NULL;
    }

    // Create TLS session
    rt_tls_config_t cfg;
    if (config)
    {
        cfg = *config;
    }
    else
    {
        rt_tls_config_init(&cfg);
    }
    cfg.hostname = host;

    rt_tls_session_t *session = rt_tls_new((int)sock, &cfg);
    if (!session)
    {
        CLOSE_SOCKET(sock);
        return NULL;
    }

    // Perform handshake
    if (rt_tls_handshake(session) != RT_TLS_OK)
    {
        rt_tls_close(session);
        CLOSE_SOCKET(sock);
        return NULL;
    }

    return session;
}

//=============================================================================
// Viper API Wrappers
//=============================================================================
//
// These functions wrap the low-level TLS API for use by the Viper runtime.
// They handle conversion between Viper types (rt_string, Bytes) and C types.
//
//=============================================================================

#include "rt_bytes.h"
#include "rt_object.h"
#include "rt_string.h"

/// @brief Internal structure for Viper TLS objects.
typedef struct rt_viper_tls
{
    rt_tls_session_t *session;
    char *host;
    int64_t port;
} rt_viper_tls_t;

/// @brief Finalizer for TLS objects.
static void rt_viper_tls_finalize(void *obj)
{
    if (!obj)
        return;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session)
    {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
    if (tls->host)
    {
        free(tls->host);
        tls->host = NULL;
    }
}

/// @brief Connect to a TLS server.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect(rt_string host, int64_t port)
{
    if (!host || port < 1 || port > 65535)
        return NULL;

    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr)
        return NULL;

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = host_cstr;

    rt_tls_session_t *session = rt_tls_connect(host_cstr, (uint16_t)port, &config);
    if (!session)
        return NULL;

    // Create Viper TLS object
    rt_viper_tls_t *tls = (rt_viper_tls_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viper_tls_t));
    if (!tls)
    {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = strdup(host_cstr);
    tls->port = port;

    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    return tls;
}

/// @brief Connect to a TLS server with timeout.
/// @param host Hostname to connect to.
/// @param port Port number.
/// @param timeout_ms Timeout in milliseconds.
/// @return TLS object or NULL on error.
void *rt_viper_tls_connect_for(rt_string host, int64_t port, int64_t timeout_ms)
{
    if (!host || port < 1 || port > 65535)
        return NULL;

    const char *host_cstr = rt_string_cstr(host);
    if (!host_cstr)
        return NULL;

    rt_tls_config_t config;
    rt_tls_config_init(&config);
    config.hostname = host_cstr;
    config.timeout_ms = (int)timeout_ms;

    rt_tls_session_t *session = rt_tls_connect(host_cstr, (uint16_t)port, &config);
    if (!session)
        return NULL;

    // Create Viper TLS object
    rt_viper_tls_t *tls = (rt_viper_tls_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viper_tls_t));
    if (!tls)
    {
        rt_tls_close(session);
        return NULL;
    }

    tls->session = session;
    tls->host = strdup(host_cstr);
    tls->port = port;

    rt_obj_set_finalizer(tls, rt_viper_tls_finalize);

    return tls;
}

/// @brief Get the hostname of the TLS connection.
rt_string rt_viper_tls_host(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    const char *h = tls->host ? tls->host : "";
    return rt_string_from_bytes(h, strlen(h));
}

/// @brief Get the port of the TLS connection.
int64_t rt_viper_tls_port(void *obj)
{
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->port;
}

/// @brief Check if the TLS connection is open.
int8_t rt_viper_tls_is_open(void *obj)
{
    if (!obj)
        return 0;
    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    return tls->session != NULL;
}

/// @brief Send bytes over TLS connection.
/// @param obj TLS object.
/// @param data Bytes object to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send(void *obj, void *data)
{
    if (!obj || !data)
        return -1;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return -1;

    int64_t len = rt_bytes_len(data);
    if (len == 0)
        return 0;

    // Copy bytes to temporary buffer
    uint8_t *buffer = (uint8_t *)malloc((size_t)len);
    if (!buffer)
        return -1;
    for (int64_t i = 0; i < len; i++)
        buffer[i] = (uint8_t)rt_bytes_get(data, i);

    long result = rt_tls_send(tls->session, buffer, (size_t)len);
    free(buffer);
    return (int64_t)result;
}

/// @brief Send string over TLS connection.
/// @param obj TLS object.
/// @param text String to send.
/// @return Number of bytes sent, or -1 on error.
int64_t rt_viper_tls_send_str(void *obj, rt_string text)
{
    if (!obj || !text)
        return -1;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return -1;

    const char *cstr = rt_string_cstr(text);
    if (!cstr)
        return 0;
    size_t len = strlen(cstr);
    if (len == 0)
        return 0;

    long result = rt_tls_send(tls->session, cstr, len);
    return (int64_t)result;
}

/// @brief Receive bytes from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return Bytes object with received data, or NULL on error.
void *rt_viper_tls_recv(void *obj, int64_t max_bytes)
{
    if (!obj || max_bytes <= 0)
        return NULL;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return NULL;

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    uint8_t *buffer = (uint8_t *)malloc(buf_size);
    if (!buffer)
        return NULL;

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0)
    {
        free(buffer);
        return received == 0 ? rt_bytes_new(0) : NULL;
    }

    // Create Bytes object and copy data
    void *result = rt_bytes_new((int64_t)received);
    for (long i = 0; i < received; i++)
        rt_bytes_set(result, i, buffer[i]);

    free(buffer);
    return result;
}

/// @brief Receive string from TLS connection.
/// @param obj TLS object.
/// @param max_bytes Maximum bytes to receive.
/// @return String with received data, or empty string on error.
rt_string rt_viper_tls_recv_str(void *obj, int64_t max_bytes)
{
    if (!obj || max_bytes <= 0)
        return rt_string_from_bytes("", 0);

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
        return rt_string_from_bytes("", 0);

    // Allocate temporary buffer
    size_t buf_size = (size_t)max_bytes;
    char *buffer = (char *)malloc(buf_size + 1);
    if (!buffer)
        return rt_string_from_bytes("", 0);

    long received = rt_tls_recv(tls->session, buffer, buf_size);
    if (received <= 0)
    {
        free(buffer);
        return rt_string_from_bytes("", 0);
    }

    buffer[received] = '\0';
    rt_string result = rt_string_from_bytes(buffer, (size_t)received);
    free(buffer);
    return result;
}

/// @brief Close the TLS connection.
void rt_viper_tls_close(void *obj)
{
    if (!obj)
        return;

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (tls->session)
    {
        rt_tls_close(tls->session);
        tls->session = NULL;
    }
}

/// @brief Get the last error message.
rt_string rt_viper_tls_error(void *obj)
{
    const char *msg;
    if (!obj)
    {
        msg = "null object";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    rt_viper_tls_t *tls = (rt_viper_tls_t *)obj;
    if (!tls->session)
    {
        msg = "connection closed";
        return rt_string_from_bytes(msg, strlen(msg));
    }

    const char *err = rt_tls_get_error(tls->session);
    msg = err ? err : "no error";
    return rt_string_from_bytes(msg, strlen(msg));
}
