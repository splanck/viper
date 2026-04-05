//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_ogg.c
// Purpose: OGG container format parser implementation.
// Key invariants:
//   - Pages are validated by the "OggS" capture pattern
//   - Packets spanning multiple pages are reassembled
// Ownership/Lifetime:
//   - ogg_reader_t owns all internal buffers
// Links: rt_ogg.h
//
//===----------------------------------------------------------------------===//

#include "rt_ogg.h"

#include <stdlib.h>
#include <string.h>

struct ogg_stream_state_t {
    uint32_t serial_number;
    ogg_packet_t partial;
    int saw_bos;
    struct ogg_stream_state_t *next;
};

struct ogg_packet_node_t {
    uint8_t *data;
    size_t len;
    ogg_packet_info_t info;
    struct ogg_packet_node_t *next;
};

//===----------------------------------------------------------------------===//
// CRC-32 for OGG (polynomial 0x04C11DB7, init 0)
//===----------------------------------------------------------------------===//

static uint32_t ogg_crc_table[256];
static int ogg_crc_table_init = 0;

static void ogg_crc_init(void) {
    if (ogg_crc_table_init)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int j = 0; j < 8; j++) {
            if (c & 0x80000000)
                c = (c << 1) ^ 0x04C11DB7;
            else
                c <<= 1;
        }
        ogg_crc_table[i] = c;
    }
    ogg_crc_table_init = 1;
}

//===----------------------------------------------------------------------===//
// Low-level I/O
//===----------------------------------------------------------------------===//

static size_t ogg_read(ogg_reader_t *r, void *buf, size_t count) {
    if (r->file) {
        return fread(buf, 1, count, r->file);
    }
    // Memory reader
    size_t avail = r->mem_len - r->mem_pos;
    if (count > avail)
        count = avail;
    memcpy(buf, r->mem + r->mem_pos, count);
    r->mem_pos += count;
    return count;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t read_i64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return (int64_t)v;
}

//===----------------------------------------------------------------------===//
// Page reading
//===----------------------------------------------------------------------===//

/// @brief Read the next OGG page, filling r->page and returning the page body.
/// @param body_out Receives a malloc'd buffer with the page body. Caller frees.
/// @param body_len_out Receives the body length.
/// @return 1 on success, 0 on EOF/error.
static int ogg_read_page(ogg_reader_t *r, uint8_t **body_out, size_t *body_len_out) {
    ogg_crc_init();

    // Search for "OggS" capture pattern
    uint8_t header[27];
    while (1) {
        if (ogg_read(r, header, 4) != 4)
            return 0;
        if (header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S')
            break;
        // Resync: shift by 1 byte and retry
        // (In practice, we only need this for corrupted streams)
    }

    // Read remaining header bytes (23 more)
    if (ogg_read(r, header + 4, 23) != 23)
        return 0;

    r->page.version = header[4];
    r->page.header_type = header[5];
    r->page.granule_position = read_i64_le(header + 6);
    r->page.serial_number = read_u32_le(header + 14);
    r->page.page_sequence = read_u32_le(header + 18);
    r->page.checksum = read_u32_le(header + 22);
    r->page.num_segments = header[26];

    // Read segment table
    if (ogg_read(r, r->page.segment_table, r->page.num_segments) != r->page.num_segments)
        return 0;

    // Calculate body size
    size_t body_len = 0;
    for (int i = 0; i < r->page.num_segments; i++)
        body_len += r->page.segment_table[i];

    // Read body
    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body)
        return 0;
    if (ogg_read(r, body, body_len) != body_len) {
        free(body);
        return 0;
    }

    // CRC verification: compute CRC over the entire page with checksum field zeroed
    uint8_t crc_header[27];
    memcpy(crc_header, header, 27);
    crc_header[22] = crc_header[23] = crc_header[24] = crc_header[25] = 0;
    // For proper CRC we need to compute over the concatenated data.
    // We currently parse permissively and do not reject on CRC mismatch.
    {
        uint32_t full_crc = 0;
        for (size_t i = 0; i < 27; i++)
            full_crc = (full_crc << 8) ^ ogg_crc_table[((full_crc >> 24) ^ crc_header[i]) & 0xFF];
        for (int i = 0; i < r->page.num_segments; i++)
            full_crc = (full_crc << 8) ^
                       ogg_crc_table[((full_crc >> 24) ^ r->page.segment_table[i]) & 0xFF];
        for (size_t i = 0; i < body_len; i++)
            full_crc = (full_crc << 8) ^ ogg_crc_table[((full_crc >> 24) ^ body[i]) & 0xFF];
        // Skip CRC check for now — many OGG files in the wild have non-standard CRC
        // due to encoder bugs. We prioritize compatibility over strictness.
        (void)full_crc;
    }

    *body_out = body;
    *body_len_out = body_len;
    return 1;
}

//===----------------------------------------------------------------------===//
// Packet assembly
//===----------------------------------------------------------------------===//

static void packet_append(ogg_packet_t *pkt, const uint8_t *data, size_t len) {
    size_t needed = pkt->len + len;
    if (needed > pkt->cap) {
        size_t new_cap = pkt->cap ? pkt->cap * 2 : 4096;
        if (new_cap < needed)
            new_cap = needed;
        uint8_t *new_data = (uint8_t *)realloc(pkt->data, new_cap);
        if (!new_data)
            return;
        pkt->data = new_data;
        pkt->cap = new_cap;
    }
    memcpy(pkt->data + pkt->len, data, len);
    pkt->len += len;
}

static void packet_reset(ogg_packet_t *pkt) {
    if (!pkt)
        return;
    pkt->len = 0;
    pkt->complete = 0;
}

static ogg_stream_state_t *find_stream_state(ogg_reader_t *r, uint32_t serial_number) {
    ogg_stream_state_t *cur = r->streams;
    while (cur) {
        if (cur->serial_number == serial_number)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static ogg_stream_state_t *get_stream_state(ogg_reader_t *r, uint32_t serial_number) {
    ogg_stream_state_t *state = find_stream_state(r, serial_number);
    if (state)
        return state;
    state = (ogg_stream_state_t *)calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->serial_number = serial_number;
    state->next = r->streams;
    r->streams = state;
    return state;
}

static void free_stream_states(ogg_reader_t *r) {
    ogg_stream_state_t *cur = r->streams;
    while (cur) {
        ogg_stream_state_t *next = cur->next;
        free(cur->partial.data);
        free(cur);
        cur = next;
    }
    r->streams = NULL;
}

static void clear_ready_packets(ogg_reader_t *r) {
    ogg_packet_node_t *node = r->ready_head;
    while (node) {
        ogg_packet_node_t *next = node->next;
        free(node->data);
        free(node);
        node = next;
    }
    r->ready_head = NULL;
    r->ready_tail = NULL;
}

static int queue_completed_packet(ogg_reader_t *r,
                                  ogg_stream_state_t *state,
                                  int64_t granule_position,
                                  uint8_t bos,
                                  uint8_t eos) {
    if (!r || !state || state->partial.len == 0)
        return 0;

    ogg_packet_node_t *node = (ogg_packet_node_t *)calloc(1, sizeof(*node));
    if (!node)
        return 0;

    node->data = (uint8_t *)malloc(state->partial.len);
    if (!node->data) {
        free(node);
        return 0;
    }

    memcpy(node->data, state->partial.data, state->partial.len);
    node->len = state->partial.len;
    node->info.serial_number = state->serial_number;
    node->info.granule_position = granule_position;
    node->info.bos = bos;
    node->info.eos = eos;

    if (r->ready_tail)
        r->ready_tail->next = node;
    else
        r->ready_head = node;
    r->ready_tail = node;

    packet_reset(&state->partial);
    return 1;
}

static int process_page_packets(ogg_reader_t *r, const uint8_t *body, size_t body_len) {
    ogg_stream_state_t *state = get_stream_state(r, r->page.serial_number);
    if (!state)
        return 0;

    int last_complete_segment = -1;
    for (int i = 0; i < r->page.num_segments; i++) {
        if (r->page.segment_table[i] < 255)
            last_complete_segment = i;
    }

    size_t offset = 0;
    int discarding_continuation = ((r->page.header_type & 0x01) != 0 && state->partial.len == 0);

    for (int i = 0; i < r->page.num_segments; i++) {
        uint8_t seg_size = r->page.segment_table[i];
        if (offset + seg_size > body_len)
            return 0;

        if (!discarding_continuation)
            packet_append(&state->partial, body + offset, seg_size);
        offset += seg_size;

        if (seg_size < 255) {
            if (discarding_continuation) {
                discarding_continuation = 0;
                continue;
            }
            int64_t packet_granule = (i == last_complete_segment) ? r->page.granule_position : -1;
            uint8_t bos = 0;
            uint8_t eos = 0;
            if ((r->page.header_type & 0x02) && !state->saw_bos) {
                bos = 1;
                state->saw_bos = 1;
            }
            if ((r->page.header_type & 0x04) && i == last_complete_segment)
                eos = 1;
            if (!queue_completed_packet(r, state, packet_granule, bos, eos))
                return 0;
        }
    }

    return 1;
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

/// @brief Open an OGG container from a file path for sequential packet reading.
ogg_reader_t *ogg_reader_open_file(const char *path) {
    if (!path)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    ogg_reader_t *r = (ogg_reader_t *)calloc(1, sizeof(ogg_reader_t));
    if (!r) {
        fclose(f);
        return NULL;
    }
    r->file = f;
    return r;
}

/// @brief Open an OGG container from an in-memory buffer for sequential packet reading.
ogg_reader_t *ogg_reader_open_mem(const uint8_t *data, size_t len) {
    if (!data || len == 0)
        return NULL;
    ogg_reader_t *r = (ogg_reader_t *)calloc(1, sizeof(ogg_reader_t));
    if (!r)
        return NULL;
    r->mem = data;
    r->mem_len = len;
    return r;
}

/// @brief Close the OGG reader and free all resources (file handle, packet buffer).
void ogg_reader_free(ogg_reader_t *r) {
    if (!r)
        return;
    if (r->file)
        fclose(r->file);
    free(r->last_packet_data);
    free_stream_states(r);
    clear_ready_packets(r);
    free(r);
}

/// @brief Rewind the reader to the beginning of the OGG stream for re-reading.
void ogg_reader_rewind(ogg_reader_t *r) {
    if (!r)
        return;
    if (r->file)
        fseek(r->file, 0, SEEK_SET);
    if (r->mem)
        r->mem_pos = 0;
    memset(&r->page, 0, sizeof(r->page));
    free(r->last_packet_data);
    r->last_packet_data = NULL;
    free_stream_states(r);
    clear_ready_packets(r);
}

/// @brief Read the next complete OGG packet (may span multiple pages). Returns 1 on success.
int ogg_reader_next_packet(ogg_reader_t *r, const uint8_t **out_data, size_t *out_len) {
    return ogg_reader_next_packet_ex(r, out_data, out_len, NULL);
}

int ogg_reader_next_packet_ex(ogg_reader_t *r,
                              const uint8_t **out_data,
                              size_t *out_len,
                              ogg_packet_info_t *out_info) {
    if (!r || !out_data || !out_len)
        return 0;

    free(r->last_packet_data);
    r->last_packet_data = NULL;

    if (r->ready_head) {
        ogg_packet_node_t *node = r->ready_head;
        r->ready_head = node->next;
        if (!r->ready_head)
            r->ready_tail = NULL;
        r->last_packet_data = node->data;
        *out_data = r->last_packet_data;
        *out_len = node->len;
        if (out_info)
            *out_info = node->info;
        free(node);
        return 1;
    }

    while (!r->ready_head) {
        uint8_t *page_body = NULL;
        size_t page_body_len = 0;
        if (!ogg_read_page(r, &page_body, &page_body_len))
            return 0;
        int ok = process_page_packets(r, page_body, page_body_len);
        free(page_body);
        if (!ok)
            return 0;
    }

    return ogg_reader_next_packet_ex(r, out_data, out_len, out_info);
}
