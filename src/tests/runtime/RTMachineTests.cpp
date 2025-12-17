//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMachineTests.cpp
// Purpose: Tests for Viper.Machine system information queries.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_machine.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_os()
{
    rt_string os = rt_machine_os();
    assert(os != nullptr);

    const char *os_str = rt_string_cstr(os);
    assert(os_str != nullptr);
    assert(rt_len(os) > 0);

    // OS should be one of the known values
    bool valid = strcmp(os_str, "linux") == 0 || strcmp(os_str, "macos") == 0 ||
                 strcmp(os_str, "windows") == 0 || strcmp(os_str, "unknown") == 0;
    assert(valid);

    printf("OS: %s\n", os_str);
}

static void test_os_ver()
{
    rt_string ver = rt_machine_os_ver();
    assert(ver != nullptr);

    const char *ver_str = rt_string_cstr(ver);
    assert(ver_str != nullptr);
    // Version might be "unknown" but should be a valid string
    assert(rt_len(ver) > 0 || strcmp(ver_str, "") == 0);

    printf("OS Version: %s\n", ver_str);
}

static void test_host()
{
    rt_string host = rt_machine_host();
    assert(host != nullptr);

    const char *host_str = rt_string_cstr(host);
    assert(host_str != nullptr);
    // Hostname should not be empty on most systems
    printf("Host: %s\n", host_str);
}

static void test_user()
{
    rt_string user = rt_machine_user();
    assert(user != nullptr);

    const char *user_str = rt_string_cstr(user);
    assert(user_str != nullptr);
    // User should not be empty on most systems
    printf("User: %s\n", user_str);
}

static void test_home()
{
    rt_string home = rt_machine_home();
    assert(home != nullptr);

    const char *home_str = rt_string_cstr(home);
    assert(home_str != nullptr);
    // Home directory should exist on most systems
    printf("Home: %s\n", home_str);
}

static void test_temp()
{
    rt_string temp = rt_machine_temp();
    assert(temp != nullptr);

    const char *temp_str = rt_string_cstr(temp);
    assert(temp_str != nullptr);
    // Temp directory should exist
    assert(rt_len(temp) > 0);

    printf("Temp: %s\n", temp_str);
}

static void test_cores()
{
    int64_t cores = rt_machine_cores();
    // Should be at least 1 core
    assert(cores >= 1);

    printf("Cores: %lld\n", (long long)cores);
}

static void test_mem_total()
{
    int64_t mem = rt_machine_mem_total();
    // Total memory should be positive (at least some MB)
    assert(mem > 1024 * 1024);

    printf("MemTotal: %lld bytes (%.2f GB)\n",
           (long long)mem,
           (double)mem / (1024.0 * 1024.0 * 1024.0));
}

static void test_mem_free()
{
    int64_t mem = rt_machine_mem_free();
    // Free memory should be non-negative
    assert(mem >= 0);

    printf("MemFree: %lld bytes (%.2f GB)\n",
           (long long)mem,
           (double)mem / (1024.0 * 1024.0 * 1024.0));
}

static void test_endian()
{
    rt_string endian = rt_machine_endian();
    assert(endian != nullptr);

    const char *endian_str = rt_string_cstr(endian);
    assert(endian_str != nullptr);

    // Should be "little" or "big"
    bool valid = strcmp(endian_str, "little") == 0 || strcmp(endian_str, "big") == 0;
    assert(valid);

    printf("Endian: %s\n", endian_str);
}

static void test_consistency()
{
    // Call each function multiple times to ensure consistency
    rt_string os1 = rt_machine_os();
    rt_string os2 = rt_machine_os();
    assert(strcmp(rt_string_cstr(os1), rt_string_cstr(os2)) == 0);

    int64_t cores1 = rt_machine_cores();
    int64_t cores2 = rt_machine_cores();
    assert(cores1 == cores2);

    rt_string endian1 = rt_machine_endian();
    rt_string endian2 = rt_machine_endian();
    assert(strcmp(rt_string_cstr(endian1), rt_string_cstr(endian2)) == 0);
}

static void test_mem_relationship()
{
    int64_t total = rt_machine_mem_total();
    int64_t free_mem = rt_machine_mem_free();

    // Free memory should not exceed total memory
    assert(free_mem <= total);
}

int main()
{
    printf("=== Viper.Machine Tests ===\n\n");

    test_os();
    test_os_ver();
    test_host();
    test_user();
    test_home();
    test_temp();
    test_cores();
    test_mem_total();
    test_mem_free();
    test_endian();
    test_consistency();
    test_mem_relationship();

    printf("\nAll tests passed!\n");
    return 0;
}
