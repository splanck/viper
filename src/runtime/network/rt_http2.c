//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http2.c
// Purpose: Internal HTTP/2 + HPACK transport used by the HTTPS runtime.
//
//===----------------------------------------------------------------------===//

#include "rt_http2.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <pthread.h>
#include <strings.h>
#include "rt_http2_internal.h"
#endif

#define H2_FRAME_DATA 0x0
#define H2_FRAME_HEADERS 0x1
#define H2_FRAME_PRIORITY 0x2
#define H2_FRAME_RST_STREAM 0x3
#define H2_FRAME_SETTINGS 0x4
#define H2_FRAME_PUSH_PROMISE 0x5
#define H2_FRAME_PING 0x6
#define H2_FRAME_GOAWAY 0x7
#define H2_FRAME_WINDOW_UPDATE 0x8
#define H2_FRAME_CONTINUATION 0x9

#define H2_FLAG_END_STREAM 0x1
#define H2_FLAG_ACK 0x1
#define H2_FLAG_END_HEADERS 0x4
#define H2_FLAG_PADDED 0x8
#define H2_FLAG_PRIORITY 0x20

#define H2_SETTINGS_HEADER_TABLE_SIZE 0x1
#define H2_SETTINGS_ENABLE_PUSH 0x2
#define H2_SETTINGS_MAX_CONCURRENT_STREAMS 0x3
#define H2_SETTINGS_INITIAL_WINDOW_SIZE 0x4
#define H2_SETTINGS_MAX_FRAME_SIZE 0x5
#define H2_SETTINGS_MAX_HEADER_LIST_SIZE 0x6

#define H2_ERROR_NO_ERROR 0x0
#define H2_ERROR_PROTOCOL 0x1
#define H2_ERROR_INTERNAL 0x2
#define H2_ERROR_FLOW_CONTROL 0x3
#define H2_ERROR_SETTINGS_TIMEOUT 0x4
#define H2_ERROR_STREAM_CLOSED 0x5
#define H2_ERROR_FRAME_SIZE 0x6
#define H2_ERROR_REFUSED_STREAM 0x7
#define H2_ERROR_CANCEL 0x8
#define H2_ERROR_COMPRESSION 0x9

#define H2_DEFAULT_WINDOW_SIZE 65535u
#define H2_MAX_WINDOW_SIZE 2147483647u
#define H2_DEFAULT_FRAME_SIZE 16384u
#define H2_MAX_FRAME_SIZE 16777215u
#define H2_MAX_HEADER_BLOCK (256u * 1024u)


typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
    uint8_t *payload;
    size_t payload_len;
} h2_frame_t;

struct rt_http2_conn {
    rt_http2_io_t io;
    int is_server;
    int started;
    int closed;
    char error[256];
    uint32_t peer_initial_window;
    uint32_t peer_max_frame_size;
    uint32_t local_max_frame_size;
    int64_t peer_conn_window;
    int next_stream_id;
    int sent_goaway;
    hpack_dyn_table_t encode_table;
    hpack_dyn_table_t decode_table;
};

static const char kClientPreface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";




static void h2_write_u16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)(v);
}

static void h2_write_u24(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v >> 16);
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v);
}

static void h2_write_u32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)(v);
}

static uint16_t h2_read_u16(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t h2_read_u24(const uint8_t *src) {
    return ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | (uint32_t)src[2];
}

static uint32_t h2_read_u32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void h2_conn_set_error(rt_http2_conn_t *conn, const char *msg) {
    if (!conn)
        return;
    snprintf(conn->error, sizeof(conn->error), "%s", msg ? msg : "HTTP/2 error");
}

static int h2_conn_fail(rt_http2_conn_t *conn, const char *msg) {
    h2_conn_set_error(conn, msg);
    conn->closed = 1;
    return 0;
}

static int h2_buf_reserve(h2_buf_t *buf, size_t needed) {
    size_t new_cap = 0;
    uint8_t *grown = NULL;
    if (!buf)
        return 0;
    if (needed <= buf->cap)
        return 1;
    new_cap = buf->cap ? buf->cap : 256;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2)
            return 0;
        new_cap *= 2;
    }
    grown = (uint8_t *)realloc(buf->data, new_cap);
    if (!grown)
        return 0;
    buf->data = grown;
    buf->cap = new_cap;
    return 1;
}

int h2_buf_append(h2_buf_t *buf, const void *src, size_t len) {
    if (!buf || (!src && len > 0))
        return 0;
    if (len == 0)
        return 1;
    if (buf->len > SIZE_MAX - len)
        return 0;
    if (!h2_buf_reserve(buf, buf->len + len))
        return 0;
    memcpy(buf->data + buf->len, src, len);
    buf->len += len;
    return 1;
}

int h2_buf_append_byte(h2_buf_t *buf, uint8_t b) {
    return h2_buf_append(buf, &b, 1);
}

void h2_buf_free(h2_buf_t *buf) {
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

int h2_header_name_bytes_is_valid(const char *name, size_t len) {
    if (!name || len == 0)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c == 0x7f)
            return 0;
        if (c >= 'A' && c <= 'Z')
            return 0;
    }
    return 1;
}

int h2_header_name_is_valid(const char *name) {
    return name && h2_header_name_bytes_is_valid(name, strlen(name));
}

int h2_header_value_bytes_is_valid(const char *value, size_t len) {
    if (!value)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c == '\0' || c == '\r' || c == '\n')
            return 0;
    }
    return 1;
}

int h2_header_value_is_valid(const char *value) {
    return value && h2_header_value_bytes_is_valid(value, strlen(value));
}

static int h2_parse_status_code(const char *value, int *status_out) {
    if (!value || !status_out || strlen(value) != 3)
        return 0;
    if (!isdigit((unsigned char)value[0]) || !isdigit((unsigned char)value[1]) ||
        !isdigit((unsigned char)value[2]))
        return 0;
    int status = (value[0] - '0') * 100 + (value[1] - '0') * 10 + (value[2] - '0');
    if (status < 100 || status > 599)
        return 0;
    *status_out = status;
    return 1;
}

char *h2_strdup_range(const uint8_t *src, size_t len) {
    if (len == SIZE_MAX)
        return NULL;
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    if (len > 0)
        memcpy(out, src, len);
    out[len] = '\0';
    return out;
}

char *h2_strdup_lower(const char *src) {
    size_t len = src ? strlen(src) : 0;
    if (len == SIZE_MAX)
        return NULL;
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    out[len] = '\0';
    return out;
}

static int h2_header_is_connection_specific(const char *name, const char *value) {
    if (!name)
        return 0;
    if (strcasecmp(name, "connection") == 0 || strcasecmp(name, "proxy-connection") == 0 ||
        strcasecmp(name, "keep-alive") == 0 || strcasecmp(name, "upgrade") == 0 ||
        strcasecmp(name, "transfer-encoding") == 0) {
        return 1;
    }
    if (strcasecmp(name, "te") == 0 && value && strcasecmp(value, "trailers") != 0)
        return 1;
    return 0;
}

int rt_http2_header_append_copy(rt_http2_header_t **list, const char *name, const char *value) {
    rt_http2_header_t *node = NULL;
    rt_http2_header_t **tail = list;
    if (!list || !name || !value)
        return 0;
    node = (rt_http2_header_t *)calloc(1, sizeof(*node));
    if (!node)
        return 0;
    node->name = strdup(name);
    node->value = strdup(value);
    if (!node->name || !node->value) {
        free(node->name);
        free(node->value);
        free(node);
        return 0;
    }
    while (*tail)
        tail = &(*tail)->next;
    *tail = node;
    return 1;
}

const char *rt_http2_header_get(const rt_http2_header_t *list, const char *name) {
    for (const rt_http2_header_t *it = list; it; it = it->next) {
        if (it->name && name && strcasecmp(it->name, name) == 0)
            return it->value;
    }
    return NULL;
}

void rt_http2_headers_free(rt_http2_header_t *headers) {
    while (headers) {
        rt_http2_header_t *next = headers->next;
        free(headers->name);
        free(headers->value);
        free(headers);
        headers = next;
    }
}

void rt_http2_request_free(rt_http2_request_t *req) {
    if (!req)
        return;
    free(req->method);
    free(req->scheme);
    free(req->authority);
    free(req->path);
    rt_http2_headers_free(req->headers);
    free(req->body);
    memset(req, 0, sizeof(*req));
}

void rt_http2_response_free(rt_http2_response_t *res) {
    if (!res)
        return;
    rt_http2_headers_free(res->headers);
    free(res->body);
    memset(res, 0, sizeof(*res));
}


static int h2_io_read_exact(rt_http2_conn_t *conn, uint8_t *buf, size_t len) {
    size_t total = 0;
    if (!conn || !buf || !conn->io.read)
        return h2_conn_fail(conn, "HTTP/2: invalid read callback");
    while (total < len) {
        long n = conn->io.read(conn->io.ctx, buf + total, len - total);
        if (n <= 0)
            return h2_conn_fail(conn, "HTTP/2: read failed");
        total += (size_t)n;
    }
    return 1;
}

static int h2_io_write_all(rt_http2_conn_t *conn, const uint8_t *buf, size_t len) {
    size_t total = 0;
    if (!conn || (!buf && len > 0) || !conn->io.write)
        return h2_conn_fail(conn, "HTTP/2: invalid write callback");
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > INT_MAX)
            chunk = INT_MAX;
        if (!conn->io.write(conn->io.ctx, buf + total, chunk))
            return h2_conn_fail(conn, "HTTP/2: write failed");
        total += chunk;
    }
    return 1;
}

static void h2_frame_free(h2_frame_t *frame) {
    if (!frame)
        return;
    free(frame->payload);
    memset(frame, 0, sizeof(*frame));
}

static int h2_send_frame(rt_http2_conn_t *conn,
                         uint8_t type,
                         uint8_t flags,
                         uint32_t stream_id,
                         const uint8_t *payload,
                         size_t payload_len) {
    uint8_t header[9];
    if (!conn || stream_id > 0x7fffffffu || payload_len > H2_MAX_FRAME_SIZE)
        return h2_conn_fail(conn, "HTTP/2: invalid frame parameters");
    h2_write_u24(header, (uint32_t)payload_len);
    header[3] = type;
    header[4] = flags;
    header[5] = (uint8_t)((stream_id >> 24) & 0x7f);
    header[6] = (uint8_t)(stream_id >> 16);
    header[7] = (uint8_t)(stream_id >> 8);
    header[8] = (uint8_t)stream_id;
    if (!h2_io_write_all(conn, header, sizeof(header)))
        return 0;
    if (payload_len > 0 && !h2_io_write_all(conn, payload, payload_len))
        return 0;
    return 1;
}

static int h2_read_frame(rt_http2_conn_t *conn, h2_frame_t *frame) {
    uint8_t header[9];
    size_t payload_len = 0;
    if (!conn || !frame)
        return 0;
    memset(frame, 0, sizeof(*frame));
    if (!h2_io_read_exact(conn, header, sizeof(header)))
        return 0;
    payload_len = h2_read_u24(header);
    if (payload_len > conn->local_max_frame_size)
        return h2_conn_fail(conn, "HTTP/2: oversized frame");
    frame->type = header[3];
    frame->flags = header[4];
    frame->stream_id = h2_read_u32(header + 5) & 0x7fffffffu;
    frame->payload_len = payload_len;
    if (payload_len > 0) {
        frame->payload = (uint8_t *)malloc(payload_len);
        if (!frame->payload)
            return h2_conn_fail(conn, "HTTP/2: frame allocation failed");
        if (!h2_io_read_exact(conn, frame->payload, payload_len)) {
            h2_frame_free(frame);
            return 0;
        }
    }
    return 1;
}

static int h2_send_settings(rt_http2_conn_t *conn) {
    uint8_t payload[6];
    if (!conn)
        return 0;
    if (conn->is_server)
        return h2_send_frame(conn, H2_FRAME_SETTINGS, 0, 0, NULL, 0);
    h2_write_u16(payload, H2_SETTINGS_ENABLE_PUSH);
    h2_write_u32(payload + 2, 0);
    return h2_send_frame(conn, H2_FRAME_SETTINGS, 0, 0, payload, sizeof(payload));
}

static int h2_apply_setting(rt_http2_conn_t *conn,
                            uint16_t id,
                            uint32_t value,
                            int64_t *stream_window_io) {
    switch (id) {
        case H2_SETTINGS_HEADER_TABLE_SIZE:
            if (value > H2_MAX_DYNAMIC_TABLE_SIZE)
                return 0;
            hpack_dyn_table_set_max_size(&conn->encode_table, value);
            break;
        case H2_SETTINGS_ENABLE_PUSH:
            if (value != 0 && value != 1)
                return 0;
            break;
        case H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            break;
        case H2_SETTINGS_INITIAL_WINDOW_SIZE: {
            if (value > H2_MAX_WINDOW_SIZE)
                return 0;
            int64_t delta = (int64_t)value - (int64_t)conn->peer_initial_window;
            conn->peer_initial_window = value;
            if (stream_window_io)
                *stream_window_io += delta;
            break;
        }
        case H2_SETTINGS_MAX_FRAME_SIZE:
            if (value < H2_DEFAULT_FRAME_SIZE || value > H2_MAX_FRAME_SIZE)
                return 0;
            conn->peer_max_frame_size = value;
            break;
        case H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            break;
        default:
            break;
    }
    return 1;
}

static int h2_send_window_update(rt_http2_conn_t *conn, uint32_t stream_id, uint32_t increment) {
    uint8_t payload[4];
    if (increment == 0 || increment > 0x7fffffffu)
        return h2_conn_fail(conn, "HTTP/2: invalid WINDOW_UPDATE increment");
    payload[0] = (uint8_t)((increment >> 24) & 0x7f);
    payload[1] = (uint8_t)(increment >> 16);
    payload[2] = (uint8_t)(increment >> 8);
    payload[3] = (uint8_t)increment;
    return h2_send_frame(conn, H2_FRAME_WINDOW_UPDATE, 0, stream_id, payload, sizeof(payload));
}

static int h2_send_rst_stream(rt_http2_conn_t *conn, uint32_t stream_id, uint32_t error_code) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(error_code >> 24);
    payload[1] = (uint8_t)(error_code >> 16);
    payload[2] = (uint8_t)(error_code >> 8);
    payload[3] = (uint8_t)error_code;
    return h2_send_frame(conn, H2_FRAME_RST_STREAM, 0, stream_id, payload, sizeof(payload));
}

static int h2_parse_headers_payload(const h2_frame_t *frame,
                                    const uint8_t **fragment_out,
                                    size_t *fragment_len_out,
                                    int *end_stream_out) {
    size_t pos = 0;
    uint8_t pad_len = 0;
    if (!frame || frame->type != H2_FRAME_HEADERS)
        return 0;
    if (frame->flags & H2_FLAG_PADDED) {
        if (frame->payload_len < 1)
            return 0;
        pad_len = frame->payload[0];
        pos++;
    }
    if (frame->flags & H2_FLAG_PRIORITY) {
        if (frame->payload_len < pos + 5)
            return 0;
        pos += 5;
    }
    if (frame->payload_len < pos + pad_len)
        return 0;
    if (fragment_out)
        *fragment_out = frame->payload + pos;
    if (fragment_len_out)
        *fragment_len_out = frame->payload_len - pos - pad_len;
    if (end_stream_out)
        *end_stream_out = (frame->flags & H2_FLAG_END_STREAM) != 0;
    return 1;
}

static int h2_parse_data_payload(const h2_frame_t *frame,
                                 const uint8_t **data_out,
                                 size_t *data_len_out,
                                 int *end_stream_out) {
    size_t pos = 0;
    uint8_t pad_len = 0;
    if (!frame || frame->type != H2_FRAME_DATA)
        return 0;
    if (frame->flags & H2_FLAG_PADDED) {
        if (frame->payload_len < 1)
            return 0;
        pad_len = frame->payload[0];
        pos++;
    }
    if (frame->payload_len < pos + pad_len)
        return 0;
    if (data_out)
        *data_out = frame->payload + pos;
    if (data_len_out)
        *data_len_out = frame->payload_len - pos - pad_len;
    if (end_stream_out)
        *end_stream_out = (frame->flags & H2_FLAG_END_STREAM) != 0;
    return 1;
}

static int h2_collect_header_block(rt_http2_conn_t *conn,
                                   const h2_frame_t *first,
                                   h2_buf_t *block_out,
                                   int *end_stream_out) {
    const uint8_t *fragment = NULL;
    size_t fragment_len = 0;
    h2_frame_t frame;
    if (!conn || !first || !block_out)
        return 0;
    memset(block_out, 0, sizeof(*block_out));
    if (!h2_parse_headers_payload(first, &fragment, &fragment_len, end_stream_out) ||
        !h2_buf_append(block_out, fragment, fragment_len)) {
        h2_buf_free(block_out);
        return h2_conn_fail(conn, "HTTP/2: malformed HEADERS frame");
    }
    if (block_out->len > H2_MAX_HEADER_BLOCK) {
        h2_buf_free(block_out);
        return h2_conn_fail(conn, "HTTP/2: header block too large");
    }
    if (first->flags & H2_FLAG_END_HEADERS)
        return 1;

    while (1) {
        memset(&frame, 0, sizeof(frame));
        if (!h2_read_frame(conn, &frame)) {
            h2_buf_free(block_out);
            return 0;
        }
        if (frame.type != H2_FRAME_CONTINUATION || frame.stream_id != first->stream_id) {
            h2_frame_free(&frame);
            h2_buf_free(block_out);
            return h2_conn_fail(conn, "HTTP/2: invalid CONTINUATION sequence");
        }
        if (!h2_buf_append(block_out, frame.payload, frame.payload_len)) {
            h2_frame_free(&frame);
            h2_buf_free(block_out);
            return h2_conn_fail(conn, "HTTP/2: header block allocation failed");
        }
        if (block_out->len > H2_MAX_HEADER_BLOCK) {
            h2_frame_free(&frame);
            h2_buf_free(block_out);
            return h2_conn_fail(conn, "HTTP/2: header block too large");
        }
        if (frame.flags & H2_FLAG_END_HEADERS) {
            h2_frame_free(&frame);
            return 1;
        }
        h2_frame_free(&frame);
    }
}

static int h2_decode_header_list(rt_http2_conn_t *conn,
                                 const h2_frame_t *first,
                                 rt_http2_header_t **decoded_out,
                                 int *end_stream_out,
                                 const char *decode_error) {
    h2_buf_t header_block;
    if (!conn || !first || !decoded_out)
        return 0;
    memset(&header_block, 0, sizeof(header_block));
    *decoded_out = NULL;
    if (!h2_collect_header_block(conn, first, &header_block, end_stream_out))
        return 0;
    if (!hpack_decode_header_block(
            &conn->decode_table, header_block.data, header_block.len, decoded_out)) {
        h2_buf_free(&header_block);
        return h2_conn_fail(conn, decode_error);
    }
    h2_buf_free(&header_block);
    return 1;
}

static int h2_append_trailer_headers(rt_http2_header_t **dest, const rt_http2_header_t *decoded) {
    for (const rt_http2_header_t *it = decoded; it; it = it->next) {
        if (it->name[0] == ':' || h2_header_is_connection_specific(it->name, it->value) ||
            !rt_http2_header_append_copy(dest, it->name, it->value)) {
            return 0;
        }
    }
    return 1;
}

static int h2_refuse_concurrent_request_stream(rt_http2_conn_t *conn, const h2_frame_t *frame) {
    h2_buf_t discard = {0};
    const uint8_t *data_ptr = NULL;
    size_t data_len = 0;
    int end_stream = 0;
    if (!conn || !frame || frame->stream_id == 0 || (frame->stream_id & 1u) == 0u)
        return h2_conn_fail(conn, "HTTP/2: invalid concurrent request stream");
    if (frame->type == H2_FRAME_HEADERS) {
        if (!h2_collect_header_block(conn, frame, &discard, &end_stream))
            return 0;
        h2_buf_free(&discard);
    } else if (frame->type == H2_FRAME_DATA) {
        if (!h2_parse_data_payload(frame, &data_ptr, &data_len, &end_stream))
            return h2_conn_fail(conn, "HTTP/2: invalid concurrent request body");
        if (data_len > 0 && !h2_send_window_update(conn, 0, (uint32_t)data_len))
            return 0;
    } else {
        return h2_conn_fail(conn, "HTTP/2: unsupported concurrent request frame");
    }
    return h2_send_rst_stream(conn, frame->stream_id, H2_ERROR_REFUSED_STREAM);
}

static int h2_send_headers_block(rt_http2_conn_t *conn,
                                 uint32_t stream_id,
                                 const uint8_t *block,
                                 size_t block_len,
                                 int end_stream) {
    size_t pos = 0;
    size_t chunk = 0;
    uint8_t flags = 0;
    if (!conn)
        return 0;
    do {
        chunk = block_len - pos;
        if (chunk > conn->peer_max_frame_size)
            chunk = conn->peer_max_frame_size;
        flags = 0;
        if (pos == 0 && end_stream && block_len == chunk)
            flags |= H2_FLAG_END_STREAM;
        if (pos + chunk == block_len)
            flags |= H2_FLAG_END_HEADERS;
        if (!h2_send_frame(conn,
                           pos == 0 ? H2_FRAME_HEADERS : H2_FRAME_CONTINUATION,
                           flags,
                           stream_id,
                           block + pos,
                           chunk)) {
            return 0;
        }
        pos += chunk;
    } while (pos < block_len);
    if (block_len == 0) {
        flags = H2_FLAG_END_HEADERS;
        if (end_stream)
            flags |= H2_FLAG_END_STREAM;
        return h2_send_frame(conn, H2_FRAME_HEADERS, flags, stream_id, NULL, 0);
    }
    return 1;
}

static int h2_handle_common_frame(rt_http2_conn_t *conn,
                                  const h2_frame_t *frame,
                                  uint32_t stream_id,
                                  int64_t *stream_window_io,
                                  int *handled_out) {
    if (handled_out)
        *handled_out = 0;
    if (!conn || !frame)
        return 0;
    switch (frame->type) {
        case H2_FRAME_SETTINGS:
            if (frame->stream_id != 0 ||
                ((frame->flags & H2_FLAG_ACK) == 0 && (frame->payload_len % 6) != 0) ||
                ((frame->flags & H2_FLAG_ACK) != 0 && frame->payload_len != 0)) {
                return h2_conn_fail(conn, "HTTP/2: malformed SETTINGS frame");
            }
            if ((frame->flags & H2_FLAG_ACK) == 0) {
                for (size_t pos = 0; pos < frame->payload_len; pos += 6) {
                    uint16_t id = h2_read_u16(frame->payload + pos);
                    uint32_t value = h2_read_u32(frame->payload + pos + 2);
                    if (!h2_apply_setting(conn, id, value, stream_window_io))
                        return h2_conn_fail(conn, "HTTP/2: invalid SETTINGS value");
                }
                if (!h2_send_frame(conn, H2_FRAME_SETTINGS, H2_FLAG_ACK, 0, NULL, 0))
                    return 0;
            }
            if (handled_out)
                *handled_out = 1;
            return 1;

        case H2_FRAME_PING:
            if (frame->stream_id != 0 || frame->payload_len != 8)
                return h2_conn_fail(conn, "HTTP/2: malformed PING frame");
            if ((frame->flags & H2_FLAG_ACK) == 0 &&
                !h2_send_frame(conn, H2_FRAME_PING, H2_FLAG_ACK, 0, frame->payload, 8)) {
                return 0;
            }
            if (handled_out)
                *handled_out = 1;
            return 1;

        case H2_FRAME_WINDOW_UPDATE: {
            uint32_t increment = 0;
            if (frame->payload_len != 4)
                return h2_conn_fail(conn, "HTTP/2: malformed WINDOW_UPDATE frame");
            increment = h2_read_u32(frame->payload) & 0x7fffffffu;
            if (increment == 0)
                return h2_conn_fail(conn, "HTTP/2: zero WINDOW_UPDATE");
            if (frame->stream_id == 0) {
                if (conn->peer_conn_window > (int64_t)H2_MAX_WINDOW_SIZE - (int64_t)increment)
                    return h2_conn_fail(conn, "HTTP/2: connection flow-control window overflow");
                conn->peer_conn_window += increment;
            } else if (frame->stream_id == stream_id && stream_window_io) {
                if (*stream_window_io > (int64_t)H2_MAX_WINDOW_SIZE - (int64_t)increment)
                    return h2_conn_fail(conn, "HTTP/2: stream flow-control window overflow");
                *stream_window_io += increment;
            }
            if (handled_out)
                *handled_out = 1;
            return 1;
        }

        case H2_FRAME_PRIORITY:
            if (frame->stream_id == 0 || frame->payload_len != 5)
                return h2_conn_fail(conn, "HTTP/2: malformed PRIORITY frame");
            if (handled_out)
                *handled_out = 1;
            return 1;

        case H2_FRAME_GOAWAY:
            if (frame->stream_id != 0 || frame->payload_len < 8)
                return h2_conn_fail(conn, "HTTP/2: malformed GOAWAY frame");
            conn->closed = 1;
            if (handled_out)
                *handled_out = 1;
            return 1;

        case H2_FRAME_PUSH_PROMISE:
            return h2_conn_fail(conn, "HTTP/2: PUSH_PROMISE is not supported");

        case H2_FRAME_RST_STREAM:
            if (frame->payload_len != 4)
                return h2_conn_fail(conn, "HTTP/2: malformed RST_STREAM frame");
            if (frame->stream_id == stream_id)
                return h2_conn_fail(conn, "HTTP/2: stream reset by peer");
            if (handled_out)
                *handled_out = 1;
            return 1;

        default:
            if (frame->type != H2_FRAME_DATA && frame->type != H2_FRAME_HEADERS &&
                frame->type != H2_FRAME_CONTINUATION && handled_out) {
                *handled_out = 1;
            }
            return 1;
    }
}

static int h2_wait_for_send_window(rt_http2_conn_t *conn,
                                   uint32_t stream_id,
                                   int64_t *stream_window_io) {
    while (conn->peer_conn_window <= 0 || (stream_window_io && *stream_window_io <= 0)) {
        h2_frame_t frame;
        int handled = 0;
        memset(&frame, 0, sizeof(frame));
        if (!h2_read_frame(conn, &frame))
            return 0;
        if (!h2_handle_common_frame(conn, &frame, stream_id, stream_window_io, &handled)) {
            h2_frame_free(&frame);
            return 0;
        }
        if (!handled) {
            h2_frame_free(&frame);
            return h2_conn_fail(conn, "HTTP/2: unexpected frame while waiting for send window");
        }
        h2_frame_free(&frame);
    }
    return 1;
}

static int h2_send_data(rt_http2_conn_t *conn,
                        uint32_t stream_id,
                        const uint8_t *data,
                        size_t data_len) {
    size_t pos = 0;
    int64_t stream_window = (int64_t)conn->peer_initial_window;
    while (pos < data_len) {
        size_t max_chunk = conn->peer_max_frame_size;
        int end_stream = 0;
        if (!h2_wait_for_send_window(conn, stream_id, &stream_window))
            return 0;
        if (conn->peer_conn_window < (int64_t)max_chunk)
            max_chunk = (size_t)conn->peer_conn_window;
        if (stream_window < (int64_t)max_chunk)
            max_chunk = (size_t)stream_window;
        if (max_chunk > data_len - pos)
            max_chunk = data_len - pos;
        end_stream = (pos + max_chunk == data_len);
        if (!h2_send_frame(conn,
                           H2_FRAME_DATA,
                           end_stream ? H2_FLAG_END_STREAM : 0,
                           stream_id,
                           data + pos,
                           max_chunk)) {
            return 0;
        }
        conn->peer_conn_window -= (int64_t)max_chunk;
        stream_window -= (int64_t)max_chunk;
        pos += max_chunk;
    }
    if (data_len == 0)
        return h2_send_frame(conn, H2_FRAME_DATA, H2_FLAG_END_STREAM, stream_id, NULL, 0);
    return 1;
}

static int h2_build_request_block(rt_http2_conn_t *conn,
                                  const char *method,
                                  const char *scheme,
                                  const char *authority,
                                  const char *path,
                                  const rt_http2_header_t *headers,
                                  h2_buf_t *out) {
    if (!conn || !method || !scheme || !authority || !path || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!hpack_encode_header_field(out, &conn->encode_table, ":method", method) ||
        !hpack_encode_header_field(out, &conn->encode_table, ":scheme", scheme) ||
        !hpack_encode_header_field(out, &conn->encode_table, ":authority", authority) ||
        !hpack_encode_header_field(out, &conn->encode_table, ":path", path)) {
        h2_buf_free(out);
        return 0;
    }
    for (const rt_http2_header_t *it = headers; it; it = it->next) {
        if (!it->name || !it->value)
            continue;
        if (it->name[0] == ':')
            continue;
        if (strcasecmp(it->name, "host") == 0)
            continue;
        if (h2_header_is_connection_specific(it->name, it->value))
            continue;
        if (!hpack_encode_header_field(out, &conn->encode_table, it->name, it->value)) {
            h2_buf_free(out);
            return 0;
        }
    }
    return 1;
}

static int h2_build_response_block(rt_http2_conn_t *conn,
                                   int status,
                                   const rt_http2_header_t *headers,
                                   h2_buf_t *out) {
    char status_buf[4];
    if (!conn || status < 100 || status > 599 || !out)
        return 0;
    snprintf(status_buf, sizeof(status_buf), "%d", status);
    memset(out, 0, sizeof(*out));
    if (!hpack_encode_header_field(out, &conn->encode_table, ":status", status_buf)) {
        h2_buf_free(out);
        return 0;
    }
    for (const rt_http2_header_t *it = headers; it; it = it->next) {
        if (!it->name || !it->value)
            continue;
        if (it->name[0] == ':')
            continue;
        if (h2_header_is_connection_specific(it->name, it->value))
            continue;
        if (!hpack_encode_header_field(out, &conn->encode_table, it->name, it->value)) {
            h2_buf_free(out);
            return 0;
        }
    }
    return 1;
}

static rt_http2_conn_t *h2_conn_new_common(const rt_http2_io_t *io, int is_server) {
    rt_http2_conn_t *conn = NULL;
    if (!io || !io->read || !io->write)
        return NULL;
    conn = (rt_http2_conn_t *)calloc(1, sizeof(*conn));
    if (!conn)
        return NULL;
    conn->io = *io;
    conn->is_server = is_server ? 1 : 0;
    conn->peer_initial_window = H2_DEFAULT_WINDOW_SIZE;
    conn->peer_max_frame_size = H2_DEFAULT_FRAME_SIZE;
    conn->local_max_frame_size = H2_DEFAULT_FRAME_SIZE;
    conn->peer_conn_window = H2_DEFAULT_WINDOW_SIZE;
    conn->next_stream_id = 1;
    conn->encode_table.max_bytes = 4096;
    conn->decode_table.max_bytes = 4096;
    return conn;
}

rt_http2_conn_t *rt_http2_client_new(const rt_http2_io_t *io) {
    return h2_conn_new_common(io, 0);
}

rt_http2_conn_t *rt_http2_server_new(const rt_http2_io_t *io) {
    return h2_conn_new_common(io, 1);
}

void rt_http2_conn_free(rt_http2_conn_t *conn) {
    if (!conn)
        return;
    hpack_dyn_table_free(&conn->encode_table);
    hpack_dyn_table_free(&conn->decode_table);
    free(conn);
}

const char *rt_http2_get_error(const rt_http2_conn_t *conn) {
    if (!conn)
        return "HTTP/2: null connection";
    return conn->error[0] ? conn->error : "no error";
}

int rt_http2_conn_is_usable(const rt_http2_conn_t *conn) {
    return conn && !conn->closed && conn->io.read && conn->io.write;
}

static int h2_client_start(rt_http2_conn_t *conn) {
    if (!conn)
        return 0;
    if (conn->started)
        return 1;
    if (!h2_io_write_all(conn, (const uint8_t *)kClientPreface, sizeof(kClientPreface) - 1) ||
        !h2_send_settings(conn)) {
        return 0;
    }
    conn->started = 1;
    return 1;
}

static int h2_server_start(rt_http2_conn_t *conn) {
    uint8_t preface[sizeof(kClientPreface) - 1];
    if (!conn)
        return 0;
    if (conn->started)
        return 1;
    if (!h2_io_read_exact(conn, preface, sizeof(preface)))
        return 0;
    if (memcmp(preface, kClientPreface, sizeof(preface)) != 0)
        return h2_conn_fail(conn, "HTTP/2: invalid client preface");
    if (!h2_send_settings(conn))
        return 0;
    conn->started = 1;
    return 1;
}

static int h2_append_body(h2_buf_t *body, size_t max_body_len, const uint8_t *src, size_t len) {
    if (!body || (!src && len > 0))
        return 0;
    if (len == 0)
        return 1;
    if (body->len > max_body_len || len > max_body_len - body->len)
        return 0;
    return h2_buf_append(body, src, len);
}

int rt_http2_client_roundtrip(rt_http2_conn_t *conn,
                              const char *method,
                              const char *scheme,
                              const char *authority,
                              const char *path,
                              const rt_http2_header_t *headers,
                              const uint8_t *body,
                              size_t body_len,
                              size_t max_body_len,
                              rt_http2_response_t *out_res) {
    h2_buf_t req_block = {0};
    h2_buf_t res_body = {0};
    uint32_t stream_id = 0;
    int saw_response_headers = 0;
    if (!conn || !out_res)
        return 0;
    memset(out_res, 0, sizeof(*out_res));
    if (!h2_client_start(conn))
        return 0;
    if ((conn->next_stream_id & 1) == 0 || conn->next_stream_id <= 0)
        return h2_conn_fail(conn, "HTTP/2: invalid client stream id");
    stream_id = (uint32_t)conn->next_stream_id;
    conn->next_stream_id += 2;
    if (!h2_build_request_block(conn, method, scheme, authority, path, headers, &req_block))
        return h2_conn_fail(conn, "HTTP/2: failed to encode request headers");
    if (!h2_send_headers_block(conn, stream_id, req_block.data, req_block.len, body_len == 0) ||
        (body_len > 0 && !h2_send_data(conn, stream_id, body, body_len))) {
        h2_buf_free(&req_block);
        return 0;
    }
    h2_buf_free(&req_block);

    while (1) {
        h2_frame_t frame;
        int handled = 0;
        memset(&frame, 0, sizeof(frame));
        if (!h2_read_frame(conn, &frame)) {
            h2_buf_free(&res_body);
            rt_http2_response_free(out_res);
            return 0;
        }
        if (!h2_handle_common_frame(conn, &frame, stream_id, NULL, &handled)) {
            h2_frame_free(&frame);
            h2_buf_free(&res_body);
            rt_http2_response_free(out_res);
            return 0;
        }
        if (handled) {
            h2_frame_free(&frame);
            continue;
        }

        if (frame.type == H2_FRAME_HEADERS && frame.stream_id == stream_id) {
            rt_http2_header_t *decoded = NULL;
            int end_stream = 0;
            if (!h2_decode_header_list(
                    conn, &frame, &decoded, &end_stream, "HTTP/2: invalid response headers")) {
                h2_frame_free(&frame);
                h2_buf_free(&res_body);
                rt_http2_response_free(out_res);
                return 0;
            }
            if (!saw_response_headers) {
                int saw_status = 0;
                int status_tmp = 0;
                rt_http2_header_t *response_headers = NULL;
                for (rt_http2_header_t *it = decoded; it; it = it->next) {
                    if (strcmp(it->name, ":status") == 0) {
                        if (saw_status) {
                            rt_http2_headers_free(response_headers);
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&res_body);
                            rt_http2_response_free(out_res);
                            return h2_conn_fail(conn, "HTTP/2: duplicate response status");
                        }
                        if (!h2_parse_status_code(it->value, &status_tmp)) {
                            rt_http2_headers_free(response_headers);
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&res_body);
                            rt_http2_response_free(out_res);
                            return h2_conn_fail(conn, "HTTP/2: invalid response status");
                        }
                        saw_status = 1;
                    } else if (it->name[0] != ':') {
                        if (h2_header_is_connection_specific(it->name, it->value) ||
                            !rt_http2_header_append_copy(&response_headers, it->name, it->value)) {
                            rt_http2_headers_free(response_headers);
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&res_body);
                            rt_http2_response_free(out_res);
                            return h2_conn_fail(conn, "HTTP/2: invalid response header");
                        }
                    } else {
                        rt_http2_headers_free(response_headers);
                        rt_http2_headers_free(decoded);
                        h2_frame_free(&frame);
                        h2_buf_free(&res_body);
                        rt_http2_response_free(out_res);
                        return h2_conn_fail(conn, "HTTP/2: invalid response pseudo-header");
                    }
                }
                if (!saw_status || status_tmp < 100 || status_tmp > 599) {
                    rt_http2_headers_free(response_headers);
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    h2_buf_free(&res_body);
                    rt_http2_response_free(out_res);
                    return h2_conn_fail(conn, "HTTP/2: missing response status");
                }
                if (status_tmp >= 100 && status_tmp < 200) {
                    rt_http2_headers_free(response_headers);
                    if (status_tmp == 101 || end_stream) {
                        rt_http2_headers_free(decoded);
                        h2_frame_free(&frame);
                        h2_buf_free(&res_body);
                        rt_http2_response_free(out_res);
                        return h2_conn_fail(conn, "HTTP/2: invalid informational response");
                    }
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    continue;
                }
                out_res->status = status_tmp;
                out_res->headers = response_headers;
                saw_response_headers = 1;
            } else {
                if (!h2_append_trailer_headers(&out_res->headers, decoded)) {
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    h2_buf_free(&res_body);
                    rt_http2_response_free(out_res);
                    return h2_conn_fail(conn, "HTTP/2: invalid response trailers");
                }
                if (!end_stream) {
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    h2_buf_free(&res_body);
                    rt_http2_response_free(out_res);
                    return h2_conn_fail(conn, "HTTP/2: response trailers missing END_STREAM");
                }
            }
            rt_http2_headers_free(decoded);
            if (end_stream) {
                out_res->stream_id = (int)stream_id;
                out_res->body = res_body.data;
                out_res->body_len = res_body.len;
                h2_frame_free(&frame);
                return 1;
            }
        } else if (frame.type == H2_FRAME_DATA && frame.stream_id == stream_id) {
            const uint8_t *data_ptr = NULL;
            size_t data_len = 0;
            int end_stream = 0;
            if (!saw_response_headers) {
                h2_frame_free(&frame);
                h2_buf_free(&res_body);
                rt_http2_response_free(out_res);
                return h2_conn_fail(conn, "HTTP/2: response DATA before HEADERS");
            }
            if (!h2_parse_data_payload(&frame, &data_ptr, &data_len, &end_stream) ||
                !h2_append_body(&res_body, max_body_len, data_ptr, data_len) ||
                (data_len > 0 && (!h2_send_window_update(conn, 0, (uint32_t)data_len) ||
                                  !h2_send_window_update(conn, stream_id, (uint32_t)data_len)))) {
                h2_frame_free(&frame);
                h2_buf_free(&res_body);
                rt_http2_response_free(out_res);
                return h2_conn_fail(conn, "HTTP/2: invalid response body");
            }
            if (end_stream) {
                out_res->stream_id = (int)stream_id;
                out_res->body = res_body.data;
                out_res->body_len = res_body.len;
                h2_frame_free(&frame);
                return 1;
            }
        } else {
            h2_frame_free(&frame);
            h2_buf_free(&res_body);
            rt_http2_response_free(out_res);
            return h2_conn_fail(conn, "HTTP/2: unexpected frame during response");
        }
        h2_frame_free(&frame);
    }
}

int rt_http2_server_receive_request(rt_http2_conn_t *conn,
                                    size_t max_body_len,
                                    rt_http2_request_t *out_req) {
    h2_buf_t body = {0};
    uint32_t active_stream = 0;
    if (!conn || !out_req)
        return 0;
    memset(out_req, 0, sizeof(*out_req));
    if (!h2_server_start(conn))
        return 0;

    while (1) {
        h2_frame_t frame;
        int handled = 0;
        memset(&frame, 0, sizeof(frame));
        if (!h2_read_frame(conn, &frame)) {
            h2_buf_free(&body);
            rt_http2_request_free(out_req);
            return 0;
        }
        if (active_stream == 0) {
            if (!h2_handle_common_frame(conn, &frame, 0, NULL, &handled)) {
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return 0;
            }
            if (handled) {
                h2_frame_free(&frame);
                continue;
            }
            if (frame.type != H2_FRAME_HEADERS || frame.stream_id == 0 ||
                (frame.stream_id & 1u) == 0u) {
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return h2_conn_fail(conn, "HTTP/2: expected request HEADERS");
            }
            {
                rt_http2_header_t *decoded = NULL;
                int end_stream = 0;
                int saw_regular = 0;
                if (!h2_decode_header_list(
                        conn, &frame, &decoded, &end_stream, "HTTP/2: invalid request headers")) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return 0;
                }
                active_stream = frame.stream_id;
                out_req->stream_id = (int)active_stream;
                for (rt_http2_header_t *it = decoded; it; it = it->next) {
                    if (it->name[0] == ':') {
                        if (saw_regular) {
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&body);
                            rt_http2_request_free(out_req);
                            return h2_conn_fail(conn, "HTTP/2: pseudo-header after regular header");
                        }
                        if (strcmp(it->name, ":method") == 0 && !out_req->method)
                            out_req->method = strdup(it->value);
                        else if (strcmp(it->name, ":scheme") == 0 && !out_req->scheme)
                            out_req->scheme = strdup(it->value);
                        else if (strcmp(it->name, ":authority") == 0 && !out_req->authority)
                            out_req->authority = strdup(it->value);
                        else if (strcmp(it->name, ":path") == 0 && !out_req->path)
                            out_req->path = strdup(it->value);
                        else {
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&body);
                            rt_http2_request_free(out_req);
                            return h2_conn_fail(conn, "HTTP/2: unsupported request pseudo-header");
                        }
                    } else {
                        saw_regular = 1;
                        if (h2_header_is_connection_specific(it->name, it->value) ||
                            !rt_http2_header_append_copy(&out_req->headers, it->name, it->value)) {
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&body);
                            rt_http2_request_free(out_req);
                            return h2_conn_fail(conn, "HTTP/2: invalid request header");
                        }
                    }
                }
                rt_http2_headers_free(decoded);
                if (!out_req->method || !out_req->scheme || !out_req->path) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return h2_conn_fail(conn, "HTTP/2: incomplete request pseudo-headers");
                }
                if (out_req->authority && !rt_http2_header_get(out_req->headers, "host") &&
                    !rt_http2_header_append_copy(&out_req->headers, "host", out_req->authority)) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return h2_conn_fail(conn, "HTTP/2: host header allocation failed");
                }
                if (end_stream) {
                    out_req->body = body.data;
                    out_req->body_len = body.len;
                    h2_frame_free(&frame);
                    return 1;
                }
            }
        } else {
            if (!h2_handle_common_frame(conn, &frame, active_stream, NULL, &handled)) {
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return 0;
            }
            if (handled) {
                h2_frame_free(&frame);
                continue;
            }
            if (frame.stream_id != active_stream) {
                if (frame.stream_id != 0 && (frame.stream_id & 1u) != 0u &&
                    (frame.type == H2_FRAME_HEADERS || frame.type == H2_FRAME_DATA)) {
                    if (!h2_refuse_concurrent_request_stream(conn, &frame)) {
                        h2_frame_free(&frame);
                        h2_buf_free(&body);
                        rt_http2_request_free(out_req);
                        return 0;
                    }
                    h2_frame_free(&frame);
                    continue;
                }
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return h2_conn_fail(conn, "HTTP/2: unexpected frame on unrelated request stream");
            }
            if (frame.type == H2_FRAME_HEADERS) {
                rt_http2_header_t *decoded = NULL;
                int end_stream = 0;
                if (!h2_decode_header_list(
                        conn, &frame, &decoded, &end_stream, "HTTP/2: invalid request trailers")) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return 0;
                }
                if (!h2_append_trailer_headers(&out_req->headers, decoded)) {
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return h2_conn_fail(conn, "HTTP/2: invalid request trailers");
                }
                rt_http2_headers_free(decoded);
                if (!end_stream) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return h2_conn_fail(conn, "HTTP/2: request trailers missing END_STREAM");
                }
                out_req->body = body.data;
                out_req->body_len = body.len;
                h2_frame_free(&frame);
                return 1;
            } else if (frame.type == H2_FRAME_DATA) {
                const uint8_t *data_ptr = NULL;
                size_t data_len = 0;
                int end_stream = 0;
                if (!h2_parse_data_payload(&frame, &data_ptr, &data_len, &end_stream) ||
                    !h2_append_body(&body, max_body_len, data_ptr, data_len) ||
                    (data_len > 0 &&
                     (!h2_send_window_update(conn, 0, (uint32_t)data_len) ||
                      !h2_send_window_update(conn, active_stream, (uint32_t)data_len)))) {
                    h2_frame_free(&frame);
                    h2_buf_free(&body);
                    rt_http2_request_free(out_req);
                    return h2_conn_fail(conn, "HTTP/2: invalid request body");
                }
                if (end_stream) {
                    out_req->body = body.data;
                    out_req->body_len = body.len;
                    h2_frame_free(&frame);
                    return 1;
                }
            } else {
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return h2_conn_fail(conn, "HTTP/2: unexpected frame in request body");
            }
        }
        h2_frame_free(&frame);
    }
}

int rt_http2_server_send_response(rt_http2_conn_t *conn,
                                  int stream_id,
                                  int status,
                                  const rt_http2_header_t *headers,
                                  const uint8_t *body,
                                  size_t body_len) {
    h2_buf_t block = {0};
    if (!conn || stream_id <= 0)
        return 0;
    if (!h2_build_response_block(conn, status, headers, &block))
        return h2_conn_fail(conn, "HTTP/2: failed to encode response headers");
    if (!h2_send_headers_block(conn, (uint32_t)stream_id, block.data, block.len, body_len == 0) ||
        (body_len > 0 && !h2_send_data(conn, (uint32_t)stream_id, body, body_len))) {
        h2_buf_free(&block);
        return 0;
    }
    h2_buf_free(&block);
    return 1;
}
