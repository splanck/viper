//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_vorbis.h
// Purpose: Vorbis audio decoder — decodes OGG Vorbis to PCM.
// Key invariants:
//   - Baseline Vorbis I decoder (floor type 1, residue types 0/1/2)
//   - Output is 16-bit signed PCM, interleaved stereo (or mono)
//   - Forward-only decoding (no seeking)
// Ownership/Lifetime:
//   - Caller owns vorbis_decoder and must call vorbis_decoder_free
// Links: rt_ogg.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque Vorbis decoder handle.
typedef struct vorbis_decoder vorbis_decoder_t;

/// @brief Create a Vorbis decoder.
/// @return New decoder or NULL on failure.
vorbis_decoder_t *vorbis_decoder_new(void);

/// @brief Free a Vorbis decoder and all its resources.
void vorbis_decoder_free(vorbis_decoder_t *dec);

/// @brief Feed the three Vorbis header packets to the decoder.
/// @param dec Decoder instance.
/// @param packet_data Pointer to packet data.
/// @param packet_len Length of packet data.
/// @param packet_num 0=identification, 1=comment, 2=setup.
/// @return 0 on success, -1 on error.
int vorbis_decode_header(vorbis_decoder_t *dec,
                         const uint8_t *packet_data,
                         size_t packet_len,
                         int packet_num);

/// @brief Decode one audio packet into PCM samples.
/// @param dec Decoder instance (headers must have been parsed first).
/// @param packet_data Pointer to packet data.
/// @param packet_len Length of packet data.
/// @param out_pcm Receives pointer to interleaved 16-bit PCM. Valid until next call.
/// @param out_samples Receives number of output samples (per channel).
/// @return 0 on success, -1 on error.
int vorbis_decode_packet(vorbis_decoder_t *dec,
                         const uint8_t *packet_data,
                         size_t packet_len,
                         int16_t **out_pcm,
                         int *out_samples);

/// @brief Get the sample rate from the identification header.
int vorbis_get_sample_rate(const vorbis_decoder_t *dec);

/// @brief Get the number of channels from the identification header.
int vorbis_get_channels(const vorbis_decoder_t *dec);

#ifdef __cplusplus
}
#endif
