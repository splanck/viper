//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http2.h
// Purpose: Internal HTTP/2 + HPACK transport used by the HTTPS runtime.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long (*rt_http2_read_fn)(void *ctx, uint8_t *buf, size_t len);
typedef int (*rt_http2_write_fn)(void *ctx, const uint8_t *buf, size_t len);

typedef struct rt_http2_io {
    void *ctx;
    rt_http2_read_fn read;
    rt_http2_write_fn write;
} rt_http2_io_t;

typedef struct rt_http2_header {
    char *name;
    char *value;
    struct rt_http2_header *next;
} rt_http2_header_t;

typedef struct rt_http2_conn rt_http2_conn_t;

typedef struct rt_http2_request {
    int stream_id;
    char *method;
    char *scheme;
    char *authority;
    char *path;
    rt_http2_header_t *headers;
    uint8_t *body;
    size_t body_len;
} rt_http2_request_t;

typedef struct rt_http2_response {
    int stream_id;
    int status;
    rt_http2_header_t *headers;
    uint8_t *body;
    size_t body_len;
} rt_http2_response_t;

rt_http2_conn_t *rt_http2_client_new(const rt_http2_io_t *io);
rt_http2_conn_t *rt_http2_server_new(const rt_http2_io_t *io);
void rt_http2_conn_free(rt_http2_conn_t *conn);
const char *rt_http2_get_error(const rt_http2_conn_t *conn);
int rt_http2_conn_is_usable(const rt_http2_conn_t *conn);

int rt_http2_client_roundtrip(rt_http2_conn_t *conn,
                              const char *method,
                              const char *scheme,
                              const char *authority,
                              const char *path,
                              const rt_http2_header_t *headers,
                              const uint8_t *body,
                              size_t body_len,
                              size_t max_body_len,
                              rt_http2_response_t *out_res);

int rt_http2_server_receive_request(
    rt_http2_conn_t *conn, size_t max_body_len, rt_http2_request_t *out_req);
int rt_http2_server_send_response(rt_http2_conn_t *conn,
                                  int stream_id,
                                  int status,
                                  const rt_http2_header_t *headers,
                                  const uint8_t *body,
                                  size_t body_len);

void rt_http2_headers_free(rt_http2_header_t *headers);
void rt_http2_request_free(rt_http2_request_t *req);
void rt_http2_response_free(rt_http2_response_t *res);
int rt_http2_header_append_copy(rt_http2_header_t **list, const char *name, const char *value);
const char *rt_http2_header_get(const rt_http2_header_t *list, const char *name);

#ifdef __cplusplus
}
#endif
