//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestVpaFormat.cpp
// Purpose: Unit tests for VPA (Viper Pack Archive) write + read round-trip.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Build-time VPA writer (C++ API)
#include "VpaWriter.hpp"

extern "C" {
// Runtime VPA reader (C API)
#include "rt_vpa_reader.h"
}

// ─── VPA round-trip: write to memory, read back ─────────────────────────────

TEST(VpaFormat, WriteAndReadRoundTrip) {
    viper::asset::VpaWriter writer;
    const uint8_t data1[] = "hello world";
    const uint8_t data2[] = {0x89, 0x50, 0x4E, 0x47}; // PNG header bytes
    writer.addEntry("test.txt", data1, sizeof(data1) - 1, false);
    writer.addEntry("image.png", data2, sizeof(data2), false);

    auto blob = writer.writeToMemory();
    ASSERT_TRUE(blob.size() >= 32);

    // Parse with runtime reader
    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 2u);

    // Find and verify first entry
    const vpa_entry_t *e1 = vpa_find(archive, "test.txt");
    ASSERT_TRUE(e1 != nullptr);
    EXPECT_EQ(e1->data_size, sizeof(data1) - 1);
    EXPECT_EQ(e1->compressed, 0);

    size_t out_size = 0;
    uint8_t *out_data = vpa_read_entry(archive, e1, &out_size);
    ASSERT_TRUE(out_data != nullptr);
    EXPECT_EQ(out_size, sizeof(data1) - 1);
    EXPECT_EQ(memcmp(out_data, data1, out_size), 0);
    free(out_data);

    // Find and verify second entry
    const vpa_entry_t *e2 = vpa_find(archive, "image.png");
    ASSERT_TRUE(e2 != nullptr);
    EXPECT_EQ(e2->data_size, sizeof(data2));

    out_data = vpa_read_entry(archive, e2, &out_size);
    ASSERT_TRUE(out_data != nullptr);
    EXPECT_EQ(out_size, sizeof(data2));
    EXPECT_EQ(memcmp(out_data, data2, out_size), 0);
    free(out_data);

    vpa_close(archive);
}

TEST(VpaFormat, FindMissing) {
    viper::asset::VpaWriter writer;
    const uint8_t data[] = "x";
    writer.addEntry("exists.txt", data, 1, false);

    auto blob = writer.writeToMemory();
    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);

    EXPECT_EQ(vpa_find(archive, "missing.txt"), nullptr);
    EXPECT_TRUE(vpa_find(archive, "exists.txt") != nullptr);

    vpa_close(archive);
}

TEST(VpaFormat, EmptyArchive) {
    viper::asset::VpaWriter writer;
    EXPECT_EQ(writer.entryCount(), 0u);
    EXPECT_TRUE(writer.empty());

    auto blob = writer.writeToMemory();
    ASSERT_TRUE(blob.size() >= 32);

    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 0u);
    EXPECT_EQ(vpa_find(archive, "anything"), nullptr);

    vpa_close(archive);
}

TEST(VpaFormat, InvalidMagic) {
    uint8_t garbage[64] = {0};
    vpa_archive_t *archive = vpa_open_memory(garbage, sizeof(garbage));
    EXPECT_EQ(archive, nullptr);
}

TEST(VpaFormat, TruncatedHeader) {
    uint8_t tiny[16] = {'V', 'P', 'A', '1'};
    vpa_archive_t *archive = vpa_open_memory(tiny, sizeof(tiny));
    EXPECT_EQ(archive, nullptr);
}

TEST(VpaFormat, WriteToFileAndReadBack) {
    viper::asset::VpaWriter writer;
    const uint8_t data[] = "file round trip test";
    writer.addEntry("data.txt", data, sizeof(data) - 1, false);

    const char *tmpPath = "/tmp/viper_test_vpa_roundtrip.vpa";
    std::string err;
    ASSERT_TRUE(writer.writeToFile(tmpPath, err));

    vpa_archive_t *archive = vpa_open_file(tmpPath);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 1u);

    const vpa_entry_t *e = vpa_find(archive, "data.txt");
    ASSERT_TRUE(e != nullptr);

    size_t out_size = 0;
    uint8_t *out = vpa_read_entry(archive, e, &out_size);
    ASSERT_TRUE(out != nullptr);
    EXPECT_EQ(out_size, sizeof(data) - 1);
    EXPECT_EQ(memcmp(out, data, out_size), 0);
    free(out);

    vpa_close(archive);
    remove(tmpPath);
}

TEST(VpaFormat, CompressedRoundTrip) {
    viper::asset::VpaWriter writer;
    // Create compressible data (repeated pattern)
    std::vector<uint8_t> compressible(4096);
    for (size_t i = 0; i < compressible.size(); i++)
        compressible[i] = static_cast<uint8_t>(i % 26 + 'A');

    writer.addEntry("compressible.txt", compressible.data(), compressible.size(), true);

    auto blob = writer.writeToMemory();
    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 1u);

    const vpa_entry_t *e = vpa_find(archive, "compressible.txt");
    ASSERT_TRUE(e != nullptr);
    EXPECT_EQ(e->data_size, 4096u);
    EXPECT_EQ(e->compressed, 1);
    // Stored size should be smaller than original (data is compressible)
    EXPECT_TRUE(e->stored_size < e->data_size);

    size_t out_size = 0;
    uint8_t *out = vpa_read_entry(archive, e, &out_size);
    ASSERT_TRUE(out != nullptr);
    EXPECT_EQ(out_size, compressible.size());
    EXPECT_EQ(memcmp(out, compressible.data(), out_size), 0);
    free(out);

    vpa_close(archive);
}

TEST(VpaFormat, SkipCompressionForPng) {
    viper::asset::VpaWriter writer;
    const uint8_t png_data[] = {0x89, 0x50, 0x4E, 0x47, 0, 0, 0, 0};
    writer.addEntry("icon.png", png_data, sizeof(png_data), true); // compress=true but .png

    auto blob = writer.writeToMemory();
    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);

    const vpa_entry_t *e = vpa_find(archive, "icon.png");
    ASSERT_TRUE(e != nullptr);
    // PNG should NOT be compressed (pre-compressed format)
    EXPECT_EQ(e->compressed, 0);
    EXPECT_EQ(e->stored_size, e->data_size);

    vpa_close(archive);
}

TEST(VpaFormat, ManyEntries) {
    viper::asset::VpaWriter writer;
    for (int i = 0; i < 100; i++) {
        char name[64];
        snprintf(name, sizeof(name), "assets/entry_%03d.dat", i);
        uint8_t val = static_cast<uint8_t>(i);
        writer.addEntry(name, &val, 1, false);
    }

    auto blob = writer.writeToMemory();
    vpa_archive_t *archive = vpa_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 100u);

    // Verify all entries are findable
    for (int i = 0; i < 100; i++) {
        char name[64];
        snprintf(name, sizeof(name), "assets/entry_%03d.dat", i);
        const vpa_entry_t *e = vpa_find(archive, name);
        ASSERT_TRUE(e != nullptr);

        size_t sz = 0;
        uint8_t *d = vpa_read_entry(archive, e, &sz);
        ASSERT_TRUE(d != nullptr);
        EXPECT_EQ(sz, 1u);
        EXPECT_EQ(d[0], static_cast<uint8_t>(i));
        free(d);
    }

    vpa_close(archive);
}

int main() {
    return viper_test::run_all_tests();
}
