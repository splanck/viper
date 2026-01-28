/**
 * @file websocket.h
 * @brief User-space WebSocket client library for ViperDOS.
 *
 * Provides WebSocket (RFC 6455) client functionality using the existing
 * socket and TLS infrastructure. Supports text/binary frames, ping/pong,
 * and graceful close.
 */

#ifndef VIPER_WEBSOCKET_H
#define VIPER_WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WebSocket error codes */
#define WS_OK 0
#define WS_ERROR -1
#define WS_ERROR_CONNECT -2
#define WS_ERROR_HANDSHAKE -3
#define WS_ERROR_CLOSED -4
#define WS_ERROR_PROTOCOL -5
#define WS_ERROR_MEMORY -6
#define WS_ERROR_TIMEOUT -7

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA

/* WebSocket close status codes */
#define WS_CLOSE_NORMAL 1000
#define WS_CLOSE_GOING_AWAY 1001
#define WS_CLOSE_PROTOCOL_ERROR 1002
#define WS_CLOSE_UNSUPPORTED 1003

/* Maximum sizes */
#define WS_MAX_FRAME_SIZE 65536
#define WS_MAX_URL 1024
#define WS_MAX_HEADER 256

/* WebSocket connection state */
typedef enum {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED,
} ws_state_t;

/* WebSocket frame (received) */
typedef struct {
    uint8_t opcode;
    uint8_t fin;     /* Final fragment */
    void *data;      /* Payload data */
    size_t data_len; /* Payload length */
} ws_frame_t;

/* WebSocket connection */
typedef struct ws_conn {
    int socket_fd;                       /* Underlying socket */
    int tls_session;                     /* TLS session handle (-1 if plain) */
    ws_state_t state;                    /* Connection state */
    int use_tls;                         /* Whether connection uses TLS */
    char host[256];                      /* Remote host */
    uint16_t port;                       /* Remote port */
    char path[512];                      /* WebSocket path */
    uint8_t recv_buf[WS_MAX_FRAME_SIZE]; /* Receive buffer */
    size_t recv_len;                     /* Bytes in receive buffer */
} ws_conn_t;

/**
 * @brief Connect to a WebSocket server.
 *
 * Performs the HTTP upgrade handshake and transitions to the WebSocket
 * protocol. Supports ws:// and wss:// URLs.
 *
 * @param url WebSocket URL (ws:// or wss://).
 * @param conn Output connection handle.
 * @return WS_OK on success, negative error code on failure.
 */
int ws_connect(const char *url, ws_conn_t *conn);

/**
 * @brief Send a text frame.
 *
 * @param conn WebSocket connection.
 * @param text NUL-terminated text string.
 * @return WS_OK on success, negative error code on failure.
 */
int ws_send_text(ws_conn_t *conn, const char *text);

/**
 * @brief Send a binary frame.
 *
 * @param conn WebSocket connection.
 * @param data Binary data.
 * @param len Data length in bytes.
 * @return WS_OK on success, negative error code on failure.
 */
int ws_send_binary(ws_conn_t *conn, const void *data, size_t len);

/**
 * @brief Send a ping frame.
 *
 * @param conn WebSocket connection.
 * @return WS_OK on success, negative error code on failure.
 */
int ws_send_ping(ws_conn_t *conn);

/**
 * @brief Receive a WebSocket frame.
 *
 * Blocks until a frame is available or timeout. The caller must free
 * frame->data when done.
 *
 * @param conn WebSocket connection.
 * @param frame Output frame.
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite).
 * @return WS_OK on success, negative error code on failure.
 */
int ws_recv(ws_conn_t *conn, ws_frame_t *frame, int timeout_ms);

/**
 * @brief Initiate a graceful close.
 *
 * Sends a close frame with the given status code.
 *
 * @param conn WebSocket connection.
 * @param status_code Close status code (e.g., WS_CLOSE_NORMAL).
 * @return WS_OK on success, negative error code on failure.
 */
int ws_close(ws_conn_t *conn, uint16_t status_code);

/**
 * @brief Free a received frame's data.
 *
 * @param frame Frame to free.
 */
void ws_frame_free(ws_frame_t *frame);

/**
 * @brief Get the current connection state.
 *
 * @param conn WebSocket connection.
 * @return Current state.
 */
ws_state_t ws_get_state(const ws_conn_t *conn);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_WEBSOCKET_H */
