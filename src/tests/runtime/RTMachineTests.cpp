//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMachineTests.cpp
// Purpose: Tests for Zanna.System.Machine system information queries.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_machine.h"
#include "rt_platform.h"
#include "rt_string.h"

#if RT_PLATFORM_LINUX
extern "C" rt_string rt_machine_linux_parse_os_release_file(const char *path);
extern "C" int64_t rt_machine_linux_parse_meminfo_file(const char *path);
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if RT_PLATFORM_LINUX
#include "rt_machine_linux_cgroup.h"
#include "rt_machine_linux_helpers.h"

#include <unistd.h>
#endif

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static void test_os() {
    rt_string os = rt_machine_os();
    assert(os != nullptr);

    const char *os_str = rt_string_cstr(os);
    assert(os_str != nullptr);
    assert(rt_str_len(os) > 0);

    // OS should be one of the known values
    bool valid = strcmp(os_str, "linux") == 0 || strcmp(os_str, "macos") == 0 ||
                 strcmp(os_str, "windows") == 0 || strcmp(os_str, "unknown") == 0;
    assert(valid);

    printf("OS: %s\n", os_str);
}

static void test_os_ver() {
    rt_string ver = rt_machine_os_ver();
    assert(ver != nullptr);

    const char *ver_str = rt_string_cstr(ver);
    assert(ver_str != nullptr);
    // Version might be "unknown" but should be a valid string
    assert(rt_str_len(ver) > 0 || strcmp(ver_str, "") == 0);

    printf("OS Version: %s\n", ver_str);
}

static void test_host() {
    rt_string host = rt_machine_host();
    assert(host != nullptr);

    const char *host_str = rt_string_cstr(host);
    assert(host_str != nullptr);
    // Hostname should not be empty on most systems
    printf("Host: %s\n", host_str);
}

static void test_user() {
    rt_string user = rt_machine_user();
    assert(user != nullptr);

    const char *user_str = rt_string_cstr(user);
    assert(user_str != nullptr);
    // User should not be empty on most systems
    printf("User: %s\n", user_str);
}

static void test_home() {
    rt_string home = rt_machine_home();
    assert(home != nullptr);

    const char *home_str = rt_string_cstr(home);
    assert(home_str != nullptr);
    // Home directory should exist on most systems
    printf("Home: %s\n", home_str);
}

static void test_temp() {
    rt_string temp = rt_machine_temp();
    assert(temp != nullptr);

    const char *temp_str = rt_string_cstr(temp);
    assert(temp_str != nullptr);
    // Temp directory should exist
    assert(rt_str_len(temp) > 0);

    printf("Temp: %s\n", temp_str);
}

static void test_cores() {
    int64_t cores = rt_machine_cores();
    // Should be at least 1 core
    assert(cores >= 1);

    printf("Cores: %lld\n", (long long)cores);
}

static void test_mem_total() {
    int64_t mem = rt_machine_mem_total();
    // Total memory should be positive (at least some MB)
    assert(mem > 1024 * 1024);

    printf("MemTotal: %lld bytes (%.2f GB)\n",
           (long long)mem,
           (double)mem / (1024.0 * 1024.0 * 1024.0));
}

static void test_mem_free() {
    int64_t mem = rt_machine_mem_free();
    // Free memory should be non-negative
    assert(mem >= 0);

    printf("MemFree: %lld bytes (%.2f GB)\n",
           (long long)mem,
           (double)mem / (1024.0 * 1024.0 * 1024.0));
}

static void test_endian() {
    rt_string endian = rt_machine_endian();
    assert(endian != nullptr);

    const char *endian_str = rt_string_cstr(endian);
    assert(endian_str != nullptr);

    // Should be "little" or "big"
    bool valid = strcmp(endian_str, "little") == 0 || strcmp(endian_str, "big") == 0;
    assert(valid);

    printf("Endian: %s\n", endian_str);
}

static void test_consistency() {
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

static void test_mem_relationship() {
    int64_t total = rt_machine_mem_total();
    int64_t free_mem = rt_machine_mem_free();

    // Free memory should not exceed total memory
    assert(free_mem <= total);
}

#if RT_PLATFORM_LINUX
static void test_linux_os_release_parser() {
    char path[] = "/tmp/zanna-os-release-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file != nullptr);
    std::string long_prefix(400, 'x');
    fprintf(file, "NAME=%s\nVERSION_ID=\"24.04\\\"-lts\"\n", long_prefix.c_str());
    assert(fclose(file) == 0);

    rt_string version = rt_machine_linux_parse_os_release_file(path);
    assert(version != nullptr);
    assert(strcmp(rt_string_cstr(version), "24.04\"-lts") == 0);
    rt_string_unref(version);
    assert(unlink(path) == 0);
}

static void test_linux_control_parsers() {
    unsigned long long value = 0;
    assert(rt_machine_linux_parse_u64("0", &value) && value == 0);
    assert(rt_machine_linux_parse_u64("18446744073709551615", &value) &&
           value == ULLONG_MAX);
    assert(!rt_machine_linux_parse_u64("18446744073709551616", &value));
    assert(!rt_machine_linux_parse_u64("1 trailing", &value));
    assert(!rt_machine_linux_parse_u64("+1", &value));

    assert(rt_machine_linux_count_cpuset("0-3,5,7-9") == 8);
    assert(rt_machine_linux_count_cpuset("0") == 1);
    assert(rt_machine_linux_count_cpuset("4-2") == 0);
    assert(rt_machine_linux_count_cpuset("0-3,2-4") == 0);
    assert(rt_machine_linux_count_cpuset("2,1") == 0);
    assert(rt_machine_linux_count_cpuset("0,,2") == 0);
    assert(rt_machine_linux_count_cpuset("0,") == 0);
    assert(rt_machine_linux_count_cpuset("1x") == 0);

    assert(rt_machine_linux_cpu_quota(1, 100000) == 1);
    assert(rt_machine_linux_cpu_quota(100000, 100000) == 1);
    assert(rt_machine_linux_cpu_quota(100001, 100000) == 2);
    assert(rt_machine_linux_cpu_quota(250000, 100000) == 3);
    assert(rt_machine_linux_cpu_quota(0, 100000) == 0);
    assert(rt_machine_linux_cpu_quota(100000, 0) == 0);
}

static void test_linux_meminfo_fallback() {
    char path[] = "/tmp/zanna-meminfo-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file != nullptr);
    fprintf(file,
            "MemTotal: 8192 kB\n"
            "MemFree: 100 kB\n"
            "Buffers: 20 kB\n"
            "Cached: 300 kB\n"
            "SReclaimable: 40 kB\n"
            "Shmem: 10 kB\n");
    assert(fclose(file) == 0);
    assert(rt_machine_linux_parse_meminfo_file(path) == 450 * 1024);
    assert(unlink(path) == 0);

    char available_path[] = "/tmp/zanna-memavailable-XXXXXX";
    fd = mkstemp(available_path);
    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != nullptr);
    fprintf(file, "MemFree: 1 kB\nMemAvailable: 777 kB\nCached: 999 kB\n");
    assert(fclose(file) == 0);
    assert(rt_machine_linux_parse_meminfo_file(available_path) == 777 * 1024);
    assert(unlink(available_path) == 0);
}

static void write_fixture_file(char *path_template, const char *contents) {
    int fd = mkstemp(path_template);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file != nullptr);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static void test_linux_cgroup_resolution() {
    char v2_mounts[] = "/tmp/zanna-cgroup-v2-mounts-XXXXXX";
    char v2_membership[] = "/tmp/zanna-cgroup-v2-membership-XXXXXX";
    write_fixture_file(v2_mounts,
                       "36 25 0:32 /tenant /run/cgroup\\040space rw,nosuid - "
                       "cgroup2 cgroup rw\n");
    write_fixture_file(v2_membership, "0::/tenant/workload/leaf\n");
    rt_machine_linux_cgroup_paths_t paths = {};
    assert(zanna_machine_linux_resolve_cgroups(v2_mounts, v2_membership, &paths));
    assert(strcmp(paths.unified, "/run/cgroup space/workload/leaf") == 0);
    assert(unlink(v2_mounts) == 0);
    assert(unlink(v2_membership) == 0);

    char v1_mounts[] = "/tmp/zanna-cgroup-v1-mounts-XXXXXX";
    char v1_membership[] = "/tmp/zanna-cgroup-v1-membership-XXXXXX";
    write_fixture_file(v1_mounts,
                       "40 25 0:40 / /cgroups/cpu rw - cgroup cgroup rw,cpu,cpuacct\n"
                       "41 25 0:41 /pod /cgroups/cpuset rw - cgroup cgroup rw,cpuset\n"
                       "42 25 0:42 / /cgroups/memory rw - cgroup cgroup rw,memory\n");
    write_fixture_file(v1_membership,
                       "2:cpu,cpuacct:/jobs/a\n"
                       "3:cpuset:/pod/limited\n"
                       "4:memory:/containers/m\n");
    paths = {};
    assert(zanna_machine_linux_resolve_cgroups(v1_mounts, v1_membership, &paths));
    assert(strcmp(paths.cpu, "/cgroups/cpu/jobs/a") == 0);
    assert(strcmp(paths.cpuset, "/cgroups/cpuset/limited") == 0);
    assert(strcmp(paths.memory, "/cgroups/memory/containers/m") == 0);
    assert(paths.unified[0] == '\0');
    assert(unlink(v1_mounts) == 0);
    assert(unlink(v1_membership) == 0);
}
#endif

int main() {
    printf("=== Zanna.System.Machine Tests ===\n\n");

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
#if RT_PLATFORM_LINUX
    test_linux_os_release_parser();
    test_linux_control_parsers();
    test_linux_meminfo_fallback();
    test_linux_cgroup_resolution();
#endif

    printf("\nAll tests passed!\n");
    return 0;
}
