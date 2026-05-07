//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_ogg.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

uint32_t g_crc_table[256];
bool g_crc_ready = false;

void crc_init() {
    if (g_crc_ready)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int j = 0; j < 8; j++)
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = true;
}

uint32_t ogg_crc(const std::vector<uint8_t> &data) {
    crc_init();
    uint32_t crc = 0;
    for (uint8_t byte : data)
        crc = (crc << 8) ^ g_crc_table[((crc >> 24) ^ byte) & 0xFFu];
    return crc;
}

void put_u32_le(std::vector<uint8_t> &buf, size_t off, uint32_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xFFu);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    buf[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    buf[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

std::vector<uint8_t> make_single_packet_page(uint32_t serial,
                                             const std::vector<uint8_t> &packet,
                                             bool corrupt_crc = false) {
    assert(packet.size() < 255);
    std::vector<uint8_t> page(27 + 1 + packet.size(), 0);
    page[0] = 'O';
    page[1] = 'g';
    page[2] = 'g';
    page[3] = 'S';
    page[4] = 0;    // stream structure version
    page[5] = 0x02; // BOS
    for (int i = 0; i < 8; i++)
        page[6 + i] = 0xFFu;
    put_u32_le(page, 14, serial);
    put_u32_le(page, 18, 0);
    page[26] = 1;
    page[27] = static_cast<uint8_t>(packet.size());
    std::memcpy(page.data() + 28, packet.data(), packet.size());

    uint32_t crc = ogg_crc(page);
    if (corrupt_crc)
        crc ^= 1u;
    put_u32_le(page, 22, crc);
    return page;
}

void test_resync_and_serial_zero() {
    std::vector<uint8_t> packet = {1, 'v', 'o', 'r', 'b', 'i', 's'};
    std::vector<uint8_t> data = {'x'};
    std::vector<uint8_t> page = make_single_packet_page(0, packet);
    data.insert(data.end(), page.begin(), page.end());

    ogg_reader_t *reader = ogg_reader_open_mem(data.data(), data.size());
    assert(reader != nullptr);

    const uint8_t *out = nullptr;
    size_t out_len = 0;
    ogg_packet_info_t info{};
    assert(ogg_reader_next_packet_ex(reader, &out, &out_len, &info) == 1);
    assert(out_len == packet.size());
    assert(std::memcmp(out, packet.data(), packet.size()) == 0);
    assert(info.serial_number == 0);
    assert(info.bos == 1);

    ogg_reader_free(reader);
}

void test_crc_mismatch_rejected() {
    std::vector<uint8_t> packet = {'b', 'a', 'd'};
    std::vector<uint8_t> page = make_single_packet_page(123, packet, true);

    ogg_reader_t *reader = ogg_reader_open_mem(page.data(), page.size());
    assert(reader != nullptr);

    const uint8_t *out = nullptr;
    size_t out_len = 0;
    assert(ogg_reader_next_packet(reader, &out, &out_len) == 0);

    ogg_reader_free(reader);
}

} // namespace

int main() {
    test_resync_and_serial_zero();
    test_crc_mismatch_rejected();
    return 0;
}
