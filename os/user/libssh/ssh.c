/**
 * @file ssh.c
 * @brief SSH-2 client implementation for ViperOS.
 *
 * Implements SSH-2 transport layer (RFC 4253), authentication (RFC 4252),
 * and connection protocol (RFC 4254).
 */

#include "include/ssh.h"
#include "ssh_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

/*=============================================================================
 * Buffer Utilities
 *===========================================================================*/

void ssh_buf_write_u32(uint8_t *buf, uint32_t val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

void ssh_buf_write_u8(uint8_t *buf, uint8_t val)
{
    buf[0] = val;
}

void ssh_buf_write_string(uint8_t *buf, const void *data, size_t len)
{
    ssh_buf_write_u32(buf, len);
    if (data && len > 0) {
        memcpy(buf + 4, data, len);
    }
}

uint32_t ssh_buf_read_u32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

uint8_t ssh_buf_read_u8(const uint8_t *buf)
{
    return buf[0];
}

size_t ssh_buf_read_string(const uint8_t *buf, size_t buf_len,
                           uint8_t *out, size_t out_max)
{
    if (buf_len < 4) return 0;
    uint32_t len = ssh_buf_read_u32(buf);
    if (len > buf_len - 4 || len > out_max) return 0;
    if (out && len > 0) {
        memcpy(out, buf + 4, len);
    }
    return len;
}

/* Write mpint (big integer) to buffer */
static size_t ssh_buf_write_mpint(uint8_t *buf, const uint8_t *data, size_t len)
{
    /* Skip leading zeros */
    while (len > 0 && *data == 0) {
        data++;
        len--;
    }

    /* Check if high bit is set (need leading zero) */
    int need_zero = (len > 0 && (data[0] & 0x80));
    uint32_t total_len = len + (need_zero ? 1 : 0);

    ssh_buf_write_u32(buf, total_len);
    if (need_zero) {
        buf[4] = 0;
        memcpy(buf + 5, data, len);
        return 4 + total_len;
    } else if (len > 0) {
        memcpy(buf + 4, data, len);
        return 4 + total_len;
    } else {
        return 4;  /* Zero value */
    }
}

/*=============================================================================
 * Session Management
 *===========================================================================*/

ssh_session_t *ssh_new(void)
{
    ssh_session_t *session = calloc(1, sizeof(ssh_session_t));
    if (!session) return NULL;

    session->socket_fd = -1;
    session->port = 22;
    session->state = SSH_STATE_NONE;
    session->next_channel_id = 0;

    return session;
}

void ssh_free(ssh_session_t *session)
{
    if (!session) return;

    if (session->socket_fd >= 0) {
        close(session->socket_fd);
    }

    free(session->hostname);
    free(session->username);
    free(session->kex_init_local);
    free(session->kex_init_remote);

    /* Free channels */
    for (int i = 0; i < SSH_MAX_CHANNELS; i++) {
        if (session->channels[i]) {
            ssh_channel_free(session->channels[i]);
        }
    }

    /* Clear sensitive data */
    memset(session, 0, sizeof(*session));
    free(session);
}

int ssh_set_host(ssh_session_t *session, const char *hostname)
{
    if (!session || !hostname) return SSH_ERROR;
    free(session->hostname);
    session->hostname = strdup(hostname);
    return session->hostname ? SSH_OK : SSH_ERROR;
}

int ssh_set_port(ssh_session_t *session, uint16_t port)
{
    if (!session) return SSH_ERROR;
    session->port = port;
    return SSH_OK;
}

int ssh_set_user(ssh_session_t *session, const char *username)
{
    if (!session || !username) return SSH_ERROR;
    free(session->username);
    session->username = strdup(username);
    return session->username ? SSH_OK : SSH_ERROR;
}

int ssh_set_hostkey_callback(ssh_session_t *session,
                             ssh_hostkey_callback_t callback,
                             void *userdata)
{
    if (!session) return SSH_ERROR;
    session->hostkey_cb = callback;
    session->hostkey_cb_data = userdata;
    return SSH_OK;
}

const char *ssh_get_error(ssh_session_t *session)
{
    if (!session) return "Invalid session";
    if (session->error_msg[0] == '\0') return "No error";
    return session->error_msg;
}

static void ssh_set_error(ssh_session_t *session, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(session->error_msg, sizeof(session->error_msg), fmt, args);
    va_end(args);
}

/*=============================================================================
 * Low-level I/O
 *===========================================================================*/

static ssize_t ssh_socket_send(ssh_session_t *session, const void *data, size_t len)
{
    const uint8_t *ptr = data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(session->socket_fd, ptr, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            ssh_set_error(session, "send failed: %d", errno);
            return SSH_ERROR;
        }
        if (sent == 0) {
            ssh_set_error(session, "connection closed");
            return SSH_EOF;
        }
        ptr += sent;
        remaining -= sent;
    }

    return len;
}

static ssize_t ssh_socket_recv(ssh_session_t *session, void *data, size_t len)
{
    uint8_t *ptr = data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t recvd = recv(session->socket_fd, ptr, remaining, 0);
        if (recvd < 0) {
            if (errno == EINTR) continue;
            ssh_set_error(session, "recv failed: %d", errno);
            return SSH_ERROR;
        }
        if (recvd == 0) {
            if (remaining == len) {
                return SSH_EOF;  /* Connection closed cleanly */
            }
            ssh_set_error(session, "connection closed unexpectedly");
            return SSH_ERROR;
        }
        ptr += recvd;
        remaining -= recvd;
    }

    return len;
}

/*=============================================================================
 * Packet Handling
 *===========================================================================*/

int ssh_packet_send(ssh_session_t *session, uint8_t msg_type,
                    const uint8_t *payload, size_t payload_len)
{
    uint8_t packet[SSH_MAX_PACKET_SIZE];
    size_t packet_len = 0;

    /* Calculate padding */
    uint32_t block_size = session->encrypted ? 16 : 8;
    uint32_t payload_total = 1 + payload_len;  /* msg_type + payload */
    uint32_t padding_len = block_size - ((4 + 1 + payload_total) % block_size);
    if (padding_len < 4) padding_len += block_size;

    uint32_t packet_length = 1 + payload_total + padding_len;

    /* Build packet: packet_length (4) + padding_length (1) + msg_type (1) + payload + padding */
    ssh_buf_write_u32(packet, packet_length);
    packet[4] = padding_len;
    packet[5] = msg_type;
    if (payload && payload_len > 0) {
        memcpy(packet + 6, payload, payload_len);
    }

    /* Add random padding */
    ssh_random_bytes(packet + 6 + payload_len, padding_len);

    packet_len = 4 + packet_length;

    /* Encrypt if needed */
    if (session->encrypted) {
        /* Debug: show what we're sending */
        printf("[ssh] TX encrypted: seq=%u len=%u msg=%u\n",
               session->seq_out, (unsigned)packet_len, msg_type);
        printf("[ssh] TX plain[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               packet[0], packet[1], packet[2], packet[3],
               packet[4], packet[5], packet[6], packet[7]);

        /* Calculate MAC BEFORE encryption (SSH uses encrypt-and-MAC) */
        /* MAC is computed over: sequence_number || unencrypted_packet */
        uint8_t mac_data[4 + SSH_MAX_PACKET_SIZE];
        ssh_buf_write_u32(mac_data, session->seq_out);
        memcpy(mac_data + 4, packet, packet_len);

        if (session->mac_out.algo == SSH_MAC_HMAC_SHA256) {
            ssh_hmac_sha256(session->mac_out.key, session->mac_out.key_len,
                           mac_data, 4 + packet_len, packet + packet_len);
            printf("[ssh] TX MAC[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   packet[packet_len], packet[packet_len+1], packet[packet_len+2], packet[packet_len+3],
                   packet[packet_len+4], packet[packet_len+5], packet[packet_len+6], packet[packet_len+7]);
            packet_len += 32;
        } else if (session->mac_out.algo == SSH_MAC_HMAC_SHA1) {
            ssh_hmac_sha1(session->mac_out.key, session->mac_out.key_len,
                         mac_data, 4 + packet_len, packet + packet_len);
            packet_len += 20;
        }

        /* Encrypt full packet AFTER computing MAC (including packet length) */
        ssh_aes_ctr_process(&session->cipher_out, packet, packet, 4 + packet_length);

        printf("[ssh] TX cipher[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               packet[0], packet[1], packet[2], packet[3],
               packet[4], packet[5], packet[6], packet[7]);
    }

    session->seq_out++;

    return ssh_socket_send(session, packet, packet_len);
}

int ssh_packet_recv(ssh_session_t *session, uint8_t *msg_type,
                    uint8_t *payload, size_t *payload_len)
{
    uint8_t header[4];
    uint8_t packet[SSH_MAX_PACKET_SIZE];

    /* Read packet length */
    ssize_t rc = ssh_socket_recv(session, header, 4);
    if (rc < 0) return rc;

    memcpy(packet, header, 4);

    /* Decrypt header if needed */
    if (session->encrypted) {
        ssh_aes_ctr_process(&session->cipher_in, packet, packet, 4);
    }

    uint32_t packet_length = ssh_buf_read_u32(packet);
    if (packet_length > SSH_MAX_PACKET_SIZE - 4) {
        ssh_set_error(session, "packet too large: %u", packet_length);
        return SSH_PROTOCOL_ERROR;
    }

    /* Read rest of packet */
    rc = ssh_socket_recv(session, packet + 4, packet_length);
    if (rc < 0) return rc;

    /* Decrypt packet body and verify MAC */
    if (session->encrypted) {
        /* Decrypt the body */
        ssh_aes_ctr_process(&session->cipher_in, packet + 4, packet + 4, packet_length);

        /* Read MAC from wire */
        uint8_t mac_received[32];
        uint32_t mac_len = (session->mac_in.algo == SSH_MAC_HMAC_SHA256) ? 32 : 20;
        rc = ssh_socket_recv(session, mac_received, mac_len);
        if (rc < 0) return rc;

        /* Calculate expected MAC on the decrypted (unencrypted) packet */
        /* MAC = HMAC(key, sequence_number || unencrypted_packet) */
        uint8_t mac_data[4 + SSH_MAX_PACKET_SIZE];
        uint8_t mac_expected[32];
        ssh_buf_write_u32(mac_data, session->seq_in);
        memcpy(mac_data + 4, packet, 4 + packet_length);  /* Full decrypted packet */

        if (session->mac_in.algo == SSH_MAC_HMAC_SHA256) {
            ssh_hmac_sha256(session->mac_in.key, session->mac_in.key_len,
                           mac_data, 4 + 4 + packet_length, mac_expected);
        } else {
            ssh_hmac_sha1(session->mac_in.key, session->mac_in.key_len,
                         mac_data, 4 + 4 + packet_length, mac_expected);
        }

        /* Verify MAC */
        if (memcmp(mac_received, mac_expected, mac_len) != 0) {
            ssh_set_error(session, "MAC verification failed");
            return SSH_PROTOCOL_ERROR;
        }
    }

    session->seq_in++;

    uint8_t padding_len = packet[4];
    *msg_type = packet[5];
    *payload_len = packet_length - 1 - padding_len - 1;

    if (*payload_len > 0 && payload) {
        memcpy(payload, packet + 6, *payload_len);
    }

    return SSH_OK;
}

int ssh_packet_wait(ssh_session_t *session, uint8_t expected_type,
                    uint8_t *payload, size_t *payload_len)
{
    uint8_t msg_type;
    int rc;

    while (1) {
        rc = ssh_packet_recv(session, &msg_type, payload, payload_len);
        if (rc < 0) return rc;

        if (msg_type == expected_type) {
            return SSH_OK;
        }

        /* Handle unexpected messages */
        if (msg_type == SSH_MSG_DISCONNECT) {
            uint32_t reason = 0;
            if (*payload_len >= 4) {
                reason = ssh_buf_read_u32(payload);
            }
            ssh_set_error(session, "disconnected by server: %u", reason);
            session->state = SSH_STATE_DISCONNECTED;
            return SSH_EOF;
        }

        if (msg_type == SSH_MSG_IGNORE || msg_type == SSH_MSG_DEBUG) {
            continue;  /* Ignore these */
        }

        /* Send unimplemented for unknown messages */
        uint8_t unimpl[4];
        ssh_buf_write_u32(unimpl, session->seq_in - 1);
        ssh_packet_send(session, SSH_MSG_UNIMPLEMENTED, unimpl, 4);
    }
}

/*=============================================================================
 * Version Exchange
 *===========================================================================*/

static int ssh_version_exchange(ssh_session_t *session)
{
    /* Send our version */
    char version_line[256];
    int len = snprintf(version_line, sizeof(version_line), "%s\r\n", SSH_VERSION_STRING);

    ssize_t rc = ssh_socket_send(session, version_line, len);
    if (rc < 0) return rc;

    /* Read server version */
    char server_version[256];
    int pos = 0;

    while (pos < (int)sizeof(server_version) - 1) {
        char c;
        rc = ssh_socket_recv(session, &c, 1);
        if (rc < 0) return rc;

        if (c == '\n') {
            server_version[pos] = '\0';
            /* Remove trailing \r if present */
            if (pos > 0 && server_version[pos-1] == '\r') {
                server_version[pos-1] = '\0';
            }
            break;
        }
        server_version[pos++] = c;
    }

    /* Verify it starts with SSH-2.0 */
    if (strncmp(server_version, "SSH-2.0-", 8) != 0) {
        ssh_set_error(session, "unsupported protocol: %s", server_version);
        return SSH_PROTOCOL_ERROR;
    }

    strncpy(session->server_version, server_version, sizeof(session->server_version) - 1);

    return SSH_OK;
}

/*=============================================================================
 * Key Exchange
 *===========================================================================*/

/* Algorithm name lists for KEX_INIT */
static const char *KEX_ALGORITHMS = "curve25519-sha256,curve25519-sha256@libssh.org";
static const char *HOSTKEY_ALGORITHMS = "ssh-ed25519,rsa-sha2-256,ssh-rsa";
static const char *CIPHER_ALGORITHMS = "aes256-ctr,aes128-ctr";
static const char *MAC_ALGORITHMS = "hmac-sha2-256,hmac-sha1";
static const char *COMPRESSION = "none";

int ssh_kex_start(ssh_session_t *session)
{
    uint8_t payload[2048];
    size_t pos = 0;

    /* Cookie (16 random bytes) */
    ssh_random_bytes(payload, 16);
    pos = 16;

    /* Algorithm lists */
    size_t len;

    /* kex_algorithms */
    len = strlen(KEX_ALGORITHMS);
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, KEX_ALGORITHMS, len);
    pos += 4 + len;

    /* server_host_key_algorithms */
    len = strlen(HOSTKEY_ALGORITHMS);
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, HOSTKEY_ALGORITHMS, len);
    pos += 4 + len;

    /* encryption_algorithms_client_to_server */
    len = strlen(CIPHER_ALGORITHMS);
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, CIPHER_ALGORITHMS, len);
    pos += 4 + len;

    /* encryption_algorithms_server_to_client */
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, CIPHER_ALGORITHMS, len);
    pos += 4 + len;

    /* mac_algorithms_client_to_server */
    len = strlen(MAC_ALGORITHMS);
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, MAC_ALGORITHMS, len);
    pos += 4 + len;

    /* mac_algorithms_server_to_client */
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, MAC_ALGORITHMS, len);
    pos += 4 + len;

    /* compression_algorithms_client_to_server */
    len = strlen(COMPRESSION);
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, COMPRESSION, len);
    pos += 4 + len;

    /* compression_algorithms_server_to_client */
    ssh_buf_write_u32(payload + pos, len);
    memcpy(payload + pos + 4, COMPRESSION, len);
    pos += 4 + len;

    /* languages (empty) */
    ssh_buf_write_u32(payload + pos, 0);
    pos += 4;
    ssh_buf_write_u32(payload + pos, 0);
    pos += 4;

    /* first_kex_packet_follows = false */
    payload[pos++] = 0;

    /* reserved */
    ssh_buf_write_u32(payload + pos, 0);
    pos += 4;

    /* Save for exchange hash */
    session->kex_init_local = malloc(pos);
    if (session->kex_init_local) {
        memcpy(session->kex_init_local, payload, pos);
        session->kex_init_local_len = pos;
    }

    return ssh_packet_send(session, SSH_MSG_KEXINIT, payload, pos);
}

/* Parse KEXINIT and select algorithms */
static int ssh_kex_parse_init(ssh_session_t *session, const uint8_t *payload, size_t len)
{
    /* Save server's KEXINIT for exchange hash */
    session->kex_init_remote = malloc(len);
    if (session->kex_init_remote) {
        memcpy(session->kex_init_remote, payload, len);
        session->kex_init_remote_len = len;
    }

    size_t pos = 16;  /* Skip cookie */

    /* Parse kex_algorithms */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    uint32_t kex_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + kex_len > len) return SSH_PROTOCOL_ERROR;

    /* Check for curve25519-sha256 */
    if (memmem(payload + pos, kex_len, "curve25519-sha256", 17)) {
        session->kex_algo = SSH_KEX_CURVE25519_SHA256;
    } else {
        ssh_set_error(session, "no common kex algorithm");
        return SSH_PROTOCOL_ERROR;
    }
    pos += kex_len;

    /* Parse server_host_key_algorithms */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    uint32_t hostkey_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + hostkey_len > len) return SSH_PROTOCOL_ERROR;

    if (memmem(payload + pos, hostkey_len, "ssh-ed25519", 11)) {
        session->hostkey_algo = SSH_KEYTYPE_ED25519;
    } else if (memmem(payload + pos, hostkey_len, "ssh-rsa", 7)) {
        session->hostkey_algo = SSH_KEYTYPE_RSA;
    } else {
        ssh_set_error(session, "no common hostkey algorithm");
        return SSH_PROTOCOL_ERROR;
    }
    pos += hostkey_len;

    /* Parse encryption_algorithms_client_to_server */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    uint32_t cipher_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + cipher_len > len) return SSH_PROTOCOL_ERROR;

    if (memmem(payload + pos, cipher_len, "aes256-ctr", 10)) {
        session->cipher_c2s = SSH_CIPHER_AES256_CTR;
    } else if (memmem(payload + pos, cipher_len, "aes128-ctr", 10)) {
        session->cipher_c2s = SSH_CIPHER_AES128_CTR;
    } else {
        ssh_set_error(session, "no common cipher");
        return SSH_PROTOCOL_ERROR;
    }
    pos += cipher_len;

    /* encryption_algorithms_server_to_client */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    cipher_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + cipher_len > len) return SSH_PROTOCOL_ERROR;

    if (memmem(payload + pos, cipher_len, "aes256-ctr", 10)) {
        session->cipher_s2c = SSH_CIPHER_AES256_CTR;
    } else if (memmem(payload + pos, cipher_len, "aes128-ctr", 10)) {
        session->cipher_s2c = SSH_CIPHER_AES128_CTR;
    }
    pos += cipher_len;

    /* mac_algorithms_client_to_server */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    uint32_t mac_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + mac_len > len) return SSH_PROTOCOL_ERROR;

    if (memmem(payload + pos, mac_len, "hmac-sha2-256", 13)) {
        session->mac_c2s = SSH_MAC_HMAC_SHA256;
    } else if (memmem(payload + pos, mac_len, "hmac-sha1", 9)) {
        session->mac_c2s = SSH_MAC_HMAC_SHA1;
    }
    pos += mac_len;

    /* mac_algorithms_server_to_client */
    if (pos + 4 > len) return SSH_PROTOCOL_ERROR;
    mac_len = ssh_buf_read_u32(payload + pos);
    pos += 4;
    if (pos + mac_len > len) return SSH_PROTOCOL_ERROR;

    if (memmem(payload + pos, mac_len, "hmac-sha2-256", 13)) {
        session->mac_s2c = SSH_MAC_HMAC_SHA256;
    } else if (memmem(payload + pos, mac_len, "hmac-sha1", 9)) {
        session->mac_s2c = SSH_MAC_HMAC_SHA1;
    }

    return SSH_OK;
}

/* Perform curve25519-sha256 key exchange */
static int ssh_kex_curve25519(ssh_session_t *session)
{
    uint8_t payload[1024];
    size_t len;
    int rc;

    /* Generate our ephemeral key pair */
    ssh_x25519_keygen(session->kex_secret, session->kex_public);

    /* Send KEX_ECDH_INIT */
    ssh_buf_write_string(payload, session->kex_public, 32);
    rc = ssh_packet_send(session, SSH_MSG_KEX_ECDH_INIT, payload, 36);
    if (rc < 0) return rc;

    /* Wait for KEX_ECDH_REPLY */
    uint8_t reply[2048];
    size_t reply_len;
    rc = ssh_packet_wait(session, SSH_MSG_KEX_ECDH_REPLY, reply, &reply_len);
    if (rc < 0) return rc;

    size_t pos = 0;

    /* Parse K_S (server public host key) */
    if (pos + 4 > reply_len) return SSH_PROTOCOL_ERROR;
    uint32_t hostkey_len = ssh_buf_read_u32(reply + pos);
    pos += 4;
    if (pos + hostkey_len > reply_len || hostkey_len > sizeof(session->server_hostkey)) {
        return SSH_PROTOCOL_ERROR;
    }
    memcpy(session->server_hostkey, reply + pos, hostkey_len);
    session->server_hostkey_len = hostkey_len;
    pos += hostkey_len;

    /* Parse Q_S (server's ephemeral public key) */
    if (pos + 4 > reply_len) return SSH_PROTOCOL_ERROR;
    uint32_t qs_len = ssh_buf_read_u32(reply + pos);
    pos += 4;
    if (qs_len != 32 || pos + 32 > reply_len) return SSH_PROTOCOL_ERROR;
    uint8_t Q_S[32];
    memcpy(Q_S, reply + pos, 32);
    pos += 32;

    /* Compute shared secret */
    ssh_x25519(session->kex_secret, Q_S, session->kex_shared);

    /*
     * RFC 8731 §3.1: X25519 produces a 32-byte little-endian string X. SSH
     * then reinterprets those octets as an unsigned fixed-length integer in
     * network byte order for mpint (K) encoding. Our mpint writer consumes
     * big-endian octets, so we keep the bytes as-is and let mpint encoding do
     * the reinterpretation.
     */
    int all_zero = 1;
    for (size_t i = 0; i < 32; i++) {
        if (session->kex_shared[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        ssh_set_error(session, "key exchange failed (all-zero shared secret)");
        return SSH_PROTOCOL_ERROR;
    }

    /* Parse signature */
    if (pos + 4 > reply_len) return SSH_PROTOCOL_ERROR;
    uint32_t sig_len = ssh_buf_read_u32(reply + pos);
    pos += 4;
    if (pos + sig_len > reply_len) return SSH_PROTOCOL_ERROR;
    uint8_t *signature = reply + pos;

    /* Compute exchange hash H = SHA256(V_C || V_S || I_C || I_S || K_S || Q_C || Q_S || K) */
    uint8_t hash_input[4096];
    size_t hash_pos = 0;

    /* V_C (client version) */
    len = strlen(SSH_VERSION_STRING);
    ssh_buf_write_u32(hash_input + hash_pos, len);
    memcpy(hash_input + hash_pos + 4, SSH_VERSION_STRING, len);
    hash_pos += 4 + len;

    /* V_S (server version) */
    len = strlen(session->server_version);
    ssh_buf_write_u32(hash_input + hash_pos, len);
    memcpy(hash_input + hash_pos + 4, session->server_version, len);
    hash_pos += 4 + len;

    /* I_C (client KEXINIT) */
    ssh_buf_write_u32(hash_input + hash_pos, session->kex_init_local_len + 1);
    hash_input[hash_pos + 4] = SSH_MSG_KEXINIT;
    memcpy(hash_input + hash_pos + 5, session->kex_init_local, session->kex_init_local_len);
    hash_pos += 5 + session->kex_init_local_len;

    /* I_S (server KEXINIT) */
    ssh_buf_write_u32(hash_input + hash_pos, session->kex_init_remote_len + 1);
    hash_input[hash_pos + 4] = SSH_MSG_KEXINIT;
    memcpy(hash_input + hash_pos + 5, session->kex_init_remote, session->kex_init_remote_len);
    hash_pos += 5 + session->kex_init_remote_len;

    /* K_S (server public host key) */
    ssh_buf_write_u32(hash_input + hash_pos, session->server_hostkey_len);
    memcpy(hash_input + hash_pos + 4, session->server_hostkey, session->server_hostkey_len);
    hash_pos += 4 + session->server_hostkey_len;

    /* Q_C (our ephemeral public) */
    ssh_buf_write_u32(hash_input + hash_pos, 32);
    memcpy(hash_input + hash_pos + 4, session->kex_public, 32);
    hash_pos += 36;

    /* Q_S (server ephemeral public) */
    ssh_buf_write_u32(hash_input + hash_pos, 32);
    memcpy(hash_input + hash_pos + 4, Q_S, 32);
    hash_pos += 36;

    /* K (shared secret as mpint) */
    hash_pos += ssh_buf_write_mpint(hash_input + hash_pos, session->kex_shared, 32);

    /* Compute H */
    uint8_t H[32];
    ssh_sha256(hash_input, hash_pos, H);

    printf("[ssh] H[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           H[0], H[1], H[2], H[3], H[4], H[5], H[6], H[7]);
    printf("[ssh] K[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           session->kex_shared[0], session->kex_shared[1],
           session->kex_shared[2], session->kex_shared[3],
           session->kex_shared[4], session->kex_shared[5],
           session->kex_shared[6], session->kex_shared[7]);

    /* Verify host key signature */
    /* Parse signature blob */
    if (sig_len < 4) return SSH_PROTOCOL_ERROR;
    uint32_t sig_type_len = ssh_buf_read_u32(signature);
    if (4 + sig_type_len + 4 > sig_len) return SSH_PROTOCOL_ERROR;

    uint32_t sig_data_len = ssh_buf_read_u32(signature + 4 + sig_type_len);
    uint8_t *sig_data = signature + 4 + sig_type_len + 4;

    if (session->hostkey_algo == SSH_KEYTYPE_ED25519) {
        /* Parse Ed25519 public key from K_S */
        /* Format: string "ssh-ed25519" || string pubkey */
        if (hostkey_len < 4 + 11 + 4 + 32) return SSH_PROTOCOL_ERROR;
        uint8_t pubkey[32];
        memcpy(pubkey, session->server_hostkey + 4 + 11 + 4, 32);

        if (sig_data_len != 64) return SSH_PROTOCOL_ERROR;
        if (!ssh_ed25519_verify(pubkey, H, 32, sig_data)) {
            ssh_set_error(session, "host key signature verification failed");
            return SSH_HOST_KEY_MISMATCH;
        }
        session->server_hostkey_type = SSH_KEYTYPE_ED25519;
    }

    /* Call host key verification callback if set */
    if (session->hostkey_cb) {
        rc = session->hostkey_cb(session, session->hostname,
                                 session->server_hostkey, session->server_hostkey_len,
                                 session->server_hostkey_type, session->hostkey_cb_data);
        if (rc != 0) {
            ssh_set_error(session, "host key rejected by user");
            return SSH_HOST_KEY_MISMATCH;
        }
    }

    /* Derive session keys */
    rc = ssh_kex_derive_keys(session, session->kex_shared, 32, H, 32);
    if (rc < 0) return rc;

    /* Session ID is first H */
    if (session->keys.session_id_len == 0) {
        memcpy(session->keys.session_id, H, 32);
        session->keys.session_id_len = 32;
    }

    return SSH_OK;
}

int ssh_kex_derive_keys(ssh_session_t *session, const uint8_t *K, size_t K_len,
                        const uint8_t *H, size_t H_len)
{
    /* Key derivation: key = HASH(K || H || "X" || session_id) */
    /* where X is 'A', 'B', 'C', 'D', 'E', 'F' for different keys */

    uint8_t hash_input[1024];
    size_t base_len = 0;

    /* K as mpint */
    base_len = ssh_buf_write_mpint(hash_input, K, K_len);
    printf("[ssh] derive_keys: K_mpint_len=%lu\n", (unsigned long)base_len);

    /* H */
    memcpy(hash_input + base_len, H, H_len);
    base_len += H_len;

    /* Generate keys A through F */
    const char letters[] = "ABCDEF";
    uint8_t *key_ptrs[] = {
        session->keys.iv_c2s,
        session->keys.iv_s2c,
        session->keys.key_c2s,
        session->keys.key_s2c,
        session->keys.mac_c2s,
        session->keys.mac_s2c
    };

    uint8_t *session_id = session->keys.session_id;
    size_t session_id_len = session->keys.session_id_len;
    if (session_id_len == 0) {
        session_id = (uint8_t *)H;
        session_id_len = H_len;
    }

    for (int i = 0; i < 6; i++) {
        hash_input[base_len] = letters[i];
        memcpy(hash_input + base_len + 1, session_id, session_id_len);
        ssh_sha256(hash_input, base_len + 1 + session_id_len, key_ptrs[i]);

        /* For longer keys, hash again with previous hash appended */
        if (i >= 2) {  /* Keys need to be longer than IVs */
            memcpy(hash_input + base_len + 1 + session_id_len, key_ptrs[i], 32);
            ssh_sha256(hash_input, base_len + 1 + session_id_len + 32, key_ptrs[i] + 32);
        }
    }

    return SSH_OK;
}

int ssh_kex_process(ssh_session_t *session)
{
    uint8_t payload[2048];
    size_t payload_len;
    int rc;

    /* Wait for server's KEXINIT */
    rc = ssh_packet_wait(session, SSH_MSG_KEXINIT, payload, &payload_len);
    if (rc < 0) return rc;

    /* Parse and select algorithms */
    rc = ssh_kex_parse_init(session, payload, payload_len);
    if (rc < 0) return rc;

    /* Perform key exchange based on selected algorithm */
    if (session->kex_algo == SSH_KEX_CURVE25519_SHA256) {
        rc = ssh_kex_curve25519(session);
        if (rc < 0) return rc;
    } else {
        ssh_set_error(session, "unsupported kex algorithm");
        return SSH_PROTOCOL_ERROR;
    }

    /* Send NEWKEYS */
    rc = ssh_packet_send(session, SSH_MSG_NEWKEYS, NULL, 0);
    if (rc < 0) return rc;

    /* Wait for server's NEWKEYS */
    rc = ssh_packet_wait(session, SSH_MSG_NEWKEYS, payload, &payload_len);
    if (rc < 0) return rc;

    /* Activate encryption */
    uint32_t key_len = (session->cipher_c2s == SSH_CIPHER_AES256_CTR) ? 32 : 16;
    printf("[ssh] Activating encryption: cipher=%s key_len=%u\n",
           key_len == 32 ? "aes256-ctr" : "aes128-ctr", key_len);
    printf("[ssh] IV_c2s[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           session->keys.iv_c2s[0], session->keys.iv_c2s[1],
           session->keys.iv_c2s[2], session->keys.iv_c2s[3],
           session->keys.iv_c2s[4], session->keys.iv_c2s[5],
           session->keys.iv_c2s[6], session->keys.iv_c2s[7]);
    printf("[ssh] Key_c2s[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           session->keys.key_c2s[0], session->keys.key_c2s[1],
           session->keys.key_c2s[2], session->keys.key_c2s[3],
           session->keys.key_c2s[4], session->keys.key_c2s[5],
           session->keys.key_c2s[6], session->keys.key_c2s[7]);
    printf("[ssh] MAC_c2s[0..7]: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           session->keys.mac_c2s[0], session->keys.mac_c2s[1],
           session->keys.mac_c2s[2], session->keys.mac_c2s[3],
           session->keys.mac_c2s[4], session->keys.mac_c2s[5],
           session->keys.mac_c2s[6], session->keys.mac_c2s[7]);

    ssh_aes_ctr_init(&session->cipher_out, session->keys.key_c2s, key_len, session->keys.iv_c2s);
    ssh_aes_ctr_init(&session->cipher_in, session->keys.key_s2c, key_len, session->keys.iv_s2c);

    /* Setup MAC */
    session->mac_out.algo = session->mac_c2s;
    session->mac_out.key_len = (session->mac_c2s == SSH_MAC_HMAC_SHA256) ? 32 : 20;
    session->mac_out.mac_len = session->mac_out.key_len;
    memcpy(session->mac_out.key, session->keys.mac_c2s, session->mac_out.key_len);

    session->mac_in.algo = session->mac_s2c;
    session->mac_in.key_len = (session->mac_s2c == SSH_MAC_HMAC_SHA256) ? 32 : 20;
    session->mac_in.mac_len = session->mac_in.key_len;
    memcpy(session->mac_in.key, session->keys.mac_s2c, session->mac_in.key_len);

    printf("[ssh] MAC algo: %s\n",
           session->mac_c2s == SSH_MAC_HMAC_SHA256 ? "hmac-sha2-256" : "hmac-sha1");

    session->encrypted = true;

    return SSH_OK;
}

/*=============================================================================
 * Connection
 *===========================================================================*/

int ssh_connect(ssh_session_t *session)
{
    int rc;

    if (!session || !session->hostname) {
        return SSH_ERROR;
    }

    session->state = SSH_STATE_CONNECTING;

    /* Create socket */
    session->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (session->socket_fd < 0) {
        ssh_set_error(session, "socket creation failed");
        return SSH_ERROR;
    }

    /* Resolve hostname and connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->port);

    /* Try IP address first, then DNS resolution */
    if (inet_pton(AF_INET, session->hostname, &addr.sin_addr) <= 0) {
        /* Not a numeric IP, try DNS resolution */
        struct hostent *he = gethostbyname(session->hostname);
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) {
            ssh_set_error(session, "hostname resolution failed for '%s'", session->hostname);
            close(session->socket_fd);
            session->socket_fd = -1;
            return SSH_ERROR;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
    }

    rc = connect(session->socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        ssh_set_error(session, "connect failed: %d", errno);
        close(session->socket_fd);
        session->socket_fd = -1;
        return SSH_ERROR;
    }

    /* Version exchange */
    session->state = SSH_STATE_VERSION_EXCHANGE;
    rc = ssh_version_exchange(session);
    if (rc < 0) return rc;

    /* Send our KEXINIT */
    session->state = SSH_STATE_KEX_INIT;
    rc = ssh_kex_start(session);
    if (rc < 0) return rc;

    /* Process key exchange */
    session->state = SSH_STATE_KEX;
    rc = ssh_kex_process(session);
    if (rc < 0) return rc;

    /* Request ssh-userauth service */
    session->state = SSH_STATE_SERVICE_REQUEST;
    uint8_t service_req[256];
    const char *service = "ssh-userauth";
    size_t service_len = strlen(service);
    ssh_buf_write_u32(service_req, service_len);
    memcpy(service_req + 4, service, service_len);

    rc = ssh_packet_send(session, SSH_MSG_SERVICE_REQUEST, service_req, 4 + service_len);
    if (rc < 0) return rc;

    /* Wait for SERVICE_ACCEPT */
    uint8_t payload[256];
    size_t payload_len;
    rc = ssh_packet_wait(session, SSH_MSG_SERVICE_ACCEPT, payload, &payload_len);
    if (rc < 0) return rc;

    return SSH_OK;
}

int ssh_disconnect(ssh_session_t *session)
{
    if (!session) return SSH_ERROR;

    if (session->socket_fd >= 0 && session->state != SSH_STATE_DISCONNECTED) {
        /* Send disconnect message */
        uint8_t payload[256];
        size_t pos = 0;

        ssh_buf_write_u32(payload + pos, SSH_DISCONNECT_BY_APPLICATION);
        pos += 4;

        const char *desc = "disconnected by user";
        ssh_buf_write_u32(payload + pos, strlen(desc));
        memcpy(payload + pos + 4, desc, strlen(desc));
        pos += 4 + strlen(desc);

        ssh_buf_write_u32(payload + pos, 0);  /* language tag */
        pos += 4;

        ssh_packet_send(session, SSH_MSG_DISCONNECT, payload, pos);

        close(session->socket_fd);
        session->socket_fd = -1;
    }

    session->state = SSH_STATE_DISCONNECTED;
    return SSH_OK;
}

int ssh_get_server_hostkey(ssh_session_t *session,
                           uint8_t *key,
                           size_t *key_len,
                           ssh_keytype_t *keytype)
{
    if (!session || session->server_hostkey_len == 0) return SSH_ERROR;

    if (key && *key_len >= session->server_hostkey_len) {
        memcpy(key, session->server_hostkey, session->server_hostkey_len);
    }
    *key_len = session->server_hostkey_len;
    if (keytype) *keytype = session->server_hostkey_type;

    return SSH_OK;
}
