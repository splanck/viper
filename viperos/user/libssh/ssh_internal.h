/**
 * @file ssh_internal.h
 * @brief Internal structures and constants for SSH implementation.
 */

#ifndef _SSH_INTERNAL_H
#define _SSH_INTERNAL_H

#include "include/ssh.h"
#include <stdbool.h>
#include <stdint.h>

/* SSH protocol constants */
#define SSH_VERSION_STRING "SSH-2.0-ViperOS_1.0"
#define SSH_MAX_PACKET_SIZE 35000
#define SSH_MAX_PAYLOAD_SIZE 32768
#define SSH_BLOCK_SIZE 16
#define SSH_MAX_CHANNELS 10

/* SSH message types */
enum ssh_msg_type
{
    /* Transport layer (1-49) */
    SSH_MSG_DISCONNECT = 1,
    SSH_MSG_IGNORE = 2,
    SSH_MSG_UNIMPLEMENTED = 3,
    SSH_MSG_DEBUG = 4,
    SSH_MSG_SERVICE_REQUEST = 5,
    SSH_MSG_SERVICE_ACCEPT = 6,

    /* Key exchange (20-29) */
    SSH_MSG_KEXINIT = 20,
    SSH_MSG_NEWKEYS = 21,

    /* Diffie-Hellman (30-49) */
    SSH_MSG_KEXDH_INIT = 30,
    SSH_MSG_KEXDH_REPLY = 31,
    SSH_MSG_KEX_ECDH_INIT = 30,
    SSH_MSG_KEX_ECDH_REPLY = 31,

    /* User authentication (50-79) */
    SSH_MSG_USERAUTH_REQUEST = 50,
    SSH_MSG_USERAUTH_FAILURE = 51,
    SSH_MSG_USERAUTH_SUCCESS = 52,
    SSH_MSG_USERAUTH_BANNER = 53,
    SSH_MSG_USERAUTH_PK_OK = 60,

    /* Connection protocol (80-127) */
    SSH_MSG_GLOBAL_REQUEST = 80,
    SSH_MSG_REQUEST_SUCCESS = 81,
    SSH_MSG_REQUEST_FAILURE = 82,
    SSH_MSG_CHANNEL_OPEN = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST = 93,
    SSH_MSG_CHANNEL_DATA = 94,
    SSH_MSG_CHANNEL_EXTENDED_DATA = 95,
    SSH_MSG_CHANNEL_EOF = 96,
    SSH_MSG_CHANNEL_CLOSE = 97,
    SSH_MSG_CHANNEL_REQUEST = 98,
    SSH_MSG_CHANNEL_SUCCESS = 99,
    SSH_MSG_CHANNEL_FAILURE = 100,
};

/* Disconnect reason codes */
enum ssh_disconnect_reason
{
    SSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT = 1,
    SSH_DISCONNECT_PROTOCOL_ERROR = 2,
    SSH_DISCONNECT_KEY_EXCHANGE_FAILED = 3,
    SSH_DISCONNECT_RESERVED = 4,
    SSH_DISCONNECT_MAC_ERROR = 5,
    SSH_DISCONNECT_COMPRESSION_ERROR = 6,
    SSH_DISCONNECT_SERVICE_NOT_AVAILABLE = 7,
    SSH_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED = 8,
    SSH_DISCONNECT_HOST_KEY_NOT_VERIFIABLE = 9,
    SSH_DISCONNECT_CONNECTION_LOST = 10,
    SSH_DISCONNECT_BY_APPLICATION = 11,
    SSH_DISCONNECT_TOO_MANY_CONNECTIONS = 12,
    SSH_DISCONNECT_AUTH_CANCELLED_BY_USER = 13,
    SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE = 14,
    SSH_DISCONNECT_ILLEGAL_USER_NAME = 15,
};

/* Channel open failure reasons */
enum ssh_channel_open_failure
{
    SSH_OPEN_ADMINISTRATIVELY_PROHIBITED = 1,
    SSH_OPEN_CONNECT_FAILED = 2,
    SSH_OPEN_UNKNOWN_CHANNEL_TYPE = 3,
    SSH_OPEN_RESOURCE_SHORTAGE = 4,
};

/* Session states */
typedef enum
{
    SSH_STATE_NONE = 0,
    SSH_STATE_CONNECTING,
    SSH_STATE_VERSION_EXCHANGE,
    SSH_STATE_KEX_INIT,
    SSH_STATE_KEX,
    SSH_STATE_NEWKEYS,
    SSH_STATE_SERVICE_REQUEST,
    SSH_STATE_AUTHENTICATED,
    SSH_STATE_DISCONNECTING,
    SSH_STATE_DISCONNECTED,
    SSH_STATE_ERROR,
} ssh_state_t;

/* Cipher algorithm */
typedef enum
{
    SSH_CIPHER_NONE = 0,
    SSH_CIPHER_AES128_CTR,
    SSH_CIPHER_AES256_CTR,
    SSH_CIPHER_CHACHA20_POLY1305,
} ssh_cipher_t;

/* MAC algorithm */
typedef enum
{
    SSH_MAC_NONE = 0,
    SSH_MAC_HMAC_SHA1,
    SSH_MAC_HMAC_SHA256,
    SSH_MAC_IMPLICIT, /* For AEAD ciphers */
} ssh_mac_t;

/* Key exchange algorithm */
typedef enum
{
    SSH_KEX_NONE = 0,
    SSH_KEX_CURVE25519_SHA256,
    SSH_KEX_DH_GROUP14_SHA256,
} ssh_kex_t;

/* Cipher context */
typedef struct
{
    ssh_cipher_t algo;
    uint8_t key[32]; /* Cipher key */
    uint8_t iv[16];  /* IV/counter */
    uint32_t block_size;
    uint32_t key_len;
    /* AES state for CTR mode */
    uint8_t aes_state[480]; /* Expanded AES key state */
    uint8_t keystream[16];  /* Current keystream block */
    uint32_t keystream_pos; /* Position within current keystream block */
} ssh_cipher_ctx_t;

/* MAC context */
typedef struct
{
    ssh_mac_t algo;
    uint8_t key[32];
    uint32_t key_len;
    uint32_t mac_len;
} ssh_mac_ctx_t;

/* Session key material */
typedef struct
{
    uint8_t iv_c2s[64];     /* Initial IV client to server */
    uint8_t iv_s2c[64];     /* Initial IV server to client */
    uint8_t key_c2s[64];    /* Encryption key client to server */
    uint8_t key_s2c[64];    /* Encryption key server to client */
    uint8_t mac_c2s[64];    /* MAC key client to server */
    uint8_t mac_s2c[64];    /* MAC key server to client */
    uint8_t session_id[64]; /* Session identifier */
    uint32_t session_id_len;
} ssh_keys_t;

/* SSH channel structure */
struct ssh_channel
{
    ssh_session_t *session;
    uint32_t local_channel;
    uint32_t remote_channel;
    uint32_t local_window;
    uint32_t remote_window;
    uint32_t local_maxpacket;
    uint32_t remote_maxpacket;
    ssh_channel_state_t state;
    int exit_status;
    bool exit_status_set;
    bool eof_sent;
    bool eof_received;
    /* Read buffer for incoming data */
    uint8_t *read_buf;
    size_t read_buf_size;
    size_t read_buf_len;
    size_t read_buf_pos;
    /* Extended data (stderr) buffer */
    uint8_t *ext_buf;
    size_t ext_buf_size;
    size_t ext_buf_len;
    size_t ext_buf_pos;
};

/* SSH key structure */
struct ssh_key
{
    ssh_keytype_t type;

    union
    {
        struct
        {
            uint8_t public_key[32];
            uint8_t secret_key[64];
        } ed25519;

        struct
        {
            uint8_t modulus[512];
            size_t modulus_len;
            uint8_t public_exp[8];
            size_t public_exp_len;
            uint8_t private_exp[512];
            size_t private_exp_len;
        } rsa;
    } key;

    bool has_private;
};

/* SSH session structure */
struct ssh_session
{
    int socket_fd;
    ssh_state_t state;
    int verbose;

    /* Connection info */
    char *hostname;
    uint16_t port;
    char *username;

    /* Server info */
    char server_version[256];
    uint8_t server_hostkey[1024];
    size_t server_hostkey_len;
    ssh_keytype_t server_hostkey_type;

    /* Key exchange state */
    ssh_kex_t kex_algo;
    ssh_keytype_t hostkey_algo;
    ssh_cipher_t cipher_c2s;
    ssh_cipher_t cipher_s2c;
    ssh_mac_t mac_c2s;
    ssh_mac_t mac_s2c;

    /* Kex init payloads (for hash) */
    uint8_t *kex_init_local;
    size_t kex_init_local_len;
    uint8_t *kex_init_remote;
    size_t kex_init_remote_len;

    /* Key exchange ephemeral data */
    uint8_t kex_secret[32]; /* Our secret key */
    uint8_t kex_public[32]; /* Our public key */
    uint8_t kex_shared[32]; /* Shared secret */

    /* Session keys */
    ssh_keys_t keys;

    /* Encryption state */
    ssh_cipher_ctx_t cipher_out;
    ssh_cipher_ctx_t cipher_in;
    ssh_mac_ctx_t mac_out;
    ssh_mac_ctx_t mac_in;
    uint32_t seq_out;
    uint32_t seq_in;
    bool encrypted;

    /* Channels */
    ssh_channel_t *channels[SSH_MAX_CHANNELS];
    uint32_t next_channel_id;

    /* I/O buffers */
    uint8_t in_buf[SSH_MAX_PACKET_SIZE];
    size_t in_buf_len;
    uint8_t out_buf[SSH_MAX_PACKET_SIZE];
    size_t out_buf_len;

    /* Host key callback */
    ssh_hostkey_callback_t hostkey_cb;
    void *hostkey_cb_data;

    /* Error handling */
    char error_msg[256];
};

/* Internal functions */

/* Packet handling */
int ssh_packet_send(ssh_session_t *session,
                    uint8_t msg_type,
                    const uint8_t *payload,
                    size_t payload_len);
int ssh_packet_recv(ssh_session_t *session,
                    uint8_t *msg_type,
                    uint8_t *payload,
                    size_t *payload_len);
int ssh_packet_wait(ssh_session_t *session,
                    uint8_t expected_type,
                    uint8_t *payload,
                    size_t *payload_len);

/* Key exchange */
int ssh_kex_start(ssh_session_t *session);
int ssh_kex_process(ssh_session_t *session);
int ssh_kex_derive_keys(
    ssh_session_t *session, const uint8_t *K, size_t K_len, const uint8_t *H, size_t H_len);

/* Authentication */
int ssh_auth_none(ssh_session_t *session);

/* Crypto primitives */
void ssh_random_bytes(uint8_t *buf, size_t len);
void ssh_sha256(const void *data, size_t len, uint8_t digest[32]);
void ssh_sha1(const void *data, size_t len, uint8_t digest[20]);
void ssh_hmac_sha256(
    const uint8_t *key, size_t key_len, const void *data, size_t data_len, uint8_t mac[32]);
void ssh_hmac_sha1(
    const uint8_t *key, size_t key_len, const void *data, size_t data_len, uint8_t mac[20]);
void ssh_aes_ctr_init(ssh_cipher_ctx_t *ctx, const uint8_t *key, size_t key_len, const uint8_t *iv);
void ssh_aes_ctr_process(ssh_cipher_ctx_t *ctx, const uint8_t *in, uint8_t *out, size_t len);

/* Ed25519 operations */
void ssh_ed25519_sign(const uint8_t secret[64], const void *msg, size_t msg_len, uint8_t sig[64]);
bool ssh_ed25519_verify(const uint8_t pub[32],
                        const void *msg,
                        size_t msg_len,
                        const uint8_t sig[64]);

/* X25519 key exchange */
void ssh_x25519_keygen(uint8_t secret[32], uint8_t public_key[32]);
void ssh_x25519(const uint8_t secret[32], const uint8_t peer_public[32], uint8_t shared[32]);

/* RSA operations */
bool ssh_rsa_sign(
    const struct ssh_key *key, const void *data, size_t data_len, uint8_t *sig, size_t *sig_len);
bool ssh_rsa_verify(const uint8_t *modulus,
                    size_t mod_len,
                    const uint8_t *exponent,
                    size_t exp_len,
                    const void *data,
                    size_t data_len,
                    const uint8_t *sig,
                    size_t sig_len);

/* Buffer utilities */
void ssh_buf_write_u32(uint8_t *buf, uint32_t val);
void ssh_buf_write_u8(uint8_t *buf, uint8_t val);
void ssh_buf_write_string(uint8_t *buf, const void *data, size_t len);
uint32_t ssh_buf_read_u32(const uint8_t *buf);
uint8_t ssh_buf_read_u8(const uint8_t *buf);
size_t ssh_buf_read_string(const uint8_t *buf, size_t buf_len, uint8_t *out, size_t out_max);

#endif /* _SSH_INTERNAL_H */
