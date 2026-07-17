//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_http2_internal.h
// Purpose: Shared HTTP/2 buffer + HPACK dynamic-table types and the helper/
//   HPACK API boundary between the frame layer (rt_http2.c) and HPACK
//   header (de)compression (rt_hpack.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>
#include <stddef.h>

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

int h2_buf_append(h2_buf_t *buf, const void *src, size_t len);
int h2_buf_append_byte(h2_buf_t *buf, uint8_t b);
void h2_buf_free(h2_buf_t *buf);
int h2_header_name_bytes_is_valid(const char *name, size_t len);
int h2_header_name_is_valid(const char *name);
int h2_header_value_bytes_is_valid(const char *value, size_t len);
int h2_header_value_is_valid(const char *value);
char *h2_strdup_lower(const char *src);
char *h2_strdup_range(const uint8_t *src, size_t len);
int hpack_decode_header_block(hpack_dyn_table_t *table, const uint8_t *block, size_t block_len, rt_http2_header_t **headers_out);
void hpack_dyn_table_free(hpack_dyn_table_t *table);
int hpack_dyn_table_set_max_size(hpack_dyn_table_t *table, size_t max_bytes);
int hpack_encode_header_field(h2_buf_t *dst, const hpack_dyn_table_t *table, const char *name, const char *value);
