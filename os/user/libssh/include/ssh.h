/**
 * @file ssh.h
 * @brief SSH-2 client library for ViperOS.
 *
 * Implements SSH-2 protocol (RFC 4250-4254) for secure remote access.
 * Supports:
 * - Key exchange: curve25519-sha256, diffie-hellman-group14-sha256
 * - Host key: ssh-ed25519, ssh-rsa
 * - Encryption: aes128-ctr, aes256-ctr, chacha20-poly1305@openssh.com
 * - MAC: hmac-sha2-256, hmac-sha1
 * - Authentication: publickey, password
 * - Channels: session, exec, shell, subsystem (sftp)
 */

#ifndef _SSH_H
#define _SSH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SSH error codes */
typedef enum {
    SSH_OK = 0,
    SSH_ERROR = -1,
    SSH_AGAIN = -2,           /* Would block, try again */
    SSH_EOF = -3,             /* Connection closed */
    SSH_TIMEOUT = -4,
    SSH_HOST_KEY_MISMATCH = -5,
    SSH_AUTH_DENIED = -6,
    SSH_CHANNEL_CLOSED = -7,
    SSH_PROTOCOL_ERROR = -8,
} ssh_error_t;

/* SSH authentication methods */
typedef enum {
    SSH_AUTH_NONE = 0,
    SSH_AUTH_PASSWORD = 1,
    SSH_AUTH_PUBLICKEY = 2,
    SSH_AUTH_KEYBOARD_INTERACTIVE = 4,
} ssh_auth_method_t;

/* SSH channel types */
typedef enum {
    SSH_CHANNEL_SESSION = 0,
    SSH_CHANNEL_DIRECT_TCPIP = 1,
    SSH_CHANNEL_FORWARDED_TCPIP = 2,
} ssh_channel_type_t;

/* SSH channel states */
typedef enum {
    SSH_CHANSTATE_CLOSED = 0,
    SSH_CHANSTATE_OPENING = 1,
    SSH_CHANSTATE_OPEN = 2,
    SSH_CHANSTATE_EOF = 3,
} ssh_channel_state_t;

/* Key types */
typedef enum {
    SSH_KEYTYPE_UNKNOWN = 0,
    SSH_KEYTYPE_RSA = 1,
    SSH_KEYTYPE_ED25519 = 2,
} ssh_keytype_t;

/* Forward declarations */
typedef struct ssh_session ssh_session_t;
typedef struct ssh_channel ssh_channel_t;
typedef struct ssh_key ssh_key_t;

/* Host key verification callback */
typedef int (*ssh_hostkey_callback_t)(ssh_session_t *session,
                                       const char *hostname,
                                       const uint8_t *key,
                                       size_t key_len,
                                       ssh_keytype_t keytype,
                                       void *userdata);

/*=============================================================================
 * Session Management
 *===========================================================================*/

/**
 * @brief Create a new SSH session.
 * @return New session or NULL on error.
 */
ssh_session_t *ssh_new(void);

/**
 * @brief Free an SSH session.
 * @param session Session to free.
 */
void ssh_free(ssh_session_t *session);

/**
 * @brief Set the hostname for connection.
 * @param session SSH session.
 * @param hostname Hostname or IP address.
 * @return SSH_OK on success.
 */
int ssh_set_host(ssh_session_t *session, const char *hostname);

/**
 * @brief Set the port for connection.
 * @param session SSH session.
 * @param port Port number (default 22).
 * @return SSH_OK on success.
 */
int ssh_set_port(ssh_session_t *session, uint16_t port);

/**
 * @brief Set the username for authentication.
 * @param session SSH session.
 * @param username Username string.
 * @return SSH_OK on success.
 */
int ssh_set_user(ssh_session_t *session, const char *username);

/**
 * @brief Set host key verification callback.
 * @param session SSH session.
 * @param callback Callback function.
 * @param userdata User data passed to callback.
 * @return SSH_OK on success.
 */
int ssh_set_hostkey_callback(ssh_session_t *session,
                             ssh_hostkey_callback_t callback,
                             void *userdata);

/**
 * @brief Connect to SSH server.
 * @param session SSH session.
 * @return SSH_OK on success, error code otherwise.
 */
int ssh_connect(ssh_session_t *session);

/**
 * @brief Disconnect from SSH server.
 * @param session SSH session.
 * @return SSH_OK on success.
 */
int ssh_disconnect(ssh_session_t *session);

/**
 * @brief Get the last error message.
 * @param session SSH session.
 * @return Error message string.
 */
const char *ssh_get_error(ssh_session_t *session);

/**
 * @brief Get the server's host key.
 * @param session Connected SSH session.
 * @param key Output buffer for key.
 * @param key_len Output: key length.
 * @param keytype Output: key type.
 * @return SSH_OK on success.
 */
int ssh_get_server_hostkey(ssh_session_t *session,
                           uint8_t *key,
                           size_t *key_len,
                           ssh_keytype_t *keytype);

/*=============================================================================
 * Authentication
 *===========================================================================*/

/**
 * @brief Get supported authentication methods.
 * @param session Connected SSH session.
 * @return Bitmask of ssh_auth_method_t values.
 */
int ssh_get_auth_methods(ssh_session_t *session);

/**
 * @brief Authenticate with password.
 * @param session SSH session.
 * @param password Password string.
 * @return SSH_OK on success, SSH_AUTH_DENIED if denied.
 */
int ssh_auth_password(ssh_session_t *session, const char *password);

/**
 * @brief Authenticate with public key.
 * @param session SSH session.
 * @param key Private key to use.
 * @return SSH_OK on success, SSH_AUTH_DENIED if denied.
 */
int ssh_auth_publickey(ssh_session_t *session, ssh_key_t *key);

/**
 * @brief Try public key authentication (without signing).
 * @param session SSH session.
 * @param key Public key to try.
 * @return SSH_OK if key is acceptable, SSH_AUTH_DENIED otherwise.
 */
int ssh_auth_try_publickey(ssh_session_t *session, ssh_key_t *key);

/*=============================================================================
 * Key Management
 *===========================================================================*/

/**
 * @brief Load private key from file.
 * @param filename Path to key file.
 * @param passphrase Passphrase for encrypted keys (NULL if unencrypted).
 * @return Key object or NULL on error.
 */
ssh_key_t *ssh_key_load(const char *filename, const char *passphrase);

/**
 * @brief Load private key from memory.
 * @param data Key data (OpenSSH format).
 * @param len Length of data.
 * @param passphrase Passphrase for encrypted keys.
 * @return Key object or NULL on error.
 */
ssh_key_t *ssh_key_load_mem(const uint8_t *data, size_t len, const char *passphrase);

/**
 * @brief Free a key object.
 * @param key Key to free.
 */
void ssh_key_free(ssh_key_t *key);

/**
 * @brief Get key type.
 * @param key Key object.
 * @return Key type.
 */
ssh_keytype_t ssh_key_type(ssh_key_t *key);

/**
 * @brief Get public key blob.
 * @param key Key object.
 * @param blob Output buffer.
 * @param len Input: buffer size, Output: blob length.
 * @return SSH_OK on success.
 */
int ssh_key_get_public_blob(ssh_key_t *key, uint8_t *blob, size_t *len);

/*=============================================================================
 * Channel Management
 *===========================================================================*/

/**
 * @brief Open a new channel.
 * @param session SSH session.
 * @return New channel or NULL on error.
 */
ssh_channel_t *ssh_channel_new(ssh_session_t *session);

/**
 * @brief Free a channel.
 * @param channel Channel to free.
 */
void ssh_channel_free(ssh_channel_t *channel);

/**
 * @brief Open a session channel.
 * @param channel Channel to open.
 * @return SSH_OK on success.
 */
int ssh_channel_open_session(ssh_channel_t *channel);

/**
 * @brief Request a PTY.
 * @param channel Open channel.
 * @param term Terminal type (e.g., "xterm").
 * @param cols Terminal width.
 * @param rows Terminal height.
 * @return SSH_OK on success.
 */
int ssh_channel_request_pty(ssh_channel_t *channel,
                            const char *term,
                            int cols,
                            int rows);

/**
 * @brief Request shell execution.
 * @param channel Open channel with PTY.
 * @return SSH_OK on success.
 */
int ssh_channel_request_shell(ssh_channel_t *channel);

/**
 * @brief Execute a command.
 * @param channel Open channel.
 * @param command Command string.
 * @return SSH_OK on success.
 */
int ssh_channel_request_exec(ssh_channel_t *channel, const char *command);

/**
 * @brief Request a subsystem (e.g., "sftp").
 * @param channel Open channel.
 * @param subsystem Subsystem name.
 * @return SSH_OK on success.
 */
int ssh_channel_request_subsystem(ssh_channel_t *channel, const char *subsystem);

/**
 * @brief Send data on channel.
 * @param channel Open channel.
 * @param data Data to send.
 * @param len Data length.
 * @return Bytes sent or error code.
 */
ssize_t ssh_channel_write(ssh_channel_t *channel, const void *data, size_t len);

/**
 * @brief Receive data from channel.
 * @param channel Open channel.
 * @param buffer Buffer for data.
 * @param len Buffer size.
 * @param is_stderr Set to 1 if data is from stderr.
 * @return Bytes received, 0 on EOF, or error code.
 */
ssize_t ssh_channel_read(ssh_channel_t *channel, void *buffer, size_t len, int *is_stderr);

/**
 * @brief Send EOF on channel.
 * @param channel Open channel.
 * @return SSH_OK on success.
 */
int ssh_channel_send_eof(ssh_channel_t *channel);

/**
 * @brief Close channel.
 * @param channel Channel to close.
 * @return SSH_OK on success.
 */
int ssh_channel_close(ssh_channel_t *channel);

/**
 * @brief Check if channel is open.
 * @param channel Channel to check.
 * @return 1 if open, 0 otherwise.
 */
int ssh_channel_is_open(ssh_channel_t *channel);

/**
 * @brief Check if channel has received EOF.
 * @param channel Channel to check.
 * @return 1 if EOF received, 0 otherwise.
 */
int ssh_channel_is_eof(ssh_channel_t *channel);

/**
 * @brief Get exit status of remote command.
 * @param channel Channel that executed command.
 * @return Exit status or -1 if not available.
 */
int ssh_channel_get_exit_status(ssh_channel_t *channel);

/**
 * @brief Change PTY window size.
 * @param channel Channel with PTY.
 * @param cols New width.
 * @param rows New height.
 * @return SSH_OK on success.
 */
int ssh_channel_change_pty_size(ssh_channel_t *channel, int cols, int rows);

/**
 * @brief Poll for available data.
 * @param channel Channel to poll.
 * @param timeout_ms Timeout in milliseconds (-1 for infinite).
 * @return >0 if data available, 0 on timeout, <0 on error.
 */
int ssh_channel_poll(ssh_channel_t *channel, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _SSH_H */
