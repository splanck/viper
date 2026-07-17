//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/audio/rt_vorbis.c"

typedef struct {
    uint8_t data[128];
    size_t len;
    uint8_t cur;
    int bits_used;
} bit_writer_t;

static void bw_put_bits(bit_writer_t *bw, uint32_t value, int count) {
    for (int i = 0; i < count; i++) {
        bw->cur |= (uint8_t)(((value >> i) & 1u) << bw->bits_used);
        bw->bits_used++;
        if (bw->bits_used == 8) {
            assert(bw->len < sizeof(bw->data));
            bw->data[bw->len++] = bw->cur;
            bw->cur = 0;
            bw->bits_used = 0;
        }
    }
}

static size_t bw_finish_setup(bit_writer_t *bw, uint8_t *out, size_t out_cap) {
    static const uint8_t prefix[7] = {5, 'v', 'o', 'r', 'b', 'i', 's'};
    assert(out_cap >= sizeof(prefix) + bw->len + 1);
    memcpy(out, prefix, sizeof(prefix));
    if (bw->bits_used != 0) {
        assert(bw->len < sizeof(bw->data));
        bw->data[bw->len++] = bw->cur;
        bw->cur = 0;
        bw->bits_used = 0;
    }
    memcpy(out + sizeof(prefix), bw->data, bw->len);
    return sizeof(prefix) + bw->len;
}

static void bw_put_minimal_codebook(bit_writer_t *bw) {
    bw_put_bits(bw, 0x564342u, 24); // sync
    bw_put_bits(bw, 1, 16);         // dimensions
    bw_put_bits(bw, 1, 24);         // entries
    bw_put_bits(bw, 0, 1);          // unordered
    bw_put_bits(bw, 0, 1);          // not sparse
    bw_put_bits(bw, 0, 5);          // code length 1
}

static void test_25_bit_codeword_decodes_without_truncation(void) {
    vorbis_codebook_t cb;
    uint8_t lengths[2] = {25, 25};
    uint8_t bits_data[4] = {0, 0, 0, 1};
    vorbis_bits_t bits;

    memset(&cb, 0, sizeof(cb));
    cb.entries = 2;
    cb.dimensions = 1;
    cb.lengths = lengths;

    assert(codebook_build_tree(&cb) == 0);
    assert(cb.sorted_count == 2);
    assert(cb.sorted_code_lengths[0] == 25);
    assert(cb.sorted_code_lengths[1] == 25);
    assert(cb.sorted_codes[1] == (1u << 24));

    bits_init(&bits, bits_data, sizeof(bits_data));
    assert(codebook_decode_scalar(&cb, &bits) == 1);

    free(cb.sorted_codes);
    free(cb.sorted_code_lengths);
    free(cb.sorted_indices);
}

static void test_setup_rejects_oversized_codebook_dimension(void) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    bit_writer_t bw;
    uint8_t setup[160];
    size_t setup_len;

    memset(&bw, 0, sizeof(bw));
    bw_put_bits(&bw, 0, 8); // one codebook
    bw_put_bits(&bw, 0x564342u, 24);
    bw_put_bits(&bw, 257, 16); // dimensions exceed internal vector buffers
    bw_put_bits(&bw, 1, 24);
    setup_len = bw_finish_setup(&bw, setup, sizeof(setup));

    assert(vorbis_decode_header(dec, setup, setup_len, 2) == -1);
    vorbis_decoder_free(dec);
}

static void test_setup_rejects_invalid_codebook_lookup_type(void) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    bit_writer_t bw;
    uint8_t setup[160];
    size_t setup_len;

    memset(&bw, 0, sizeof(bw));
    bw_put_bits(&bw, 0, 8); // one codebook
    bw_put_minimal_codebook(&bw);
    bw_put_bits(&bw, 3, 4); // only lookup types 0, 1, and 2 are valid
    setup_len = bw_finish_setup(&bw, setup, sizeof(setup));

    assert(vorbis_decode_header(dec, setup, setup_len, 2) == -1);
    vorbis_decoder_free(dec);
}

static void test_setup_rejects_nonzero_time_transform(void) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    bit_writer_t bw;
    uint8_t setup[160];
    size_t setup_len;

    memset(&bw, 0, sizeof(bw));
    bw_put_bits(&bw, 0, 8); // one codebook
    bw_put_minimal_codebook(&bw);
    bw_put_bits(&bw, 0, 4);  // lookup type 0
    bw_put_bits(&bw, 0, 6);  // one time-domain transform
    bw_put_bits(&bw, 1, 16); // time-domain transforms must be zero
    setup_len = bw_finish_setup(&bw, setup, sizeof(setup));

    assert(vorbis_decode_header(dec, setup, setup_len, 2) == -1);
    vorbis_decoder_free(dec);
}

int main(void) {
    test_25_bit_codeword_decodes_without_truncation();
    test_setup_rejects_oversized_codebook_dimension();
    test_setup_rejects_invalid_codebook_lookup_type();
    test_setup_rejects_nonzero_time_transform();
    return 0;
}
