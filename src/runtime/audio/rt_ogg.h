//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_ogg.h
// Purpose: OGG container format parser — extracts Vorbis packets from .ogg files.
// Key invariants:
//   - OGG pages are validated by CRC-32 before packet extraction
//   - Packets can span multiple pages (continued flag)
//   - Forward-only streaming — no seeking
// Ownership/Lifetime:
//   - Caller owns the ogg_reader and must call ogg_reader_free
// Links: src/runtime/audio/rt_vorbis.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief OGG page header (27 bytes + segment table)
typedef struct {
    uint8_t version;
    uint8_t header_type; // 0x01=continued, 0x02=BOS, 0x04=EOS
    int64_t granule_position;
    uint32_t serial_number;
    uint32_t page_sequence;
    uint32_t checksum;
    uint8_t num_segments;
    uint8_t segment_table[255];
} ogg_page_header_t;

/// @brief OGG packet (variable-length)
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int complete; // 1 if packet is complete, 0 if continued on next page
} ogg_packet_t;

/// @brief Metadata attached to a completed logical OGG packet.
typedef struct {
    uint32_t serial_number;
    int64_t granule_position; // -1 when not known for this packet
    uint8_t bos;
    uint8_t eos;
} ogg_packet_info_t;

typedef struct ogg_stream_state_t ogg_stream_state_t;
typedef struct ogg_packet_node_t ogg_packet_node_t;

/// @brief OGG file reader
typedef struct {
    FILE *file; // NULL for memory-based reading
    const uint8_t *mem;
    size_t mem_len;
    size_t mem_pos;

    // Current page state
    ogg_page_header_t page;
    ogg_stream_state_t *streams;
    ogg_packet_node_t *ready_head;
    ogg_packet_node_t *ready_tail;
    uint8_t *last_packet_data;
} ogg_reader_t;

/// @brief Create an OGG reader from a file path.
ogg_reader_t *ogg_reader_open_file(const char *path);

/// @brief Create an OGG reader from memory.
ogg_reader_t *ogg_reader_open_mem(const uint8_t *data, size_t len);

/// @brief Free an OGG reader.
void ogg_reader_free(ogg_reader_t *r);

/// @brief Read the next complete packet.
/// @return 1 if a packet was read, 0 on EOF or error.
/// The returned packet data is valid until the next call to ogg_reader_next_packet.
int ogg_reader_next_packet(ogg_reader_t *r, const uint8_t **out_data, size_t *out_len);

/// @brief Read the next complete packet plus stream metadata.
/// @return 1 if a packet was read, 0 on EOF or error.
/// The returned packet data is valid until the next call to ogg_reader_next_packet[_ex].
int ogg_reader_next_packet_ex(ogg_reader_t *r,
                              const uint8_t **out_data,
                              size_t *out_len,
                              ogg_packet_info_t *out_info);

/// @brief Reset reader to the beginning of the file (for looping).
/// @details Seeks the file to position 0 and resets page/packet state.
void ogg_reader_rewind(ogg_reader_t *r);

#ifdef __cplusplus
}
#endif
