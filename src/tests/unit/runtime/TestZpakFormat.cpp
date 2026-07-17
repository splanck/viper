//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestZpakFormat.cpp
// Purpose: Unit tests for ZPAK (Zanna Pack Archive) write + read round-trip.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// Build-time ZPAK writer (C++ API)
#include "ZpakWriter.hpp"

extern "C" {
// Runtime ZPAK reader (C API)
#include "rt_zpak_reader.h"
}

struct ManualZpakEntry {
    std::string name;
    uint64_t dataOffset;
    uint64_t dataSize;
    uint64_t storedSize;
    uint16_t flags;
};

static void put_u16(std::vector<uint8_t> &blob, size_t offset, uint16_t value) {
    blob[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    blob[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void put_u32(std::vector<uint8_t> &blob, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        blob[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

static void put_u64(std::vector<uint8_t> &blob, size_t offset, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        blob[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
}

static void append_u16(std::vector<uint8_t> &blob, uint16_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 2);
    put_u16(blob, offset, value);
}

static void append_u64(std::vector<uint8_t> &blob, uint64_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 8);
    put_u64(blob, offset, value);
}

static std::vector<uint8_t> make_manual_zpak(const std::vector<uint8_t> &data,
                                            const std::vector<ManualZpakEntry> &entries) {
    std::vector<uint8_t> blob(32, 0);
    blob[0] = 'Z';
    blob[1] = 'P';
    blob[2] = 'A';
    blob[3] = 'K';
    put_u16(blob, 4, 1);
    put_u32(blob, 8, static_cast<uint32_t>(entries.size()));

    blob.insert(blob.end(), data.begin(), data.end());
    uint64_t tocOffset = static_cast<uint64_t>(blob.size());
    for (const ManualZpakEntry &entry : entries) {
        append_u16(blob, static_cast<uint16_t>(entry.name.size()));
        blob.insert(blob.end(), entry.name.begin(), entry.name.end());
        append_u64(blob, entry.dataOffset);
        append_u64(blob, entry.dataSize);
        append_u64(blob, entry.storedSize);
        append_u16(blob, entry.flags);
        append_u16(blob, 0);
    }
    uint64_t tocSize = static_cast<uint64_t>(blob.size()) - tocOffset;
    put_u64(blob, 12, tocOffset);
    put_u64(blob, 20, tocSize);
    return blob;
}

// ─── ZPAK round-trip: write to memory, read back ─────────────────────────────

TEST(ZpakFormat, WriteAndReadRoundTrip) {
    zanna::asset::ZpakWriter writer;
    const uint8_t data1[] = "hello world";
    const uint8_t data2[] = {0x89, 0x50, 0x4E, 0x47}; // PNG header bytes
    writer.addEntry("test.txt", data1, sizeof(data1) - 1, false);
    writer.addEntry("image.png", data2, sizeof(data2), false);

    auto blob = writer.writeToMemory();
    ASSERT_TRUE(blob.size() >= 32);

    // Parse with runtime reader
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 2u);

    // Find and verify first entry
    const zpak_entry_t *e1 = zpak_find(archive, "test.txt");
    ASSERT_TRUE(e1 != nullptr);
    EXPECT_EQ(e1->data_size, sizeof(data1) - 1);
    EXPECT_EQ(e1->compressed, 0);

    size_t out_size = 0;
    uint8_t *out_data = zpak_read_entry(archive, e1, &out_size);
    ASSERT_TRUE(out_data != nullptr);
    EXPECT_EQ(out_size, sizeof(data1) - 1);
    EXPECT_EQ(memcmp(out_data, data1, out_size), 0);
    free(out_data);

    // Find and verify second entry
    const zpak_entry_t *e2 = zpak_find(archive, "image.png");
    ASSERT_TRUE(e2 != nullptr);
    EXPECT_EQ(e2->data_size, sizeof(data2));

    out_data = zpak_read_entry(archive, e2, &out_size);
    ASSERT_TRUE(out_data != nullptr);
    EXPECT_EQ(out_size, sizeof(data2));
    EXPECT_EQ(memcmp(out_data, data2, out_size), 0);
    free(out_data);

    zpak_close(archive);
}

TEST(ZpakFormat, FindMissing) {
    zanna::asset::ZpakWriter writer;
    const uint8_t data[] = "x";
    writer.addEntry("exists.txt", data, 1, false);

    auto blob = writer.writeToMemory();
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);

    EXPECT_EQ(zpak_find(archive, "missing.txt"), nullptr);
    EXPECT_TRUE(zpak_find(archive, "exists.txt") != nullptr);

    zpak_close(archive);
}

TEST(ZpakFormat, EmptyArchive) {
    zanna::asset::ZpakWriter writer;
    EXPECT_EQ(writer.entryCount(), 0u);
    EXPECT_TRUE(writer.empty());

    auto blob = writer.writeToMemory();
    ASSERT_TRUE(blob.size() >= 32);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 0u);
    EXPECT_EQ(zpak_find(archive, "anything"), nullptr);

    zpak_close(archive);
}

TEST(ZpakFormat, InvalidMagic) {
    uint8_t garbage[64] = {0};
    zpak_archive_t *archive = zpak_open_memory(garbage, sizeof(garbage));
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, TruncatedHeader) {
    uint8_t tiny[16] = {'Z', 'P', 'A', 'K'};
    zpak_archive_t *archive = zpak_open_memory(tiny, sizeof(tiny));
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, WriteToFileAndReadBack) {
    zanna::asset::ZpakWriter writer;
    const uint8_t data[] = "file round trip test";
    writer.addEntry("data.txt", data, sizeof(data) - 1, false);

    const char *tmpPath = "/tmp/zanna_test_zpak_roundtrip.zpak";
    std::string err;
    ASSERT_TRUE(writer.writeToFile(tmpPath, err));

    zpak_archive_t *archive = zpak_open_file(tmpPath);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 1u);

    const zpak_entry_t *e = zpak_find(archive, "data.txt");
    ASSERT_TRUE(e != nullptr);

    size_t out_size = 0;
    uint8_t *out = zpak_read_entry(archive, e, &out_size);
    ASSERT_TRUE(out != nullptr);
    EXPECT_EQ(out_size, sizeof(data) - 1);
    EXPECT_EQ(memcmp(out, data, out_size), 0);
    free(out);

    zpak_close(archive);
    remove(tmpPath);
}

TEST(ZpakFormat, CompressedRoundTrip) {
    zanna::asset::ZpakWriter writer;
    // Create compressible data (repeated pattern)
    std::vector<uint8_t> compressible(4096);
    for (size_t i = 0; i < compressible.size(); i++)
        compressible[i] = static_cast<uint8_t>(i % 26 + 'A');

    writer.addEntry("compressible.txt", compressible.data(), compressible.size(), true);

    auto blob = writer.writeToMemory();
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 1u);

    const zpak_entry_t *e = zpak_find(archive, "compressible.txt");
    ASSERT_TRUE(e != nullptr);
    EXPECT_EQ(e->data_size, 4096u);
    EXPECT_EQ(e->compressed, 1);
    // Stored size should be smaller than original (data is compressible)
    EXPECT_TRUE(e->stored_size < e->data_size);

    size_t out_size = 0;
    uint8_t *out = zpak_read_entry(archive, e, &out_size);
    ASSERT_TRUE(out != nullptr);
    EXPECT_EQ(out_size, compressible.size());
    EXPECT_EQ(memcmp(out, compressible.data(), out_size), 0);
    free(out);

    zpak_close(archive);
}

TEST(ZpakFormat, SkipCompressionForPng) {
    zanna::asset::ZpakWriter writer;
    const uint8_t png_data[] = {0x89, 0x50, 0x4E, 0x47, 0, 0, 0, 0};
    writer.addEntry("icon.png", png_data, sizeof(png_data), true); // compress=true but .png

    auto blob = writer.writeToMemory();
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);

    const zpak_entry_t *e = zpak_find(archive, "icon.png");
    ASSERT_TRUE(e != nullptr);
    // PNG should NOT be compressed (pre-compressed format)
    EXPECT_EQ(e->compressed, 0);
    EXPECT_EQ(e->stored_size, e->data_size);

    zpak_close(archive);
}

TEST(ZpakFormat, ManyEntries) {
    zanna::asset::ZpakWriter writer;
    for (int i = 0; i < 100; i++) {
        char name[64];
        snprintf(name, sizeof(name), "assets/entry_%03d.dat", i);
        uint8_t val = static_cast<uint8_t>(i);
        writer.addEntry(name, &val, 1, false);
    }

    auto blob = writer.writeToMemory();
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 100u);

    // Verify all entries are findable
    for (int i = 0; i < 100; i++) {
        char name[64];
        snprintf(name, sizeof(name), "assets/entry_%03d.dat", i);
        const zpak_entry_t *e = zpak_find(archive, name);
        ASSERT_TRUE(e != nullptr);

        size_t sz = 0;
        uint8_t *d = zpak_read_entry(archive, e, &sz);
        ASSERT_TRUE(d != nullptr);
        EXPECT_EQ(sz, 1u);
        EXPECT_EQ(d[0], static_cast<uint8_t>(i));
        free(d);
    }

    zpak_close(archive);
}

TEST(ZpakFormat, RejectsPartialToc) {
    const uint8_t data[] = {'x'};
    zanna::asset::ZpakWriter writer;
    writer.addEntry("exists.txt", data, sizeof(data), false);
    auto blob = writer.writeToMemory();
    ASSERT_TRUE(blob.size() > 32);
    blob.pop_back();

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, RejectsDuplicateTocNames) {
    std::vector<uint8_t> data = {'a', 'b'};
    std::vector<ManualZpakEntry> entries = {
        {"dup.txt", 32, 1, 1, 0},
        {"dup.txt", 33, 1, 1, 0},
    };
    auto blob = make_manual_zpak(data, entries);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, UncompressedStoredSizeMustMatchDataSize) {
    std::vector<uint8_t> data = {'a', 'b'};
    std::vector<ManualZpakEntry> entries = {
        {"bad.bin", 32, 4, 2, 0},
    };
    auto blob = make_manual_zpak(data, entries);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *entry = zpak_find(archive, "bad.bin");
    ASSERT_TRUE(entry != nullptr);

    size_t outSize = 123;
    uint8_t *out = zpak_read_entry(archive, entry, &outSize);
    EXPECT_EQ(out, nullptr);
    EXPECT_EQ(outSize, 0u);
    zpak_close(archive);
}

TEST(ZpakFormat, ReadRejectsOverflowingEntryRange) {
    std::vector<uint8_t> data = {'x'};
    std::vector<ManualZpakEntry> entries = {
        {"overflow.bin", std::numeric_limits<uint64_t>::max(), 1, 1, 0},
    };
    auto blob = make_manual_zpak(data, entries);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *entry = zpak_find(archive, "overflow.bin");
    ASSERT_TRUE(entry != nullptr);

    size_t outSize = 123;
    uint8_t *out = zpak_read_entry(archive, entry, &outSize);
    EXPECT_EQ(out, nullptr);
    EXPECT_EQ(outSize, 0u);
    zpak_close(archive);
}

TEST(ZpakFormat, FileBackedReadRejectsOutOfFileEntryRange) {
    std::vector<uint8_t> data = {'x'};
    std::vector<ManualZpakEntry> entries = {
        {"past-eof.bin", 999999, 1, 1, 0},
    };
    auto blob = make_manual_zpak(data, entries);

    const char *tmpPath = "/tmp/zanna_test_zpak_past_eof.zpak";
    FILE *fp = fopen(tmpPath, "wb");
    ASSERT_TRUE(fp != nullptr);
    ASSERT_EQ(fwrite(blob.data(), 1, blob.size(), fp), blob.size());
    fclose(fp);

    zpak_archive_t *archive = zpak_open_file(tmpPath);
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *entry = zpak_find(archive, "past-eof.bin");
    ASSERT_TRUE(entry != nullptr);

    size_t outSize = 123;
    uint8_t *out = zpak_read_entry(archive, entry, &outSize);
    EXPECT_EQ(out, nullptr);
    EXPECT_EQ(outSize, 0u);

    zpak_close(archive);
    remove(tmpPath);
}

#ifndef _WIN32
TEST(ZpakFormat, NoFollowOpenRejectsSymlinkPack) {
    zanna::asset::ZpakWriter writer;
    const uint8_t data[] = "pack data";
    writer.addEntry("data.txt", data, sizeof(data) - 1, false);

    const char *realPath = "/tmp/zanna_test_zpak_real.zpak";
    const char *linkPath = "/tmp/zanna_test_zpak_link.zpak";
    remove(realPath);
    unlink(linkPath);

    std::string err;
    ASSERT_TRUE(writer.writeToFile(realPath, err));
    ASSERT_EQ(symlink(realPath, linkPath), 0);

    EXPECT_EQ(zpak_open_file_no_follow(linkPath), nullptr);

    unlink(linkPath);
    remove(realPath);
}
#endif

int main() {
    return zanna_test::run_all_tests();
}
