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
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = ".";
    snprintf(empty_path, sizeof(empty_path), "%s\\viper_empty_asset_%d.bin", tmp, (int)GETPID());
#else
    snprintf(empty_path, sizeof(empty_path), "/tmp/viper_empty_asset_%d.bin", (int)GETPID());
#endif

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
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = ".";
    snprintf(dir_path, sizeof(dir_path), "%s\\viper_asset_dir_%d", tmp, (int)GETPID());
#else
    snprintf(dir_path, sizeof(dir_path), "/tmp/viper_asset_dir_%d", (int)GETPID());
#endif

    rmdir_p(dir_path);
    mkdir_p(dir_path);

    rt_string name = rt_const_cstr(dir_path);
    assert(rt_asset_exists(name) == 0);
    assert(rt_asset_size(name) == 0);
    assert(rt_asset_load_bytes(name) == nullptr);

    rmdir_p(dir_path);
}

int main() {
    test_filesystem_zero_byte_asset();
    test_filesystem_directories_are_not_assets();
    return 0;
}
