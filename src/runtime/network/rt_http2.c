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
#include <strings.h>
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
#define H2_DEFAULT_FRAME_SIZE 16384u
#define H2_MAX_FRAME_SIZE 16777215u
#define H2_MAX_HEADER_BLOCK (256u * 1024u)
#define H2_MAX_DYNAMIC_TABLE_SIZE (64u * 1024u)
#define H2_MAX_HUFF_NODES 8192

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} h2_buf_t;

typedef struct {
    char *name;
    char *value;
    size_t size;
} hpack_dyn_entry_t;

typedef struct {
    hpack_dyn_entry_t *entries;
    size_t count;
    size_t cap;
    size_t bytes;
    size_t max_bytes;
} hpack_dyn_table_t;

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
    int64_t peer_conn_window;
    int next_stream_id;
    int sent_goaway;
    hpack_dyn_table_t decode_table;
};

static const char kClientPreface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

static const struct {
    const char *name;
    const char *value;
} kHpackStaticTable[61] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

static const struct {
    uint32_t nbits;
    uint32_t code;
} kHpackHuffSym[257] = {
    {13u, 0xffc00000u},
    {23u, 0xffffb000u},
    {28u, 0xfffffe20u},
    {28u, 0xfffffe30u},
    {28u, 0xfffffe40u},
    {28u, 0xfffffe50u},
    {28u, 0xfffffe60u},
    {28u, 0xfffffe70u},
    {28u, 0xfffffe80u},
    {24u, 0xffffea00u},
    {30u, 0xfffffff0u},
    {28u, 0xfffffe90u},
    {28u, 0xfffffea0u},
    {30u, 0xfffffff4u},
    {28u, 0xfffffeb0u},
    {28u, 0xfffffec0u},
    {28u, 0xfffffed0u},
    {28u, 0xfffffee0u},
    {28u, 0xfffffef0u},
    {28u, 0xffffff00u},
    {28u, 0xffffff10u},
    {28u, 0xffffff20u},
    {30u, 0xfffffff8u},
    {28u, 0xffffff30u},
    {28u, 0xffffff40u},
    {28u, 0xffffff50u},
    {28u, 0xffffff60u},
    {28u, 0xffffff70u},
    {28u, 0xffffff80u},
    {28u, 0xffffff90u},
    {28u, 0xffffffa0u},
    {28u, 0xffffffb0u},
    {6u, 0x50000000u},
    {10u, 0xfe000000u},
    {10u, 0xfe400000u},
    {12u, 0xffa00000u},
    {13u, 0xffc80000u},
    {6u, 0x54000000u},
    {8u, 0xf8000000u},
    {11u, 0xff400000u},
    {10u, 0xfe800000u},
    {10u, 0xfec00000u},
    {8u, 0xf9000000u},
    {11u, 0xff600000u},
    {8u, 0xfa000000u},
    {6u, 0x58000000u},
    {6u, 0x5c000000u},
    {6u, 0x60000000u},
    {5u, 0x00000000u},
    {5u, 0x08000000u},
    {5u, 0x10000000u},
    {6u, 0x64000000u},
    {6u, 0x68000000u},
    {6u, 0x6c000000u},
    {6u, 0x70000000u},
    {6u, 0x74000000u},
    {6u, 0x78000000u},
    {6u, 0x7c000000u},
    {7u, 0xb8000000u},
    {8u, 0xfb000000u},
    {15u, 0xfff80000u},
    {6u, 0x80000000u},
    {12u, 0xffb00000u},
    {10u, 0xff000000u},
    {13u, 0xffd00000u},
    {6u, 0x84000000u},
    {7u, 0xba000000u},
    {7u, 0xbc000000u},
    {7u, 0xbe000000u},
    {7u, 0xc0000000u},
    {7u, 0xc2000000u},
    {7u, 0xc4000000u},
    {7u, 0xc6000000u},
    {7u, 0xc8000000u},
    {7u, 0xca000000u},
    {7u, 0xcc000000u},
    {7u, 0xce000000u},
    {7u, 0xd0000000u},
    {7u, 0xd2000000u},
    {7u, 0xd4000000u},
    {7u, 0xd6000000u},
    {7u, 0xd8000000u},
    {7u, 0xda000000u},
    {7u, 0xdc000000u},
    {7u, 0xde000000u},
    {7u, 0xe0000000u},
    {7u, 0xe2000000u},
    {7u, 0xe4000000u},
    {8u, 0xfc000000u},
    {7u, 0xe6000000u},
    {8u, 0xfd000000u},
    {13u, 0xffd80000u},
    {19u, 0xfffe0000u},
    {13u, 0xffe00000u},
    {14u, 0xfff00000u},
    {6u, 0x88000000u},
    {15u, 0xfffa0000u},
    {5u, 0x18000000u},
    {6u, 0x8c000000u},
    {5u, 0x20000000u},
    {6u, 0x90000000u},
    {5u, 0x28000000u},
    {6u, 0x94000000u},
    {6u, 0x98000000u},
    {6u, 0x9c000000u},
    {5u, 0x30000000u},
    {7u, 0xe8000000u},
    {7u, 0xea000000u},
    {6u, 0xa0000000u},
    {6u, 0xa4000000u},
    {6u, 0xa8000000u},
    {5u, 0x38000000u},
    {6u, 0xac000000u},
    {7u, 0xec000000u},
    {6u, 0xb0000000u},
    {5u, 0x40000000u},
    {5u, 0x48000000u},
    {6u, 0xb4000000u},
    {7u, 0xee000000u},
    {7u, 0xf0000000u},
    {7u, 0xf2000000u},
    {7u, 0xf4000000u},
    {7u, 0xf6000000u},
    {15u, 0xfffc0000u},
    {11u, 0xff800000u},
    {14u, 0xfff40000u},
    {13u, 0xffe80000u},
    {28u, 0xffffffc0u},
    {20u, 0xfffe6000u},
    {22u, 0xffff4800u},
    {20u, 0xfffe7000u},
    {20u, 0xfffe8000u},
    {22u, 0xffff4c00u},
    {22u, 0xffff5000u},
    {22u, 0xffff5400u},
    {23u, 0xffffb200u},
    {22u, 0xffff5800u},
    {23u, 0xffffb400u},
    {23u, 0xffffb600u},
    {23u, 0xffffb800u},
    {23u, 0xffffba00u},
    {23u, 0xffffbc00u},
    {24u, 0xffffeb00u},
    {23u, 0xffffbe00u},
    {24u, 0xffffec00u},
    {24u, 0xffffed00u},
    {22u, 0xffff5c00u},
    {23u, 0xffffc000u},
    {24u, 0xffffee00u},
    {23u, 0xffffc200u},
    {23u, 0xffffc400u},
    {23u, 0xffffc600u},
    {23u, 0xffffc800u},
    {21u, 0xfffee000u},
    {22u, 0xffff6000u},
    {23u, 0xffffca00u},
    {22u, 0xffff6400u},
    {23u, 0xffffcc00u},
    {23u, 0xffffce00u},
    {24u, 0xffffef00u},
    {22u, 0xffff6800u},
    {21u, 0xfffee800u},
    {20u, 0xfffe9000u},
    {22u, 0xffff6c00u},
    {22u, 0xffff7000u},
    {23u, 0xffffd000u},
    {23u, 0xffffd200u},
    {21u, 0xfffef000u},
    {23u, 0xffffd400u},
    {22u, 0xffff7400u},
    {22u, 0xffff7800u},
    {24u, 0xfffff000u},
    {21u, 0xfffef800u},
    {22u, 0xffff7c00u},
    {23u, 0xffffd600u},
    {23u, 0xffffd800u},
    {21u, 0xffff0000u},
    {21u, 0xffff0800u},
    {22u, 0xffff8000u},
    {21u, 0xffff1000u},
    {23u, 0xffffda00u},
    {22u, 0xffff8400u},
    {23u, 0xffffdc00u},
    {23u, 0xffffde00u},
    {20u, 0xfffea000u},
    {22u, 0xffff8800u},
    {22u, 0xffff8c00u},
    {22u, 0xffff9000u},
    {23u, 0xffffe000u},
    {22u, 0xffff9400u},
    {22u, 0xffff9800u},
    {23u, 0xffffe200u},
    {26u, 0xfffff800u},
    {26u, 0xfffff840u},
    {20u, 0xfffeb000u},
    {19u, 0xfffe2000u},
    {22u, 0xffff9c00u},
    {23u, 0xffffe400u},
    {22u, 0xffffa000u},
    {25u, 0xfffff600u},
    {26u, 0xfffff880u},
    {26u, 0xfffff8c0u},
    {26u, 0xfffff900u},
    {27u, 0xfffffbc0u},
    {27u, 0xfffffbe0u},
    {26u, 0xfffff940u},
    {24u, 0xfffff100u},
    {25u, 0xfffff680u},
    {19u, 0xfffe4000u},
    {21u, 0xffff1800u},
    {26u, 0xfffff980u},
    {27u, 0xfffffc00u},
    {27u, 0xfffffc20u},
    {26u, 0xfffff9c0u},
    {27u, 0xfffffc40u},
    {24u, 0xfffff200u},
    {21u, 0xffff2000u},
    {21u, 0xffff2800u},
    {26u, 0xfffffa00u},
    {26u, 0xfffffa40u},
    {28u, 0xffffffd0u},
    {27u, 0xfffffc60u},
    {27u, 0xfffffc80u},
    {27u, 0xfffffca0u},
    {20u, 0xfffec000u},
    {24u, 0xfffff300u},
    {20u, 0xfffed000u},
    {21u, 0xffff3000u},
    {22u, 0xffffa400u},
    {21u, 0xffff3800u},
    {21u, 0xffff4000u},
    {23u, 0xffffe600u},
    {22u, 0xffffa800u},
    {22u, 0xffffac00u},
    {25u, 0xfffff700u},
    {25u, 0xfffff780u},
    {24u, 0xfffff400u},
    {24u, 0xfffff500u},
    {26u, 0xfffffa80u},
    {23u, 0xffffe800u},
    {26u, 0xfffffac0u},
    {27u, 0xfffffcc0u},
    {26u, 0xfffffb00u},
    {26u, 0xfffffb40u},
    {27u, 0xfffffce0u},
    {27u, 0xfffffd00u},
    {27u, 0xfffffd20u},
    {27u, 0xfffffd40u},
    {27u, 0xfffffd60u},
    {28u, 0xffffffe0u},
    {27u, 0xfffffd80u},
    {27u, 0xfffffda0u},
    {27u, 0xfffffdc0u},
    {27u, 0xfffffde0u},
    {27u, 0xfffffe00u},
    {26u, 0xfffffb80u},
    {30u, 0xfffffffcu},
};

typedef struct {
    int child[2];
    int symbol;
    int pad_ok;
} huff_node_t;

static huff_node_t g_huff_nodes[H2_MAX_HUFF_NODES];
static int g_huff_node_count = 0;
static int g_huff_initialized = 0;

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

static int h2_buf_append(h2_buf_t *buf, const void *src, size_t len) {
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

static int h2_buf_append_byte(h2_buf_t *buf, uint8_t b) {
    return h2_buf_append(buf, &b, 1);
}

static void h2_buf_free(h2_buf_t *buf) {
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int h2_header_name_is_valid(const char *name) {
    if (!name || !*name)
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p <= 0x20 || *p == 0x7f)
            return 0;
        if (*p >= 'A' && *p <= 'Z')
            return 0;
    }
    return 1;
}

static int h2_header_value_is_valid(const char *value) {
    if (!value)
        return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '\r' || *p == '\n' || *p == '\0')
            return 0;
    }
    return 1;
}

static char *h2_strdup_range(const uint8_t *src, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    if (len > 0)
        memcpy(out, src, len);
    out[len] = '\0';
    return out;
}

static char *h2_strdup_lower(const char *src) {
    size_t len = src ? strlen(src) : 0;
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

static void hpack_dyn_table_free(hpack_dyn_table_t *table) {
    if (!table)
        return;
    for (size_t i = 0; i < table->count; i++) {
        free(table->entries[i].name);
        free(table->entries[i].value);
    }
    free(table->entries);
    memset(table, 0, sizeof(*table));
}

static void hpack_dyn_table_evict_to_fit(hpack_dyn_table_t *table) {
    while (table->bytes > table->max_bytes && table->count > 0) {
        hpack_dyn_entry_t *entry = &table->entries[table->count - 1];
        table->bytes -= entry->size;
        free(entry->name);
        free(entry->value);
        table->count--;
    }
}

static int hpack_dyn_table_set_max_size(hpack_dyn_table_t *table, size_t max_bytes) {
    if (!table || max_bytes > H2_MAX_DYNAMIC_TABLE_SIZE)
        return 0;
    table->max_bytes = max_bytes;
    hpack_dyn_table_evict_to_fit(table);
    return 1;
}

static int hpack_dyn_table_add(hpack_dyn_table_t *table, const char *name, const char *value) {
    char *name_copy = NULL;
    char *value_copy = NULL;
    size_t name_len = 0;
    size_t value_len = 0;
    size_t entry_size = 0;
    if (!table || !name || !value)
        return 0;
    name_len = strlen(name);
    value_len = strlen(value);
    if (name_len > SIZE_MAX - value_len - 32)
        return 0;
    entry_size = name_len + value_len + 32;
    if (entry_size > table->max_bytes) {
        hpack_dyn_table_set_max_size(table, table->max_bytes);
        for (size_t i = 0; i < table->count; i++) {
            free(table->entries[i].name);
            free(table->entries[i].value);
        }
        table->count = 0;
        table->bytes = 0;
        return 1;
    }
    while (table->bytes + entry_size > table->max_bytes && table->count > 0) {
        hpack_dyn_entry_t *entry = &table->entries[table->count - 1];
        table->bytes -= entry->size;
        free(entry->name);
        free(entry->value);
        table->count--;
    }
    if (table->count == table->cap) {
        size_t new_cap = table->cap ? table->cap * 2 : 8;
        hpack_dyn_entry_t *grown =
            (hpack_dyn_entry_t *)realloc(table->entries, new_cap * sizeof(*grown));
        if (!grown)
            return 0;
        table->entries = grown;
        table->cap = new_cap;
    }
    name_copy = strdup(name);
    value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return 0;
    }
    memmove(table->entries + 1, table->entries, table->count * sizeof(*table->entries));
    table->entries[0].name = name_copy;
    table->entries[0].value = value_copy;
    table->entries[0].size = entry_size;
    table->count++;
    table->bytes += entry_size;
    return 1;
}

static int hpack_lookup_exact_static(const char *name, const char *value) {
    for (int i = 0; i < 61; i++) {
        if (strcmp(kHpackStaticTable[i].name, name) == 0 &&
            strcmp(kHpackStaticTable[i].value, value) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static int hpack_lookup_name_static(const char *name) {
    for (int i = 0; i < 61; i++) {
        if (strcmp(kHpackStaticTable[i].name, name) == 0)
            return i + 1;
    }
    return 0;
}

static int hpack_lookup_exact_dynamic(const hpack_dyn_table_t *table,
                                      const char *name,
                                      const char *value) {
    if (!table)
        return 0;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0 &&
            strcmp(table->entries[i].value, value) == 0) {
            return 62 + (int)i;
        }
    }
    return 0;
}

static int hpack_lookup_name_dynamic(const hpack_dyn_table_t *table, const char *name) {
    if (!table)
        return 0;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0)
            return 62 + (int)i;
    }
    return 0;
}

static int hpack_get_by_index(const hpack_dyn_table_t *table,
                              size_t index,
                              const char **name_out,
                              const char **value_out) {
    if (index == 0)
        return 0;
    if (index <= 61) {
        if (name_out)
            *name_out = kHpackStaticTable[index - 1].name;
        if (value_out)
            *value_out = kHpackStaticTable[index - 1].value;
        return 1;
    }
    index -= 62;
    if (!table || index >= table->count)
        return 0;
    if (name_out)
        *name_out = table->entries[index].name;
    if (value_out)
        *value_out = table->entries[index].value;
    return 1;
}

static int hpack_int_decode(const uint8_t *src,
                            size_t src_len,
                            uint8_t prefix_bits,
                            size_t *value_out,
                            size_t *consumed_out) {
    size_t value = 0;
    size_t consumed = 0;
    size_t shift = 0;
    uint8_t mask = 0;
    if (!src || src_len == 0 || prefix_bits == 0 || prefix_bits > 8)
        return 0;
    mask = (uint8_t)((1u << prefix_bits) - 1u);
    value = (size_t)(src[0] & mask);
    consumed = 1;
    if (value < mask) {
        if (value_out)
            *value_out = value;
        if (consumed_out)
            *consumed_out = consumed;
        return 1;
    }
    while (consumed < src_len) {
        uint8_t b = src[consumed++];
        if (shift > sizeof(size_t) * 8 - 7)
            return 0;
        value += (size_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            if (value_out)
                *value_out = value;
            if (consumed_out)
                *consumed_out = consumed;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int hpack_int_encode(h2_buf_t *dst, uint8_t prefix_bits, uint8_t prefix_high, size_t value) {
    uint8_t first = prefix_high;
    size_t max_prefix = ((size_t)1u << prefix_bits) - 1u;
    if (!dst || prefix_bits == 0 || prefix_bits > 8)
        return 0;
    if (value < max_prefix) {
        first = (uint8_t)(prefix_high | (uint8_t)value);
        return h2_buf_append_byte(dst, first);
    }
    first = (uint8_t)(prefix_high | (uint8_t)max_prefix);
    if (!h2_buf_append_byte(dst, first))
        return 0;
    value -= max_prefix;
    while (value >= 128) {
        uint8_t b = (uint8_t)((value & 0x7f) | 0x80);
        if (!h2_buf_append_byte(dst, b))
            return 0;
        value >>= 7;
    }
    return h2_buf_append_byte(dst, (uint8_t)value);
}

static int huff_new_node(void) {
    int index = g_huff_node_count;
    if (index >= H2_MAX_HUFF_NODES)
        return -1;
    g_huff_nodes[index].child[0] = -1;
    g_huff_nodes[index].child[1] = -1;
    g_huff_nodes[index].symbol = -1;
    g_huff_nodes[index].pad_ok = 0;
    g_huff_node_count++;
    return index;
}

static int hpack_huffman_init(void) {
    if (g_huff_initialized)
        return 1;
    g_huff_node_count = 0;
    if (huff_new_node() != 0)
        return 0;
    for (int sym = 0; sym < 257; sym++) {
        int node = 0;
        uint32_t code = kHpackHuffSym[sym].code;
        uint32_t nbits = kHpackHuffSym[sym].nbits;
        for (uint32_t i = 0; i < nbits; i++) {
            int bit = (int)((code >> (31u - i)) & 1u);
            if (g_huff_nodes[node].child[bit] < 0) {
                int next = huff_new_node();
                if (next < 0)
                    return 0;
                g_huff_nodes[node].child[bit] = next;
            }
            node = g_huff_nodes[node].child[bit];
        }
        g_huff_nodes[node].symbol = sym;
    }
    {
        int node = 0;
        g_huff_nodes[node].pad_ok = 1;
        for (int i = 0; i < 7; i++) {
            node = g_huff_nodes[node].child[1];
            if (node < 0)
                return 0;
            g_huff_nodes[node].pad_ok = 1;
        }
    }
    g_huff_initialized = 1;
    return 1;
}

static char *hpack_decode_huffman(const uint8_t *src, size_t src_len) {
    h2_buf_t out = {0};
    int node = 0;
    if (!hpack_huffman_init())
        return NULL;
    for (size_t i = 0; i < src_len; i++) {
        uint8_t byte = src[i];
        for (int bit = 7; bit >= 0; bit--) {
            int one = (byte >> bit) & 1;
            node = g_huff_nodes[node].child[one];
            if (node < 0) {
                h2_buf_free(&out);
                return NULL;
            }
            if (g_huff_nodes[node].symbol >= 0) {
                int sym = g_huff_nodes[node].symbol;
                if (sym == 256) {
                    h2_buf_free(&out);
                    return NULL;
                }
                if (!h2_buf_append_byte(&out, (uint8_t)sym)) {
                    h2_buf_free(&out);
                    return NULL;
                }
                node = 0;
            }
        }
    }
    if (!g_huff_nodes[node].pad_ok) {
        h2_buf_free(&out);
        return NULL;
    }
    if (!h2_buf_append_byte(&out, '\0')) {
        h2_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}

static int hpack_decode_string(const uint8_t *src,
                               size_t src_len,
                               char **out,
                               size_t *consumed_out) {
    size_t str_len = 0;
    size_t int_consumed = 0;
    int huffman = 0;
    char *value = NULL;
    if (!src || src_len == 0 || !out)
        return 0;
    huffman = (src[0] & 0x80) != 0;
    if (!hpack_int_decode(src, src_len, 7, &str_len, &int_consumed))
        return 0;
    if (int_consumed > src_len || str_len > src_len - int_consumed)
        return 0;
    if (huffman) {
        value = hpack_decode_huffman(src + int_consumed, str_len);
    } else {
        value = h2_strdup_range(src + int_consumed, str_len);
    }
    if (!value)
        return 0;
    *out = value;
    if (consumed_out)
        *consumed_out = int_consumed + str_len;
    return 1;
}

static int hpack_encode_string(h2_buf_t *dst, const char *text) {
    size_t len = text ? strlen(text) : 0;
    if (!text)
        return 0;
    if (!hpack_int_encode(dst, 7, 0x00, len))
        return 0;
    return h2_buf_append(dst, text, len);
}

static int hpack_encode_header_field(h2_buf_t *dst,
                                     const hpack_dyn_table_t *table,
                                     const char *name,
                                     const char *value) {
    int exact_index = 0;
    int name_index = 0;
    char *lower_name = NULL;
    const char *emit_name = name;

    if (!dst || !name || !value)
        return 0;
    if (name[0] != ':') {
        lower_name = h2_strdup_lower(name);
        if (!lower_name)
            return 0;
        emit_name = lower_name;
    }
    if (!h2_header_name_is_valid(emit_name) || !h2_header_value_is_valid(value)) {
        free(lower_name);
        return 0;
    }
    exact_index = hpack_lookup_exact_dynamic(table, emit_name, value);
    if (!exact_index)
        exact_index = hpack_lookup_exact_static(emit_name, value);
    if (exact_index) {
        free(lower_name);
        return hpack_int_encode(dst, 7, 0x80, (size_t)exact_index);
    }
    name_index = hpack_lookup_name_dynamic(table, emit_name);
    if (!name_index)
        name_index = hpack_lookup_name_static(emit_name);
    if (!hpack_int_encode(dst, 4, 0x00, (size_t)name_index)) {
        free(lower_name);
        return 0;
    }
    if (!name_index && !hpack_encode_string(dst, emit_name)) {
        free(lower_name);
        return 0;
    }
    free(lower_name);
    return hpack_encode_string(dst, value);
}

static int hpack_decode_header_block(hpack_dyn_table_t *table,
                                     const uint8_t *block,
                                     size_t block_len,
                                     rt_http2_header_t **headers_out) {
    size_t pos = 0;
    rt_http2_header_t *headers = NULL;
    if (!table || !headers_out)
        return 0;
    *headers_out = NULL;
    while (pos < block_len) {
        const char *name_ref = NULL;
        const char *value_ref = NULL;
        char *name = NULL;
        char *value = NULL;
        size_t index = 0;
        size_t consumed = 0;
        int add_index = 0;
        uint8_t b = block[pos];

        if ((b & 0x80) != 0) {
            if (!hpack_int_decode(block + pos, block_len - pos, 7, &index, &consumed) ||
                !hpack_get_by_index(table, index, &name_ref, &value_ref) ||
                !rt_http2_header_append_copy(&headers, name_ref, value_ref)) {
                rt_http2_headers_free(headers);
                return 0;
            }
            pos += consumed;
            continue;
        }

        if ((b & 0x20) != 0) {
            if (!hpack_int_decode(block + pos, block_len - pos, 5, &index, &consumed) ||
                !hpack_dyn_table_set_max_size(table, index)) {
                rt_http2_headers_free(headers);
                return 0;
            }
            pos += consumed;
            continue;
        }

        if ((b & 0x40) != 0) {
            add_index = 1;
            if (!hpack_int_decode(block + pos, block_len - pos, 6, &index, &consumed)) {
                rt_http2_headers_free(headers);
                return 0;
            }
        } else {
            add_index = 0;
            if (!hpack_int_decode(block + pos, block_len - pos, 4, &index, &consumed)) {
                rt_http2_headers_free(headers);
                return 0;
            }
        }
        pos += consumed;

        if (index > 0) {
            if (!hpack_get_by_index(table, index, &name_ref, NULL)) {
                rt_http2_headers_free(headers);
                return 0;
            }
            name = strdup(name_ref);
            if (!name) {
                rt_http2_headers_free(headers);
                return 0;
            }
        } else {
            size_t name_consumed = 0;
            if (!hpack_decode_string(block + pos, block_len - pos, &name, &name_consumed)) {
                rt_http2_headers_free(headers);
                return 0;
            }
            pos += name_consumed;
        }

        {
            size_t value_consumed = 0;
            if (!hpack_decode_string(block + pos, block_len - pos, &value, &value_consumed)) {
                free(name);
                rt_http2_headers_free(headers);
                return 0;
            }
            pos += value_consumed;
        }

        if (!h2_header_name_is_valid(name) || !h2_header_value_is_valid(value) ||
            !rt_http2_header_append_copy(&headers, name, value)) {
            free(name);
            free(value);
            rt_http2_headers_free(headers);
            return 0;
        }

        if (add_index && !hpack_dyn_table_add(table, name, value)) {
            free(name);
            free(value);
            rt_http2_headers_free(headers);
            return 0;
        }
        free(name);
        free(value);
    }
    *headers_out = headers;
    return 1;
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
    if (payload_len > H2_MAX_FRAME_SIZE)
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

static void h2_apply_setting(rt_http2_conn_t *conn,
                             uint16_t id,
                             uint32_t value,
                             int64_t *stream_window_io) {
    switch (id) {
        case H2_SETTINGS_HEADER_TABLE_SIZE:
            break;
        case H2_SETTINGS_ENABLE_PUSH:
            break;
        case H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            break;
        case H2_SETTINGS_INITIAL_WINDOW_SIZE: {
            int64_t delta = (int64_t)value - (int64_t)conn->peer_initial_window;
            conn->peer_initial_window = value;
            if (stream_window_io)
                *stream_window_io += delta;
            break;
        }
        case H2_SETTINGS_MAX_FRAME_SIZE:
            if (value >= H2_DEFAULT_FRAME_SIZE && value <= H2_MAX_FRAME_SIZE)
                conn->peer_max_frame_size = value;
            break;
        case H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            break;
        default:
            break;
    }
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
    if (!hpack_decode_header_block(&conn->decode_table, header_block.data, header_block.len, decoded_out)) {
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
                    h2_apply_setting(conn, id, value, stream_window_io);
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
                conn->peer_conn_window += increment;
            } else if (frame->stream_id == stream_id && stream_window_io) {
                *stream_window_io += increment;
            }
            if (handled_out)
                *handled_out = 1;
            return 1;
        }

        case H2_FRAME_PRIORITY:
            if (frame->payload_len != 5)
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
            if (frame->stream_id == 0 && handled_out)
                *handled_out = 1;
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
    if (!hpack_encode_header_field(out, &conn->decode_table, ":method", method) ||
        !hpack_encode_header_field(out, &conn->decode_table, ":scheme", scheme) ||
        !hpack_encode_header_field(out, &conn->decode_table, ":authority", authority) ||
        !hpack_encode_header_field(out, &conn->decode_table, ":path", path)) {
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
        if (!hpack_encode_header_field(out, &conn->decode_table, it->name, it->value)) {
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
    if (!hpack_encode_header_field(out, &conn->decode_table, ":status", status_buf)) {
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
        if (!hpack_encode_header_field(out, &conn->decode_table, it->name, it->value)) {
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
    conn->peer_conn_window = H2_DEFAULT_WINDOW_SIZE;
    conn->next_stream_id = 1;
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

static int h2_append_body(h2_buf_t *body,
                          size_t max_body_len,
                          const uint8_t *src,
                          size_t len) {
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
            if (!h2_decode_header_list(conn, &frame, &decoded, &end_stream, "HTTP/2: invalid response headers")) {
                h2_frame_free(&frame);
                h2_buf_free(&res_body);
                rt_http2_response_free(out_res);
                return 0;
            }
            if (!saw_response_headers) {
                int saw_status = 0;
                for (rt_http2_header_t *it = decoded; it; it = it->next) {
                    if (strcmp(it->name, ":status") == 0) {
                        if (saw_status) {
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&res_body);
                            rt_http2_response_free(out_res);
                            return h2_conn_fail(conn, "HTTP/2: duplicate response status");
                        }
                        out_res->status = atoi(it->value);
                        saw_status = 1;
                    } else if (it->name[0] != ':') {
                        if (h2_header_is_connection_specific(it->name, it->value) ||
                            !rt_http2_header_append_copy(&out_res->headers, it->name, it->value)) {
                            rt_http2_headers_free(decoded);
                            h2_frame_free(&frame);
                            h2_buf_free(&res_body);
                            rt_http2_response_free(out_res);
                            return h2_conn_fail(conn, "HTTP/2: invalid response header");
                        }
                    } else {
                        rt_http2_headers_free(decoded);
                        h2_frame_free(&frame);
                        h2_buf_free(&res_body);
                        rt_http2_response_free(out_res);
                        return h2_conn_fail(conn, "HTTP/2: invalid response pseudo-header");
                    }
                }
                if (!saw_status || out_res->status < 100 || out_res->status > 599) {
                    rt_http2_headers_free(decoded);
                    h2_frame_free(&frame);
                    h2_buf_free(&res_body);
                    rt_http2_response_free(out_res);
                    return h2_conn_fail(conn, "HTTP/2: missing response status");
                }
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
                !h2_send_window_update(conn, 0, (uint32_t)data_len) ||
                !h2_send_window_update(conn, stream_id, (uint32_t)data_len)) {
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

int rt_http2_server_receive_request(
    rt_http2_conn_t *conn, size_t max_body_len, rt_http2_request_t *out_req) {
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
            if (frame.type != H2_FRAME_HEADERS || frame.stream_id == 0 || (frame.stream_id & 1u) == 0u) {
                h2_frame_free(&frame);
                h2_buf_free(&body);
                rt_http2_request_free(out_req);
                return h2_conn_fail(conn, "HTTP/2: expected request HEADERS");
            }
            {
                rt_http2_header_t *decoded = NULL;
                int end_stream = 0;
                int saw_regular = 0;
                if (!h2_decode_header_list(conn, &frame, &decoded, &end_stream, "HTTP/2: invalid request headers")) {
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
                if (frame.stream_id != 0 &&
                    (frame.stream_id & 1u) != 0u &&
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
                if (!h2_decode_header_list(conn, &frame, &decoded, &end_stream, "HTTP/2: invalid request trailers")) {
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
                    !h2_send_window_update(conn, 0, (uint32_t)data_len) ||
                    !h2_send_window_update(conn, active_stream, (uint32_t)data_len)) {
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
