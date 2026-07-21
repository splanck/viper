//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAssetTests.cpp
// Purpose: Verify asset lookup, packed-data failure handling, type dispatch,
//          filesystem safety, and zero-byte asset behavior.
// Key invariants:
//   - Trap-capable packed reads never leave the asset manager locked.
//   - Unsafe or non-regular filesystem paths are never exposed as assets.
// Ownership/Lifetime:
//   - Temporary files are removed by the test that creates them.
//   - The embedded ZPAK vector remains alive for the process lifetime.
// Links: src/runtime/io/rt_asset.c, src/runtime/io/rt_zpak_reader.c
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_asset.h"
#include "rt_bytes.h"
#include "rt_file_path.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_zpak_format.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define mkdir_p(path) _mkdir(path)
#define rmdir_p(path) _rmdir(path)
#define unlink_p(path) _unlink(path)
#define GETPID _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_p(path) mkdir(path, 0755)
#define rmdir_p(path) rmdir(path)
#define unlink_p(path) unlink(path)
#define GETPID getpid
#endif

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

/// @brief Store a little-endian 16-bit integer in a mutable byte vector.
/// @param blob Destination byte vector.
/// @param offset Starting byte offset, which must have two available bytes.
/// @param value Value to encode.
static void put_u16(std::vector<uint8_t> &blob, size_t offset, uint16_t value) {
    blob[offset] = static_cast<uint8_t>(value & 0xFFu);
    blob[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

/// @brief Store a little-endian 32-bit integer in a mutable byte vector.
/// @param blob Destination byte vector.
/// @param offset Starting byte offset, which must have four available bytes.
/// @param value Value to encode.
static void put_u32(std::vector<uint8_t> &blob, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i)
        blob[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
}

/// @brief Store a little-endian 64-bit integer in a mutable byte vector.
/// @param blob Destination byte vector.
/// @param offset Starting byte offset, which must have eight available bytes.
/// @param value Value to encode.
static void put_u64(std::vector<uint8_t> &blob, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; ++i)
        blob[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
}

/// @brief Append a little-endian 16-bit integer to a byte vector.
/// @param blob Destination byte vector.
/// @param value Value to append.
static void append_u16(std::vector<uint8_t> &blob, uint16_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 2);
    put_u16(blob, offset, value);
}

/// @brief Append a little-endian 64-bit integer to a byte vector.
/// @param blob Destination byte vector.
/// @param value Value to append.
static void append_u64(std::vector<uint8_t> &blob, uint64_t value) {
    size_t offset = blob.size();
    blob.resize(offset + 8);
    put_u64(blob, offset, value);
}

/// @brief Build a valid one-entry ZPAK whose compressed payload is malformed.
/// @details The archive and TOC are structurally valid, ensuring the failure
///          occurs inside trap-capable DEFLATE decoding rather than parsing.
/// @return Byte vector containing the complete borrowed embedded archive.
static std::vector<uint8_t> make_corrupt_compressed_zpak() {
    static const char kName[] = "corrupt-packed.bin";
    std::vector<uint8_t> blob(32, 0);
    blob[0] = 'Z';
    blob[1] = 'P';
    blob[2] = 'A';
    blob[3] = 'K';
    put_u16(blob, 4, RT_ZPAK_VERSION_1);
    put_u16(blob, 6, RT_ZPAK_HEADER_FLAG_COMPRESSED);
    put_u32(blob, 8, 1);

    const uint64_t data_offset = blob.size();
    blob.push_back(0xFF);
    blob.push_back(0x00);

    const uint64_t toc_offset = blob.size();
    append_u16(blob, static_cast<uint16_t>(sizeof(kName) - 1));
    blob.insert(blob.end(), kName, kName + sizeof(kName) - 1);
    append_u64(blob, data_offset);
    append_u64(blob, 32);
    append_u64(blob, 2);
    append_u16(blob, 1);
    append_u16(blob, 0);

    put_u64(blob, 12, toc_offset);
    put_u64(blob, 20, static_cast<uint64_t>(blob.size()) - toc_offset);
    return blob;
}

/// @brief Race lazy initialization and verify every caller sees one full registry.
/// @details Threads begin together, call the idempotent initializer with the
///          same process-lifetime blob, and immediately query its sole entry.
///          A caller that returned while another thread was still parsing or
///          discovering packs would observe a false miss and fail the test.
/// @param blob Structurally valid embedded ZPAK retained for process lifetime.
static void initialize_asset_manager_concurrently(const std::vector<uint8_t> &blob) {
    constexpr int kThreadCount = 24;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> all_observed{true};
    rt_string packed_name = rt_const_cstr("corrupt-packed.bin");
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            rt_asset_init(blob.data(), static_cast<uint64_t>(blob.size()));
            if (rt_asset_exists(packed_name) != 1)
                all_observed.store(false, std::memory_order_release);
        });
    }
    while (ready.load(std::memory_order_acquire) != kThreadCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread &thread : threads)
        thread.join();
    assert(all_observed.load(std::memory_order_acquire));
}

/// @brief Verify corrupt packed data cannot leave the global asset lock held.
/// @details Catches the expected decompression trap and immediately performs a
///          second manager operation. The second operation would deadlock if
///          trap unwinding skipped an asset-lock release.
static void test_corrupt_pack_trap_does_not_poison_asset_manager() {
    static const std::vector<uint8_t> blob = make_corrupt_compressed_zpak();
    initialize_asset_manager_concurrently(blob);

    bool trapped = false;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        (void)rt_asset_load_bytes(rt_const_cstr("corrupt-packed.bin"));
    } else {
        trapped = true;
    }
    const std::string message = rt_trap_get_error();
    rt_trap_clear_recovery();

    assert(trapped);
    assert(!message.empty());
    assert(rt_asset_exists(rt_const_cstr("missing-after-corrupt-pack.bin")) == 0);
}

static void write_file(const char *path, const char *data) {
    FILE *fp = fopen(path, "wb");
    assert(fp != nullptr);
    if (data && data[0] != '\0')
        assert(fwrite(data, 1, strlen(data), fp) == strlen(data));
    fclose(fp);
}

static void test_filesystem_zero_byte_asset() {
    char empty_path[512];
    snprintf(empty_path, sizeof(empty_path), "zanna_empty_asset_%d.bin", (int)GETPID());

    unlink_p(empty_path);
    write_file(empty_path, "");

    rt_string name = rt_const_cstr(empty_path);
    assert(rt_asset_exists(name) == 1);
    assert(rt_asset_size(name) == 0);

    void *bytes = rt_asset_load_bytes(name);
    assert(bytes != nullptr);
    assert(rt_bytes_len(bytes) == 0);

    unlink_p(empty_path);
}

static void test_filesystem_directories_are_not_assets() {
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "zanna_asset_dir_%d", (int)GETPID());

    rmdir_p(dir_path);
    mkdir_p(dir_path);

    rt_string name = rt_const_cstr(dir_path);
    assert(rt_asset_exists(name) == 0);
    assert(rt_asset_size(name) == -1);
    assert(rt_asset_load_bytes(name) == nullptr);

    rmdir_p(dir_path);
}

static void test_missing_asset_size_sentinel() {
    char missing_path[512];
    snprintf(missing_path, sizeof(missing_path), "zanna_missing_asset_%d.bin", (int)GETPID());

    unlink_p(missing_path);
    rt_string name = rt_const_cstr(missing_path);
    assert(rt_asset_exists(name) == 0);
    assert(rt_asset_size(name) == -1);
    assert(rt_asset_load_bytes(name) == nullptr);
}

static void test_unsafe_asset_names_are_rejected() {
    char absolute_path[512];
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = ".";
    snprintf(
        absolute_path, sizeof(absolute_path), "%s\\zanna_abs_asset_%d.bin", tmp, (int)GETPID());
#else
    snprintf(absolute_path, sizeof(absolute_path), "/tmp/zanna_abs_asset_%d.bin", (int)GETPID());
#endif
    unlink_p(absolute_path);
    write_file(absolute_path, "secret");

    rt_string absolute = rt_const_cstr(absolute_path);
    assert(rt_asset_exists(absolute) == 0);
    assert(rt_asset_size(absolute) == -1);
    assert(rt_asset_load_bytes(absolute) == nullptr);

    rt_string parent = rt_const_cstr("../not_an_asset.bin");
    assert(rt_asset_exists(parent) == 0);
    assert(rt_asset_size(parent) == -1);
    assert(rt_asset_load_bytes(parent) == nullptr);

    rt_string drive = rt_const_cstr("C:bad.bin");
    assert(rt_asset_exists(drive) == 0);
    assert(rt_asset_size(drive) == -1);
    assert(rt_asset_load_bytes(drive) == nullptr);

    unlink_p(absolute_path);
}

#ifndef _WIN32
static void test_loose_symlink_asset_rejected() {
    char real_path[512];
    char link_path[512];
    snprintf(real_path, sizeof(real_path), "zanna_real_asset_%d.bin", (int)GETPID());
    snprintf(link_path, sizeof(link_path), "zanna_link_asset_%d.bin", (int)GETPID());

    unlink_p(real_path);
    unlink_p(link_path);
    write_file(real_path, "secret");
    assert(symlink(real_path, link_path) == 0);

    rt_string name = rt_const_cstr(link_path);
    assert(rt_asset_exists(name) == 0);
    assert(rt_asset_size(name) == -1);
    assert(rt_asset_load_bytes(name) == nullptr);

    unlink_p(link_path);
    unlink_p(real_path);
}
#endif

/// @brief A recognized image/audio extension whose bytes fail to decode must
///        return NULL, not silently downgrade to Bytes (VDOC-181), while an
///        unrecognized extension still returns raw Bytes.
static void test_recognized_decode_failure_returns_null() {
    char png_path[512];
    snprintf(png_path, sizeof(png_path), "zanna_corrupt_asset_%d.png", (int)GETPID());
    unlink_p(png_path);
    write_file(png_path, "this is definitely not a valid PNG file");

    rt_string png_name = rt_const_cstr(png_path);
    // Recognized (.png) but malformed: Load must fail, not return Bytes.
    void *typed = rt_asset_load(png_name);
    assert(typed == nullptr);
    // The raw-bytes accessor still returns the bytes unconditionally.
    void *raw = rt_asset_load_bytes(png_name);
    assert(raw != nullptr && rt_bytes_len(raw) > 0);
    unlink_p(png_path);

    // An unrecognized extension still returns Bytes from Load.
    char bin_path[512];
    snprintf(bin_path, sizeof(bin_path), "zanna_unknown_asset_%d.dat", (int)GETPID());
    unlink_p(bin_path);
    write_file(bin_path, "arbitrary payload");
    rt_string bin_name = rt_const_cstr(bin_path);
    void *bytes = rt_asset_load(bin_name);
    assert(bytes != nullptr && rt_bytes_len(bytes) > 0);
    unlink_p(bin_path);
}

#if RT_PLATFORM_WINDOWS
/// @brief Verify mounted-pack identity uses Unicode ordinal case folding, not UTF-8 byte folding.
static void test_windows_unicode_pack_path_identity() {
    const std::vector<uint8_t> blob = make_corrupt_compressed_zpak();
    char upper_path[512];
    char lower_path[512];
    snprintf(upper_path, sizeof(upper_path), "zanna_\xC3\x84_pack_%d.zpak", (int)GETPID());
    snprintf(lower_path, sizeof(lower_path), "zanna_\xC3\xA4_pack_%d.zpak", (int)GETPID());
    wchar_t *wide_path = rt_file_path_utf8_to_wide(upper_path);
    assert(wide_path != nullptr);
    _wunlink(wide_path);
    FILE *file = _wfopen(wide_path, L"wb");
    assert(file != nullptr);
    assert(fwrite(blob.data(), 1, blob.size(), file) == blob.size());
    assert(fclose(file) == 0);

    assert(rt_asset_mount(rt_const_cstr(upper_path)) == 1);
    assert(rt_asset_unmount(rt_const_cstr(lower_path)) == 1);
    assert(rt_asset_unmount(rt_const_cstr(upper_path)) == 0);

    assert(_wunlink(wide_path) == 0);
    free(wide_path);
}
#endif

int main() {
    test_corrupt_pack_trap_does_not_poison_asset_manager();
    test_recognized_decode_failure_returns_null();
    test_filesystem_zero_byte_asset();
    test_filesystem_directories_are_not_assets();
    test_missing_asset_size_sentinel();
    test_unsafe_asset_names_are_rejected();
#if RT_PLATFORM_WINDOWS
    test_windows_unicode_pack_path_identity();
#endif
#ifndef _WIN32
    test_loose_symlink_asset_rejected();
#endif
    return 0;
}
