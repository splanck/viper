//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../runtime/audio/rt_vorbis.c"

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

int main(void) {
    test_25_bit_codeword_decodes_without_truncation();
    return 0;
}
