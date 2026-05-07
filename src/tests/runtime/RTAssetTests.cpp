//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_asset.h"
#include "rt_bytes.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static void write_file(const char *path, const char *data) {
    FILE *fp = fopen(path, "wb");
    assert(fp != nullptr);
    if (data && data[0] != '\0')
        assert(fwrite(data, 1, strlen(data), fp) == strlen(data));
    fclose(fp);
}

static void test_filesystem_zero_byte_asset() {
    char empty_path[512];
    snprintf(empty_path, sizeof(empty_path), "viper_empty_asset_%d.bin", (int)GETPID());

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
    snprintf(dir_path, sizeof(dir_path), "viper_asset_dir_%d", (int)GETPID());

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
    snprintf(missing_path, sizeof(missing_path), "viper_missing_asset_%d.bin", (int)GETPID());

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
    snprintf(absolute_path, sizeof(absolute_path), "%s\\viper_abs_asset_%d.bin", tmp, (int)GETPID());
#else
    snprintf(absolute_path, sizeof(absolute_path), "/tmp/viper_abs_asset_%d.bin", (int)GETPID());
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
    snprintf(real_path, sizeof(real_path), "viper_real_asset_%d.bin", (int)GETPID());
    snprintf(link_path, sizeof(link_path), "viper_link_asset_%d.bin", (int)GETPID());

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

int main() {
    test_filesystem_zero_byte_asset();
    test_filesystem_directories_are_not_assets();
    test_missing_asset_size_sentinel();
    test_unsafe_asset_names_are_rejected();
#ifndef _WIN32
    test_loose_symlink_asset_rejected();
#endif
    return 0;
}
