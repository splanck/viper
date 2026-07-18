//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestZpakFormat.cpp
// Purpose: Verify ZPAK parsing, bounds checks, retained lifetime, concurrent
//          file reads, and build-time writer/runtime reader round trips.
// Key invariants:
//   - Concurrent reads sharing one archive never interfere with file position.
//   - Archive storage remains alive until the final retained reference closes.
// Ownership/Lifetime:
//   - Each test closes every archive reference and removes temporary files.
// Links: src/runtime/io/rt_zpak_reader.c, src/tools/common/asset/ZpakWriter.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// Build-time ZPAK writer (C++ API)
#include "ZpakWriter.hpp"

extern "C" {
// Runtime ZPAK reader (C API)
#include "rt_crc32.h"
#include "rt_zpak_reader.h"
}

struct ManualZpakEntry {
    std::string name;
    uint64_t dataOffset;
    uint64_t dataSize;
    uint64_t storedSize;
    uint16_t flags;
    uint16_t reserved{0};
    uint32_t crc32{0};
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

static void append_u32(std::vector<uint8_t> &blob, uint32_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 4);
    put_u32(blob, offset, value);
}

static void append_u64(std::vector<uint8_t> &blob, uint64_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 8);
    put_u64(blob, offset, value);
}

static std::vector<uint8_t> make_manual_zpak(const std::vector<uint8_t> &data,
                                             const std::vector<ManualZpakEntry> &entries,
                                             uint16_t version = RT_ZPAK_VERSION_1,
                                             uint16_t headerFlags = UINT16_MAX) {
    std::vector<uint8_t> blob(32, 0);
    blob[0] = 'Z';
    blob[1] = 'P';
    blob[2] = 'A';
    blob[3] = 'K';
    put_u16(blob, 4, version);
    if (headerFlags == UINT16_MAX) {
        headerFlags = version == RT_ZPAK_VERSION_2 ? RT_ZPAK_HEADER_FLAG_ENTRY_CRC32 : 0;
        for (const ManualZpakEntry &entry : entries) {
            if ((entry.flags & RT_ZPAK_ENTRY_FLAG_COMPRESSED) != 0) {
                headerFlags |= RT_ZPAK_HEADER_FLAG_COMPRESSED;
                break;
            }
        }
    }
    put_u16(blob, 6, headerFlags);
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
        append_u16(blob, entry.reserved);
        if (version == RT_ZPAK_VERSION_2)
            append_u32(blob, entry.crc32);
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
    EXPECT_EQ(static_cast<uint16_t>(blob[4] | (static_cast<uint16_t>(blob[5]) << 8)),
              static_cast<uint16_t>(RT_ZPAK_VERSION_2));
    EXPECT_TRUE((blob[6] & RT_ZPAK_HEADER_FLAG_ENTRY_CRC32) != 0);

    // Parse with runtime reader
    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->count, 2u);
    EXPECT_EQ(archive->version, static_cast<uint16_t>(RT_ZPAK_VERSION_2));

    // Find and verify first entry
    const zpak_entry_t *e1 = zpak_find(archive, "test.txt");
    ASSERT_TRUE(e1 != nullptr);
    EXPECT_EQ(e1->data_size, sizeof(data1) - 1);
    EXPECT_EQ(e1->compressed, 0);
    EXPECT_EQ(e1->crc32, rt_crc32_compute(data1, sizeof(data1) - 1));

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

TEST(ZpakFormat, ReadsLegacyVersionOneWithoutChecksum) {
    std::vector<uint8_t> data = {'l', 'e', 'g', 'a', 'c', 'y'};
    std::vector<ManualZpakEntry> entries = {
        {"legacy.txt", 32, data.size(), data.size(), 0},
    };
    auto blob = make_manual_zpak(data, entries, RT_ZPAK_VERSION_1);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->version, static_cast<uint16_t>(RT_ZPAK_VERSION_1));
    const zpak_entry_t *entry = zpak_find(archive, "legacy.txt");
    ASSERT_TRUE(entry != nullptr);
    size_t size = 0;
    uint8_t *bytes = zpak_read_entry(archive, entry, &size);
    ASSERT_TRUE(bytes != nullptr);
    EXPECT_EQ(size, data.size());
    EXPECT_EQ(std::memcmp(bytes, data.data(), size), 0);
    std::free(bytes);
    zpak_close(archive);
}

TEST(ZpakFormat, VersionTwoRejectsCorruptRawEntry) {
    zanna::asset::ZpakWriter writer;
    const uint8_t original[] = "checksum raw payload";
    writer.addEntry("raw.txt", original, sizeof(original) - 1, false);
    auto blob = writer.writeToMemory();

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *entry = zpak_find(archive, "raw.txt");
    ASSERT_TRUE(entry != nullptr);
    const size_t dataOffset = static_cast<size_t>(entry->data_offset);
    zpak_close(archive);

    blob[dataOffset] ^= 0x40;
    archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    entry = zpak_find(archive, "raw.txt");
    ASSERT_TRUE(entry != nullptr);
    size_t size = 123;
    uint8_t *bytes = zpak_read_entry(archive, entry, &size);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
    zpak_close(archive);
}

TEST(ZpakFormat, VersionTwoChecksInflatedEntryCrc) {
    zanna::asset::ZpakWriter writer;
    std::vector<uint8_t> original(4096, 'Q');
    writer.addEntry("compressed.txt", original.data(), original.size(), true);
    auto blob = writer.writeToMemory();

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *entry = zpak_find(archive, "compressed.txt");
    ASSERT_TRUE(entry != nullptr);
    ASSERT_TRUE(entry->compressed != 0);
    zpak_close(archive);

    // A one-entry v2 TOC ends with that entry's four-byte checksum. Altering
    // only the checksum keeps DEFLATE valid and isolates post-inflate checking.
    blob.back() ^= 0x80;
    archive = zpak_open_memory(blob.data(), blob.size());
    ASSERT_TRUE(archive != nullptr);
    entry = zpak_find(archive, "compressed.txt");
    ASSERT_TRUE(entry != nullptr);
    size_t size = 123;
    uint8_t *bytes = zpak_read_entry(archive, entry, &size);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);
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

TEST(ZpakFormat, RejectsEntryCountThatCannotFitToc) {
    std::vector<uint8_t> data = {'x'};
    std::vector<ManualZpakEntry> entries = {
        {"one.txt", 32, 1, 1, 0},
    };
    auto blob = make_manual_zpak(data, entries);
    put_u32(blob, 8, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(zpak_open_memory(blob.data(), blob.size()), nullptr);
}

TEST(ZpakFormat, RejectsUnsupportedVersionFlagsAndReservedHeader) {
    std::vector<uint8_t> data = {'x'};
    std::vector<ManualZpakEntry> entries = {
        {"one.txt", 32, 1, 1, 0},
    };
    auto baseline = make_manual_zpak(data, entries);

    auto unsupportedVersion = baseline;
    put_u16(unsupportedVersion, 4, 99);
    EXPECT_EQ(zpak_open_memory(unsupportedVersion.data(), unsupportedVersion.size()), nullptr);

    auto unknownFlags = baseline;
    put_u16(unknownFlags, 6, UINT16_C(0x8000));
    EXPECT_EQ(zpak_open_memory(unknownFlags.data(), unknownFlags.size()), nullptr);

    auto reservedHeader = baseline;
    put_u32(reservedHeader, 28, 1);
    EXPECT_EQ(zpak_open_memory(reservedHeader.data(), reservedHeader.size()), nullptr);

    auto versionTwoMissingCrcFlag =
        make_manual_zpak(data,
                         {{"one.txt", 32, 1, 1, 0, 0, rt_crc32_compute(data.data(), data.size())}},
                         RT_ZPAK_VERSION_2,
                         0);
    EXPECT_EQ(zpak_open_memory(versionTwoMissingCrcFlag.data(), versionTwoMissingCrcFlag.size()),
              nullptr);
}

TEST(ZpakFormat, RejectsUnknownEntryFlagsReservedFieldsAndAggregateMismatch) {
    std::vector<uint8_t> data = {'x'};

    auto unknownEntryFlags = make_manual_zpak(data, {{"one.txt", 32, 1, 1, UINT16_C(0x8000)}});
    EXPECT_EQ(zpak_open_memory(unknownEntryFlags.data(), unknownEntryFlags.size()), nullptr);

    auto reservedEntry = make_manual_zpak(data, {{"one.txt", 32, 1, 1, 0, 1}});
    EXPECT_EQ(zpak_open_memory(reservedEntry.data(), reservedEntry.size()), nullptr);

    auto aggregateMismatch = make_manual_zpak(
        data, {{"one.txt", 32, 1, 1, 0}}, RT_ZPAK_VERSION_1, RT_ZPAK_HEADER_FLAG_COMPRESSED);
    EXPECT_EQ(zpak_open_memory(aggregateMismatch.data(), aggregateMismatch.size()), nullptr);
}

TEST(ZpakFormat, RejectsUnsafeEntryNames) {
    std::vector<uint8_t> data = {'x'};
    for (const std::string &name : {std::string("../escape"),
                                    std::string("a//b"),
                                    std::string("C:drive"),
                                    std::string("a\\b")}) {
        auto blob = make_manual_zpak(data, {{name, 32, 1, 1, 0}});
        EXPECT_EQ(zpak_open_memory(blob.data(), blob.size()), nullptr);
    }
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

TEST(ZpakFormat, ConcurrentFileReadsRetainLifetimeAndPosition) {
    zanna::asset::ZpakWriter writer;
    std::vector<uint8_t> first(32768, 0x3A);
    std::vector<uint8_t> second(32768, 0xC5);
    writer.addEntry("first.bin", first.data(), first.size(), false);
    writer.addEntry("second.bin", second.data(), second.size(), false);

    const char *tmpPath = "/tmp/zanna_test_zpak_concurrent.zpak";
    std::string err;
    ASSERT_TRUE(writer.writeToFile(tmpPath, err));

    zpak_archive_t *archive = zpak_open_file(tmpPath);
    ASSERT_TRUE(archive != nullptr);
    const zpak_entry_t *first_entry = zpak_find(archive, "first.bin");
    const zpak_entry_t *second_entry = zpak_find(archive, "second.bin");
    ASSERT_TRUE(first_entry != nullptr);
    ASSERT_TRUE(second_entry != nullptr);

    std::atomic<bool> valid{true};
    auto reader = [&](const zpak_entry_t *entry, const std::vector<uint8_t> &expected) {
        if (!zpak_retain(archive)) {
            valid.store(false, std::memory_order_relaxed);
            return;
        }
        for (int iteration = 0; iteration < 100; ++iteration) {
            size_t size = 0;
            uint8_t *bytes = zpak_read_entry(archive, entry, &size);
            if (!bytes || size != expected.size() ||
                std::memcmp(bytes, expected.data(), expected.size()) != 0)
                valid.store(false, std::memory_order_relaxed);
            std::free(bytes);
        }
        zpak_close(archive);
    };

    std::thread first_reader(reader, first_entry, std::cref(first));
    std::thread second_reader(reader, second_entry, std::cref(second));
    first_reader.join();
    second_reader.join();

    EXPECT_TRUE(valid.load(std::memory_order_relaxed));
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
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, OpenRejectsOverflowingEntryRange) {
    std::vector<uint8_t> data = {'x'};
    std::vector<ManualZpakEntry> entries = {
        {"overflow.bin", std::numeric_limits<uint64_t>::max(), 1, 1, 0},
    };
    auto blob = make_manual_zpak(data, entries);

    zpak_archive_t *archive = zpak_open_memory(blob.data(), blob.size());
    EXPECT_EQ(archive, nullptr);
}

TEST(ZpakFormat, FileOpenRejectsOutOfFileEntryRange) {
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
    EXPECT_EQ(archive, nullptr);
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
